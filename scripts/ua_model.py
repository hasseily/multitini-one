#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original executable Ultimate Audio behavior model; no upstream text copied.

The reference mode models the sampler2 voice visit/prescaler algorithm at
1541ultimate fea2798c. The product law removes fetch-visit pitch differences.
"""
from __future__ import annotations
from dataclasses import dataclass
from fractions import Fraction

REFERENCE_COMMIT = 'fea2798c217674cf0e6eddb121792f3a214ddd9f'


def period_seconds(rate: int, offset: int = 3) -> Fraction:
    return Fraction(2 * rate + offset, 12_500_000)


def upstream_periods(clock_hz: int, rate: int, sixteen_bit: bool,
                     prescale: int, count: int) -> list[int]:
    numerator, denominator = {50_000_000:(1,1), 66_666_667:(3,4),
                              100_000_000:(1,2)}[clock_hz]
    periods = []
    for _ in range(count):
        divider = rate
        visits = 0
        while True:
            visits += 1
            prescale += numerator
            if prescale >= denominator:
                prescale -= denominator
                if divider == 0: break
                divider -= 1
        # Eight voices are visited once per eight system clocks. Fetching
        # one or two bytes takes one or two extra visits, respectively.
        periods.append(8 * (visits + 1 + int(sixteen_bit)))
    return periods


def saturate(value: int, bits: int) -> int:
    return min((1 << (bits - 1)) - 1, max(-(1 << (bits - 1)), value))


def mix(samples: list[int], volumes: list[int], pans: list[int]) -> tuple[int,int]:
    left = right = 0
    for sample, volume, pan in zip(samples, volumes, pans, strict=True):
        scaled = (sample * (volume & 63)) // 32
        pan &= 15
        pl, pr = (7,pan) if pan < 8 else (15-pan,7)
        left = saturate(left + scaled * pl, 21)
        right = saturate(right + scaled * pr, 21)
    return left // 8, right // 8


def deadline_limit(period_clocks: int, voices: int = 7, spacing: int = 32,
                   words: int = 1, periods: int = 1) -> int:
    return ((voices-1)*spacing + periods*period_clocks) // (voices*words)


@dataclass
class Voice:
    """Product voice state, advanced by native clocks and shared half ticks.

    A fetch returns (address, sixteen_bit). The caller may supply its sample
    any time before the next boundary; an absent sample repeats the output.
    All register fields are live. IRQ-set and underrun are one-step events.
    """
    control: int = 0
    start: int = 0
    length: int = 0
    rate: int = 0
    repeat_a: int = 0
    repeat_b: int = 0
    state: str = 'idle'
    position: int = 0
    remaining: int = 0
    output: int = 0
    next_sample: int | None = None
    irq_set: bool = False
    underrun: bool = False

    def step(self, half_tick: bool, paused: bool = False) -> tuple[int,bool] | None:
        self.irq_set = self.underrun = False
        if paused: return None
        gate, repeat = bool(self.control & 1), bool(self.control & 2)
        sixteen = bool(self.control & 0x30)
        stride = (2 if sixteen else 1) * (2 if self.control & 0x40 else 1)
        fetch = False
        if self.state == 'idle':
            if gate:
                self.state, self.position, self.output = 'play', 0, 0
                self.remaining = 2*self.rate+3
                fetch = True
        elif self.state == 'finished':
            if not gate: self.state = 'idle'
        else:
            if half_tick:
                if self.remaining > 1:
                    self.remaining -= 1
                else:
                    if self.next_sample is None:
                        self.underrun = True
                    else:
                        self.output = self.next_sample
                    self.remaining = 2*self.rate+3
                    if self.position == self.repeat_b and gate and repeat:
                        self.position = self.repeat_a
                    elif self.position == self.length:
                        self.state = 'finished'
                        self.irq_set = bool(self.control & 4)
                    fetch = self.state == 'play'
            if not gate and not repeat:
                self.state, fetch = 'idle', False
        if fetch:
            address = (self.start+self.position) & 0xffffff
            self.position = (self.position+stride) & 0xffffff
            self.next_sample = None
            return address, sixteen
        return None


def verify_upstream(rows: list[dict[str,str]], clock_hz: int) -> dict[str,int]:
    checked_periods = checked_bytes = 0
    for scenario in range(21):
        events = [row for row in rows if int(row['scenario']) == scenario]
        fetch = [row for row in events if row['event'] == 'FETCH']
        if scenario < 16:
            rate = (1,281,283,781)[scenario//4]
            mode = scenario % 4
            kind = 0
        else:
            rate = 3 if scenario in (18,19) else 1
            mode = 1 if scenario in (17,19) else 0
            kind = 2 if scenario in (18,19) else (3 if scenario == 20 else 1)
        sixteen = bool(mode & 1)
        stride = (2 if sixteen else 1) * (2 if mode & 2 else 1)
        lows = [row for row in fetch if not sixteen or not (int(row['tag'],16)&1)]
        if not lows: raise AssertionError(f'No fetches in reference case {scenario}')
        if kind != 2:
            for index, row in enumerate(lows):
                if int(row['address'],16) != index * stride:
                    raise AssertionError(f'Incorrect reference address case {scenario} index {index}')
        else:
            addresses = [int(row['address'],16) for row in lows]
            if addresses[:8] != list(range(0,8*stride,stride)):
                raise AssertionError('Reference loop entry sequence')
            if addresses[8] != 2*stride or max(addresses) <= 8*stride:
                raise AssertionError('Reference loop or release/B==length quirk missing')
        intervals = [int(b['cycle']) - int(a['cycle']) for a,b in zip(lows,lows[1:])]
        denominator = {50_000_000:1,66_666_667:4,100_000_000:2}[clock_hz]
        # Soft control stop does not reset upstream's fractional phase.
        if not any(intervals == upstream_periods(clock_hz,rate,sixteen,p,len(intervals))
                   for p in range(denominator)):
            raise AssertionError(f'Upstream visit/prescaler mismatch case {scenario}: {intervals[:12]}')
        if sixteen:
            if len(fetch)%2: raise AssertionError('Incomplete two-byte reference sample')
            for lo,hi in zip(fetch[::2],fetch[1::2]):
                if int(hi['address'],16) != int(lo['address'],16)+1 or int(hi['cycle'])-int(lo['cycle']) != 8:
                    raise AssertionError('Reference 16-bit fetch ordering')
        irqs = [row for row in events if row['event']=='IRQ' and row['irq']=='1']
        if bool(irqs) != (kind in (0,3)):
            raise AssertionError(f'Reference IRQ/end mismatch case {scenario}')
        checked_periods += len(intervals)
        checked_bytes += len(fetch)
    return {'cases':21,'periods':checked_periods,'fetch_bytes':checked_bytes}
