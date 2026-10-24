# SPDX-License-Identifier: GPL-3.0-only
"""Pinned executable subset of the project NMI design model (2026-10-24).

This is a source test fixture: it never reads or rewrites design documents.
The history checker independently detects lost, duplicated and late edges.
"""
import bisect
import random
F = 64
LAG = 2

class Island:
    """NMI path from the registered native term to the T65.

    variant: 'none'      live registered term at every enable;
             'latch'     v3 draft: a fall sets hold; while hold = 1 the T65
                         sees 0; hold clears after an enable that showed 0;
             'rearm'     design rule (nmi_seen_q, fell_q, force_q); an enable
                         counts a fall on its own clock and its clear wins;
             'setwins'   ablation: an enable ignores a fall on its own clock,
                         and that fall survives the enable's clear;
             'clearwins' ablation: an enable ignores a fall on its own clock,
                         and the enable's clear drops it.
    switch:  'carry'  design rule: turbo->exact at the first turbo enable
                      boundary, for every cause; fell_q and force_q are kept,
                      and the first exact enables show the rule instead of
                      B0 (0 if force_q; else 0 if fell_q and nmi_seen_q = 1;
                      else 1 and set force_q if fell_q; else B0) until one
                      shows B0 and leaves force_q = 0; then both are held at
                      0. Exact->turbo keeps nmi_seen_q, and fell_q and
                      force_q pass to the turbo rule unchanged;
             'two'    ablation: as 'carry', but the rule stops after at most
                      two exact enables (the second only to show the forced
                      0), whatever B0 shows;
             'tclear' ablation: as 'carry', but exact->turbo clears fell_q
                      and force_q;
             'wait'   ablation (the rule the carry replaced): turbo->exact only
                      at a turbo enable that showed the live registered term
                      and left force_q = 0;
             'force0' ablation: turbo->exact at any enable with force_q = 0
                      after it (a held 0 may be the last turbo sample);
             'nowait' ablation: turbo->exact at once, force_q and fell_q
                      dropped, B0 from the first exact enable;
             'seen1'  ablation: as 'carry', but exact->turbo starts with
                      nmi_seen_q = 1.
    """

    def __init__(self, variant, last0, switch='carry'):
        self.v = variant
        self.sw = switch
        self.t65 = last0   # T65 NMI_n_o
        self.seen = last0  # nmi_seen_q
        self.fell = 0      # fell_q
        self.force = 0     # force_q
        self.hold = 0      # v3 nmi_hold_q
        self.carry = False  # exact enables still finish fell_q / force_q
        self.carry_n = 0    # exact enables taken from the rule, this carry
        self.carry_max = 0
        self.live = True   # the last turbo enable showed the live term
        self.due = False   # a turbo->exact switch is due
        self.edges = []

    def shown_turbo(self, fall, live):
        v = self.v
        self.live = True
        if v == 'none':
            return live
        if v == 'latch':
            self.live = not self.hold
            p = 0 if self.hold else live
            self.hold = 1 if fall else (0 if p == 0 else self.hold)
            return p
        fell = self.fell or (fall and v == 'rearm')
        if self.force:
            p, self.force = 0, 0
        elif fell and self.seen:
            p = 0
        elif fell:
            p, self.force = 1, 1
        else:
            p = live
        self.live = p == live and not self.force
        self.fell = 1 if (fall and v == 'setwins') else 0
        return p

    def shown_exact(self, fall, b0):
        """The carry: exact enables after turbo->exact show the rule until one
        shows B0 and leaves force_q = 0 ('two': at most two enables)."""
        if not self.carry:
            return b0
        fell = self.fell or fall
        if self.force:
            p, self.force = 0, 0
        elif fell and self.seen:
            p = 0
        elif fell:
            p, self.force = 1, 1
        else:
            p = b0
        self.fell = 0
        self.carry_n += 1
        self.carry_max = max(self.carry_max, self.carry_n)
        if self.sw == 'two':
            self.carry = bool(self.force)
        else:
            self.carry = bool(self.force) or p != b0
        return p

    def idle(self, fall):
        if fall:
            self.fell = 1
            self.hold = 1

    def sample(self, t, p):
        if self.t65 == 1 and p == 0:
            self.edges.append(t)
        self.t65 = p
        self.seen = p

def wave(changes, init, T):
    ch, v, out = dict(changes), init, []
    for t in range(T):
        v = ch.get(t, v)
        out.append(v)
    return out

def registered(native, init):
    return [init] * LAG + native[:-LAG]

def check(edges, ens, reg):
    """Every fall of the registered term must be taken by a T65 edge at the
    first or second enable at or after it; one edge takes every open fall up
    to it (merging). An edge with no open fall is an extra NMI."""
    falls = [t for t in range(1, len(reg)) if reg[t - 1] == 1 and reg[t] == 0]
    idx = {e: i for i, e in enumerate(ens)}
    open_, missed, extra, later = falls, 0, 0, 0
    for e in edges:
        got = [f for f in open_ if f <= e]
        if not got:
            extra += 1
            continue
        for f in got:
            d = idx[e] - bisect.bisect_left(ens, f)
            missed += d > 1
            later += d == 1
        open_ = [f for f in open_ if f > e]
    for f in open_:
        if bisect.bisect_left(ens, f) + 1 < len(ens):
            missed += 1
    return missed, extra, later, len(falls)

def random_enables(rng, T):
    en, t = set(), 0
    while t < T:
        en.add(t)
        t += 1 if rng.random() < 0.7 else rng.randint(2, 6 * F)
    return en
