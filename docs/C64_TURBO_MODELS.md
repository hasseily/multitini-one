# C64 turbo models: performance, fast I/O and clock-plan arithmetic

Throwaway arithmetic written on 2026-10-23 for [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md) and its sub-designs, against commit 5341812 (branch c64-turbo). These are models, not measurements: every figure they produce is tagged [I] where it is used. Nothing here is board-tested.

**How to run.** Save every script below under its heading's name in one folder and run it from that folder with `python <name>.py` (Python 3, standard library only). `final.py` and `rot.py` import `perf.py`; `final_model.py`, `top_rows_model.py`, `rev_fastio_latch.py` and `rev_collision_alias.py` load sibling models from their own folder. `python -I` drops the script's folder from the import path, so `final.py` and `rot.py` fail under it. Random draws use fixed seeds, so the output is repeatable. The outputs shown were regenerated on 2026-10-23 with Python 3.12.0; `top_rows_model.py` matches its design-stage run. `ntsc_plans.py` and its output were replaced on 2026-10-24 (design section 14.4, finding 6): the search hits and constants are unchanged, the N5 verdicts and the labels changed, and check lines were added.

The last six scripts in the table were added on 2026-10-24 with the fixes of design section 14.4. `governor_credit.py` loads `perf.py` and `final.py` by path from its own folder, so it runs under `python -I`; it takes about 40 s because of its exhaustive pattern search. The other five are standalone.

Script comments that name FINAL_DESIGN_v2 mean the v2 draft of the design, which is not kept; the section they cite, 2.13.4, has the same number in C64_TURBO_DESIGN.md. In the table below, FAST_VIC, FAST_SID, BA_LOW_IO and NTSC mean the C64_TURBO_*.md sub-designs of those names.

| Script | Cited by | What it computes |
|---|---|---|
| [perf.py](#perfpy) | design §7 | First performance model: grant table, forward cutoff, Amdahl I/O share, scaling beyond 64x. |
| [final.py](#finalpy) | design §8 | Performance model with rotated grants. Imports `perf.py`. Its N = 1 values predate the completion credit of design §2.7 (see governor_credit.py). |
| [rot.py](#rotpy) | design (model list) | Grant-rotation check on the `perf.py` grant table. Imports `perf.py`. Its N = 1 values predate the completion credit of design §2.7. |
| [sim.py](#simpy) | design §8 | Reviewer's independent model, including the IRQ-mask starvation check behind the late-acknowledge rule. |
| [plan_ps.py](#plan_pspy) | design §3.1 | PAL MMCM plan search under Vivado's 1 ps period rounding. |
| [vicread_model.py](#vicread_modelpy) | design §2.13.2, §2.13.4; FAST_VIC §3.3 | Fast VIC read cost per speed index against the C64U and U64E measurements. |
| [vicpath_model.py](#vicpath_modelpy) | design §2.13.1, §2.13.6; FAST_VIC §3.8, §3.10, §3.15 | Loop costs with posted writes and barriers, and the #203 pixel pattern. |
| [sid_fio_model.py](#sid_fio_modelpy) | FAST_SID (header) | SID cost under the rejected non-posted variant, kept for the record. |
| [lead_model.py](#lead_modelpy) | design §2.13.4, §8 | Costs of the merged fast I/O path: write barrier, fetch-ordered local writes, shared VIC/SID FIFO. |
| [final_model.py](#final_modelpy) | design §2.13.2, §2.13.4, §8, §14.1 | Final barrier rules: side-effect reads, selective clears, φ2 forwards, the 26-cycle Doom `miPlay` loop. Loads `lead_model.py`. |
| [top_rows_model.py](#top_rows_modelpy) | design §0.3, §2.13.4, §2.13.6, §7, §8, §13.7 | The `final_model.py` and `final.py` rules at 48 and 64 slots, with the C64U rows 47 and 63 for comparison. Loads `final_model.py`, `vicpath_model.py` and (through `final_model.py`) `lead_model.py`. |
| [rev_fastio_latch.py](#rev_fastio_latchpy) | design §14.1 finding 1 | Reviewer model that found finding 1 of design section 14.1. Loads `lead_model.py`. |
| [rev_collision_alias.py](#rev_collision_aliaspy) | design §2.13.4, §14.1 finding 2 | Reviewer model that found finding 2 of design section 14.1. Loads `lead_model.py`. |
| [balow_calc.py](#balow_calcpy) | design §2.13.8, §8; BA_LOW_IO §3.3, §3.10 | BA-low exposure and #665 arithmetic. |
| [ntsc_plans.py](#ntsc_planspy) | design §3.1, §13.3, §13.7; NTSC §3.3, §5 | NTSC MMCM plan search with a ps-exact 2:1 test, and constants. |
| [governor_credit.py](#governor_creditpy) | design §1.7, §2.7, §10.2 | Forward-completion credit against the old rule and the rejected own-grant rule: I/O loops at N = 1-64, the `meas_c` cart column, the exhaustive periodic-pattern maximum and the governor invariants. Loads `perf.py` and `final.py`. |
| [nmi_rearm.py](#nmi_rearmpy) | design §2.9, §9 (R35) | NMI hold and rearm compared with no latch, the old set/clear latch and stock, and turbo → exact switch rules (the carry against the wait it replaced): directed cases S0-S12 and fixed-seed fuzz turbo only, with exact/turbo switches, and with a forced switch after pulses on any clock. |
| [vic_t2_sticky.py](#vic_t2_stickypy) | design §2.13.6, §10.2 (T2); FAST_VIC §3.7, §6 (T2) | T2 sticky-state check: DUT vs collapsed reference vs per-event scoreboard vs stock with separate-cycle writes. |
| [sid_edge_stage.py](#sid_edge_stagepy) | design §2.13.7, §10.2; FAST_SID §3.4, §6.1 | Literal edge-stage model: kept and merged gate/test pulse cases with expected engine samples. |
| [svf_persistence.py](#svf_persistencepy) | design §10.2; FAST_SID §6.1 | How long a one-cycle F0, routing or resonance difference lasts in the SID filter integrators. |
| [preload_cache.py](#preload_cachepy) | design §6.4 (a), §10.2 (X-PRELOAD) | REU image preload against a write-back PS data cache: old flush-after-copy rule, aligned pre-invalidate and non-cacheable mapping. |


## perf.py

First performance model: grant table, forward cutoff, Amdahl I/O share, scaling beyond 64x.

```python
# Performance model for the synthesized design (scratch arithmetic only).
# 64 fast clocks per native cycle, f = 0..63. Grants: N per frame, Bresenham.
# A forwarded access whose request register is loaded at fast index f <= CUT
# is served in the current native cycle (retire at f=64); otherwise next one.
# The island resumes (completion Enable) at f = RESUME of the following frame.
import random

FRAME = 64

def grants(N):
    if N >= 64:
        return [True] * 64
    m = [False] * 64
    acc = 0
    for s in range(64):
        acc += N
        if acc >= 64:
            acc -= 64
            m[s] = True
    return m

def run(seq, N, CUT=28, RESUME=5):
    m = grants(N)
    t = RESUME
    for c in seq:
        if c == 'L':
            while not m[t % FRAME]:
                t += 1
            t += 1
        else:
            f = t % FRAME
            base = t - f
            retire = base + FRAME if f <= CUT else base + 2 * FRAME
            t = retire + RESUME
    return t

def speed(seq, N, reps=400, **kw):
    s = seq * reps
    t = run(s, N, **kw)
    return len(s) * FRAME / t, t / reps

TABLE = [1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 47, 63]

loops = {
    'RAM: lda abs,x / dex / bne': list('LLLL' + 'LL' + 'LLL'),
    'I/O read: lda $dc01,x / dex / bne': list('LLLI' + 'LL' + 'LLL'),
    'I/O write: sta $d020,x / dex / bne': list('LLLII' + 'LL' + 'LLL'),
    'I/O abs store: sta $d020 / dex / bne': list('LLLI' + 'LL' + 'LLL'),
    'fill: sta $0400,x / sta $d800,x (colour forwarded)': list('LLLLL' + 'LLLII' + 'LL' + 'LLL'),
    'fill: sta $0400,x / sta $d800,x (colour local)': list('LLLLL' + 'LLLLL' + 'LL' + 'LLL'),
}

for resume in (5, 13):
    print('=== RESUME f=%d (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===' % resume)
    for name, seq in loops.items():
        out = []
        for idx in (4, 6, 9, 12, 14, 15):
            N = TABLE[idx]
            x, ft = speed(seq, N, RESUME=resume)
            out.append('i%d(%d):%.1fx/%.2fus' % (idx, N, x, ft / 64))
        print('%-52s %s' % (name, ' '.join(out)))

# Single 4-cycle absolute I/O access repeated: where is the 1-vs-2 phi2 boundary?
seq = list('LLLI')
print('--- unrolled lda $de80 (LLLI): phi2 per access by index ---')
for resume in (5, 13):
    row = []
    for idx, N in enumerate(TABLE):
        x, ft = speed(seq, N, RESUME=resume)
        row.append('%d:%.2f' % (N, ft / 64))
    print('RESUME %2d  ' % resume + ' '.join(row))

random.seed(7)
print('--- random I/O fraction, index 15 (63 slots) and 14 (47) ---')
for p in (0.0, 0.1, 0.5, 1, 2, 5, 10):
    n = 200000
    s = ['I' if random.random() * 100 < p else 'L' for _ in range(n)]
    r = []
    for N in (63, 47, 16):
        t = run(s, N)
        r.append('N=%d %.1fx' % (N, n * FRAME / t))
    print('I/O %5.1f%%: %s' % (p, '  '.join(r)))

# Badline cost with badline timing enabled (screen on, no sprites)
for name, lines, cyc, bl in (('PAL', 312, 63, 25 * 43), ('NTSC', 263, 65, 25 * 43)):
    frac = bl / (lines * cyc)
    print('%s BA-low fraction %.3f -> index 15 RAM loop %.1fx with badline timing enabled' % (name, frac, 63 * (1 - frac)))

# Beyond 64x: local clock ratio r (slots per native cycle), same I/O model scaled
for p in (0.5, 1, 2, 5):
    n = 200000
    random.seed(11)
    s = ['I' if random.random() * 100 < p else 'L' for _ in range(n)]
    base = n * FRAME / run(s, 63)
    # 80x / 96x approximated by scaling local cycle cost only
    def eff(rx):
        loc = sum(1 for c in s if c == 'L')
        io = n - loc
        # each I/O ~ cost from the 63x simulation minus local share
        t63 = run(s, 63) / FRAME  # native cycles
        io_cost = t63 - loc / 63.0
        return n / (loc / rx + io_cost)
    print('I/O %.1f%%: 63x->%.1f  80x->%.1f  96x->%.1f' % (p, base, eff(80), eff(96)))
```

Output:

```text
=== RESUME f=5 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):32.0x/0.28us i14(47):47.0x/0.19us i15(63):62.9x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):3.3x/3.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.8x/4.00us i6(10):5.0x/3.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):7.5x/2.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):47.0x/0.32us i15(63):63.0x/0.24us
=== RESUME f=13 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):31.9x/0.28us i14(47):46.9x/0.19us i15(63):62.8x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):2.5x/4.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.7x/4.00us i6(10):3.8x/4.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):5.0x/3.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):46.9x/0.32us i15(63):62.9x/0.24us
--- unrolled lda $de80 (LLLI): phi2 per access by index ---
RESUME  5  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:1.00 10:1.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
RESUME 13  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:2.00 10:2.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
--- random I/O fraction, index 15 (63 slots) and 14 (47) ---
I/O   0.0%: N=63 63.0x  N=47 47.0x  N=16 16.0x
I/O   0.1%: N=63 59.0x  N=47 44.7x  N=16 15.7x
I/O   0.5%: N=63 46.7x  N=47 37.3x  N=16 14.8x
I/O   1.0%: N=63 37.4x  N=47 31.0x  N=16 13.7x
I/O   2.0%: N=63 27.0x  N=47 23.5x  N=16 12.1x
I/O   5.0%: N=63 15.2x  N=47 13.8x  N=16 8.8x
I/O  10.0%: N=63 9.3x  N=47 8.7x  N=16 6.3x
PAL BA-low fraction 0.055 -> index 15 RAM loop 59.6x with badline timing enabled
NTSC BA-low fraction 0.063 -> index 15 RAM loop 59.0x with badline timing enabled
I/O 0.5%: 63x->47.0  80x->55.8  96x->63.1
I/O 1.0%: 63x->37.8  80x->43.2  96x->47.4
I/O 2.0%: 63x->27.1  80x->29.8  96x->31.7
I/O 5.0%: 63x->15.4  80x->16.2  96x->16.8
```


## final.py

Performance model with rotated grants. Imports `perf.py`. Its N = 1 values in the loop table (index 0 with badline timing disabled) predate the completion credit of design §2.7. With the credit, `governor_credit.py` gives 1:9.00 for `lda $dc01,x/dex/bne`, 1:10.00 for `sta $d020,x/dex/bne`, 1:9.00 for `sta $d020/dex/bne`, 1:4.00 for `unrolled lda $de80` and 1:15.00 for the colour-forwarded fill. The N ≥ 2 values do not change.

```python
import random, perf
TABLE=perf.TABLE
def gr(N, off=5, gap=4):
    if N>=63:
        m=[True]*64
        if N==63: m[gap]=False
        return m
    g=perf.grants(N); first=g.index(True); sh=(off-first)%64
    return [g[(s-sh)%64] for s in range(64)]
def run(seq,N,CUT=30,RES=5):
    m=gr(N); t=RES
    for c in seq:
        if c=='L':
            while not m[t%64]: t+=1
            t+=1
        else:
            f=t%64; b=t-f
            t=(b+64 if f<=CUT else b+128)+RES
    return t
loops={'RAM lda abs,x/dex/bne':'LLLLLLLLL','lda $dc01,x/dex/bne':'LLLILLLLL','sta $d020,x/dex/bne':'LLLIILLLLL',
'sta $d020/dex/bne':'LLLILLLLL','unrolled lda $de80':'LLLI',
'fill $0400,x+$d800,x colour fwd':'LLLLLLLLII'+'LLLLL','fill colour local':'LLLLLLLLLLLLLLL'}
for k,v in loops.items():
    row=[]
    for N in TABLE:
        s=list(v)*300; t=run(s,N)
        row.append('%d:%.2f'%(N,t/300/64))
    print('%-34s'%k,' '.join(row))
random.seed(3)
for p in (0,0.1,0.5,1,2,5,10):
    n=200000; s=['I' if random.random()*100<p else 'L' for _ in range(n)]
    print('I/O %.1f%%:'%p, '  '.join('N=%d %.1fx'%(N,n*64/run(s,N)) for N in (63,47,16,4)))
```

Output:

```text
=== RESUME f=5 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):32.0x/0.28us i14(47):47.0x/0.19us i15(63):62.9x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):3.3x/3.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.8x/4.00us i6(10):5.0x/3.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):7.5x/2.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):47.0x/0.32us i15(63):63.0x/0.24us
=== RESUME f=13 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):31.9x/0.28us i14(47):46.9x/0.19us i15(63):62.8x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):2.5x/4.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.7x/4.00us i6(10):3.8x/4.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):5.0x/3.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):46.9x/0.32us i15(63):62.9x/0.24us
--- unrolled lda $de80 (LLLI): phi2 per access by index ---
RESUME  5  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:1.00 10:1.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
RESUME 13  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:2.00 10:2.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
--- random I/O fraction, index 15 (63 slots) and 14 (47) ---
I/O   0.0%: N=63 63.0x  N=47 47.0x  N=16 16.0x
I/O   0.1%: N=63 59.0x  N=47 44.7x  N=16 15.7x
I/O   0.5%: N=63 46.7x  N=47 37.3x  N=16 14.8x
I/O   1.0%: N=63 37.4x  N=47 31.0x  N=16 13.7x
I/O   2.0%: N=63 27.0x  N=47 23.5x  N=16 12.1x
I/O   5.0%: N=63 15.2x  N=47 13.8x  N=16 8.8x
I/O  10.0%: N=63 9.3x  N=47 8.7x  N=16 6.3x
PAL BA-low fraction 0.055 -> index 15 RAM loop 59.6x with badline timing enabled
NTSC BA-low fraction 0.063 -> index 15 RAM loop 59.0x with badline timing enabled
I/O 0.5%: 63x->47.0  80x->55.8  96x->63.1
I/O 1.0%: 63x->37.8  80x->43.2  96x->47.4
I/O 2.0%: 63x->27.1  80x->29.8  96x->31.7
I/O 5.0%: 63x->15.4  80x->16.2  96x->16.8
RAM lda abs,x/dex/bne              1:9.00 2:4.50 3:3.00 4:2.25 6:1.50 8:1.12 10:0.90 12:0.75 14:0.64 16:0.56 20:0.45 24:0.38 32:0.28 40:0.23 47:0.19 63:0.14
lda $dc01,x/dex/bne                1:8.00 2:5.00 3:3.00 4:3.00 6:2.00 8:2.00 10:2.00 12:2.00 14:2.00 16:2.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
sta $d020,x/dex/bne                1:9.00 2:6.00 3:4.00 4:4.00 6:3.00 8:3.00 10:3.00 12:3.00 14:3.00 16:3.00 20:2.00 24:2.00 32:2.00 40:2.00 47:2.00 63:2.00
sta $d020/dex/bne                  1:8.00 2:5.00 3:3.00 4:3.00 6:2.00 8:2.00 10:2.00 12:2.00 14:2.00 16:2.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
unrolled lda $de80                 1:3.00 2:2.00 3:2.00 4:2.00 6:1.00 8:1.00 10:1.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
fill $0400,x+$d800,x colour fwd    1:14.00 2:8.00 3:6.00 4:5.00 6:4.00 8:4.00 10:3.00 12:3.00 14:3.00 16:3.00 20:3.00 24:3.00 32:2.00 40:2.00 47:2.00 63:2.00
fill colour local                  1:15.00 2:7.50 3:5.00 4:3.75 6:2.50 8:1.87 10:1.50 12:1.25 14:1.07 16:0.94 20:0.75 24:0.63 32:0.47 40:0.38 47:0.32 63:0.24
I/O 0.0%: N=63 63.0x  N=47 47.0x  N=16 16.0x  N=4 4.0x
I/O 0.1%: N=63 59.2x  N=47 44.8x  N=16 15.8x  N=4 4.0x
I/O 0.5%: N=63 47.9x  N=47 38.0x  N=16 14.9x  N=4 4.0x
I/O 1.0%: N=63 38.1x  N=47 31.6x  N=16 13.9x  N=4 3.9x
I/O 2.0%: N=63 27.6x  N=47 24.0x  N=16 12.3x  N=4 3.8x
I/O 5.0%: N=63 15.7x  N=47 14.3x  N=16 9.2x  N=4 3.6x
I/O 10.0%: N=63 9.4x  N=47 8.9x  N=16 6.5x  N=4 3.2x
```


## rot.py

Grant-rotation check on the `perf.py` grant table. Imports `perf.py`. Its N = 1 values (`1:3.00` for LLLI, `1:8.00` for the two loops) predate the completion credit of design §2.7, which gives 4.00 and 9.00 (`governor_credit.py`). The N ≥ 2 values do not change.

```python
import perf
TABLE=perf.TABLE
def grants_rot(N, off):
    g=perf.grants(N)
    # rotate so that pattern's first True lands at index 'off'
    first=g.index(True)
    sh=(off-first)%64
    return [g[(s-sh)%64] for s in range(64)]
def run(seq,N,off,CUT=28,RESUME=5):
    m=grants_rot(N,off); t=RESUME
    for c in seq:
        if c=='L':
            while not m[t%64]: t+=1
            t+=1
        else:
            f=t%64; base=t-f
            t=(base+64 if f<=CUT else base+128)+RESUME
    return t
for off in (5,):
    for name,seq in (('LLLI',list('LLLI')),('lda io,x/dex/bne',list('LLLILLLLL')),('sta abs io',list('LLLILLLLL'))):
        row=[]
        for N in TABLE:
            s=seq*400; t=run(s,N,off)
            row.append('%d:%.2f'%(N,t/400/64))
        print(off,name,' '.join(row))
```

Output:

```text
=== RESUME f=5 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):32.0x/0.28us i14(47):47.0x/0.19us i15(63):62.9x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):3.3x/3.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):4.5x/2.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.8x/4.00us i6(10):5.0x/3.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):7.5x/2.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):47.0x/0.32us i15(63):63.0x/0.24us
=== RESUME f=13 (5: e_1 post sample + IRQ mask; 13: phase-5 resume) ===
RAM: lda abs,x / dex / bne                           i4(6):6.0x/1.50us i6(10):10.0x/0.90us i9(16):16.0x/0.56us i12(32):31.9x/0.28us i14(47):46.9x/0.19us i15(63):62.8x/0.14us
I/O read: lda $dc01,x / dex / bne                    i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
I/O write: sta $d020,x / dex / bne                   i4(6):2.5x/4.00us i6(10):3.3x/3.00us i9(16):3.3x/3.00us i12(32):5.0x/2.00us i14(47):5.0x/2.00us i15(63):5.0x/2.00us
I/O abs store: sta $d020 / dex / bne                 i4(6):3.0x/3.00us i6(10):4.5x/2.00us i9(16):4.5x/2.00us i12(32):9.0x/1.00us i14(47):9.0x/1.00us i15(63):9.0x/1.00us
fill: sta $0400,x / sta $d800,x (colour forwarded)   i4(6):3.7x/4.00us i6(10):3.8x/4.00us i9(16):5.0x/3.00us i12(32):5.0x/3.00us i14(47):5.0x/3.00us i15(63):7.5x/2.00us
fill: sta $0400,x / sta $d800,x (colour local)       i4(6):6.0x/2.50us i6(10):10.0x/1.50us i9(16):16.0x/0.94us i12(32):32.0x/0.47us i14(47):46.9x/0.32us i15(63):62.9x/0.24us
--- unrolled lda $de80 (LLLI): phi2 per access by index ---
RESUME  5  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:1.00 10:1.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
RESUME 13  1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:2.00 10:2.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
--- random I/O fraction, index 15 (63 slots) and 14 (47) ---
I/O   0.0%: N=63 63.0x  N=47 47.0x  N=16 16.0x
I/O   0.1%: N=63 59.0x  N=47 44.7x  N=16 15.7x
I/O   0.5%: N=63 46.7x  N=47 37.3x  N=16 14.8x
I/O   1.0%: N=63 37.4x  N=47 31.0x  N=16 13.7x
I/O   2.0%: N=63 27.0x  N=47 23.5x  N=16 12.1x
I/O   5.0%: N=63 15.2x  N=47 13.8x  N=16 8.8x
I/O  10.0%: N=63 9.3x  N=47 8.7x  N=16 6.3x
PAL BA-low fraction 0.055 -> index 15 RAM loop 59.6x with badline timing enabled
NTSC BA-low fraction 0.063 -> index 15 RAM loop 59.0x with badline timing enabled
I/O 0.5%: 63x->47.0  80x->55.8  96x->63.1
I/O 1.0%: 63x->37.8  80x->43.2  96x->47.4
I/O 2.0%: 63x->27.1  80x->29.8  96x->31.7
I/O 5.0%: 63x->15.4  80x->16.2  96x->16.8
5 LLLI 1:3.00 2:2.00 3:2.00 4:2.00 6:1.00 8:1.00 10:1.00 12:1.00 14:1.00 16:1.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
5 lda io,x/dex/bne 1:8.00 2:5.00 3:3.00 4:3.00 6:2.00 8:2.00 10:2.00 12:2.00 14:2.00 16:2.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
5 sta abs io 1:8.00 2:5.00 3:3.00 4:3.00 6:2.00 8:2.00 10:2.00 12:2.00 14:2.00 16:2.00 20:1.00 24:1.00 32:1.00 40:1.00 47:1.00 63:1.00
```


## sim.py

Reviewer's independent model, including the IRQ-mask starvation check behind the late-acknowledge rule.

```python
# Independent re-check of the design's performance model (review scratch only).
# Rules taken from the design text (sections 2.7, 5.2, 5.7, 5.8):
#  - 64 fast clocks per native cycle, f = 0..63.
#  - Grant table: N Bresenham-spaced ones, rotated so first one at f=5; N=63 -> all but f=4.
#  - Local cycle: enable at end of fast clock t if grant(t). Next cycle ready at t+1.
#  - Forward: request registered at end of fast clock f_r = ready time.  If f_r <= 30
#    the access runs in this native cycle, else the next.  Completion enable at end f=4
#    of the following frame; next cycle ready at f=5.
#  - Colour RAM read: enable one fast clock after the grant (2 slots).
#  - IRQ mask: after each forward completion, IRQ held at 1 for the completion enable
#    and every enable up to and including end f=12 (unmask register flips at end f=12).
import random

def grants(N, off=5):
    if N >= 63:
        m = [True] * 64
        if N == 63:
            m[4] = False
        return m
    g = [False] * 64
    acc = 0
    for s in range(64):
        acc += N
        if acc >= 64:
            acc -= 64
            g[s] = True
    first = g.index(True)
    sh = (off - first) % 64
    return [g[(s - sh) % 64] for s in range(64)]

def run(seq, N, CUT=30, RES=5, trace_irq=False):
    m = grants(N)
    t = RES
    masked_until = -1
    enables = []  # (time, masked)
    for c in seq:
        if c == 'L' or c == 'C':
            while not m[t % 64]:
                t += 1
            if c == 'C':
                t += 1  # colour read: enable one clock later
            enables.append((t, t <= masked_until))
            t += 1
        else:  # forward
            f = t % 64
            b = t - f
            comp = (b + 64 if f <= CUT else b + 128) + 4
            # mask from completion until end f=12 of that frame
            masked_until = comp - 4 + 12
            enables.append((comp, True))
            t = comp + 1
    return t, enables

TABLE = [1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 47, 63]

def per_iter(seq, N, reps=300):
    t, _ = run(list(seq) * reps, N)
    return t / reps / 64.0  # native cycles (us) per iteration

loops = {
    'RAM lda abs,x/dex/bne (9 cyc)': 'LLLLLLLLL',
    'lda $dc01,x/dex/bne (9 cyc)': 'LLLFLLLLL',
    'sta $d020,x/dex/bne (10 cyc)': 'LLLFFLLLLL',
    'unrolled lda $de80 (4 cyc)': 'LLLF',
    'fill colour fwd (15 cyc)': 'LLLLL' + 'LLLFF' + 'LL' + 'LLL',
    'fill colour local, dummy=1 slot': 'LLLLL' + 'LLLLL' + 'LL' + 'LLL',
    'fill colour local, dummy colour read=2 slots': 'LLLLL' + 'LLLCL' + 'LL' + 'LLL',
}
stock = {k: len(v) for k, v in loops.items()}
for k, v in loops.items():
    row = []
    for idx in (1, 2, 3, 4, 9, 12, 14, 15):
        N = TABLE[idx]
        us = per_iter(v, N)
        row.append('i%d:%.2fus/%.1fx' % (idx, us, stock[k] / us))
    print('%-46s %s' % (k, ' '.join(row)))

print('--- random I/O share (Amdahl), 3 seeds ---')
for p in (0.1, 0.5, 1, 2, 5, 10):
    out = []
    for N in (63, 47, 16):
        xs = []
        for seed in (1, 2, 3):
            random.seed(seed)
            n = 200000
            s = ['F' if random.random() * 100 < p else 'L' for _ in range(n)]
            t, _ = run(s, N)
            xs.append(n * 64 / t)
        out.append('N=%d %.1fx' % (N, sum(xs) / len(xs)))
    print('I/O %4.1f%%: %s' % (p, '  '.join(out)))

print('--- IRQ starvation: loops with k local cycles per forward ---')
for idx in (15, 14, 12, 9, 4, 1):
    N = TABLE[idx]
    starve = []
    for k in range(1, 16):
        _, en = run((['L'] * k + ['F']) * 50, N)
        en = en[20:]
        if all(msk for _, msk in en):
            starve.append(k)
    print('index %2d (N=%2d): every T65 enable is IRQ-masked for k in %s' % (idx, N, starve))

# BA-low fraction with badline timing enabled
for name, lines, cyc in (('PAL', 312, 63), ('NTSC', 263, 65)):
    frac = 25 * 43 / (lines * cyc)
    print('%s BA-low %.4f -> 63x * (1-frac) = %.2fx' % (name, frac, 63 * (1 - frac)))

# Dormann simulation cost through the island at various N (fast clocks)
for N in (63, 47, 16, 2):
    clocks = 96241364 * 64 / N
    print('Dormann N=%2d: %.3g fast clocks -> %.1f min at 208k clk/s' % (N, clocks, clocks / 208000 / 60))
```

Output:

```text
RAM lda abs,x/dex/bne (9 cyc)                  i1:4.50us/2.0x i2:3.00us/3.0x i3:2.25us/4.0x i4:1.50us/6.0x i9:0.56us/16.0x i12:0.28us/32.0x i14:0.19us/46.9x i15:0.14us/62.9x
lda $dc01,x/dex/bne (9 cyc)                    i1:5.00us/1.8x i2:3.00us/3.0x i3:3.00us/3.0x i4:2.00us/4.5x i9:2.00us/4.5x i12:1.00us/9.0x i14:1.00us/9.0x i15:1.00us/9.0x
sta $d020,x/dex/bne (10 cyc)                   i1:6.00us/1.7x i2:4.00us/2.5x i3:4.00us/2.5x i4:3.00us/3.3x i9:3.00us/3.3x i12:2.00us/5.0x i14:2.00us/5.0x i15:2.00us/5.0x
unrolled lda $de80 (4 cyc)                     i1:2.00us/2.0x i2:2.00us/2.0x i3:2.00us/2.0x i4:1.00us/4.0x i9:1.00us/4.0x i12:1.00us/4.0x i14:1.00us/4.0x i15:1.00us/4.0x
fill colour fwd (15 cyc)                       i1:8.00us/1.9x i2:6.00us/2.5x i3:5.00us/3.0x i4:4.00us/3.8x i9:3.00us/5.0x i12:2.00us/7.5x i14:2.00us/7.5x i15:2.00us/7.5x
fill colour local, dummy=1 slot                i1:7.50us/2.0x i2:5.00us/3.0x i3:3.75us/4.0x i4:2.50us/6.0x i9:0.94us/16.0x i12:0.47us/32.0x i14:0.32us/47.0x i15:0.24us/62.9x
fill colour local, dummy colour read=2 slots   i1:7.50us/2.0x i2:5.00us/3.0x i3:3.75us/4.0x i4:2.50us/6.0x i9:0.94us/16.0x i12:0.47us/32.0x i14:0.33us/45.0x i15:0.25us/59.9x
--- random I/O share (Amdahl), 3 seeds ---
I/O  0.1%: N=63 58.9x  N=47 44.7x  N=16 15.8x
I/O  0.5%: N=63 47.0x  N=47 37.6x  N=16 14.8x
I/O  1.0%: N=63 37.6x  N=47 31.4x  N=16 13.8x
I/O  2.0%: N=63 27.4x  N=47 23.9x  N=16 12.2x
I/O  5.0%: N=63 15.7x  N=47 14.4x  N=16 9.2x
I/O 10.0%: N=63 9.4x  N=47 9.0x  N=16 6.6x
--- IRQ starvation: loops with k local cycles per forward ---
index 15 (N=63): every T65 enable is IRQ-masked for k in [1, 2, 3, 4, 5, 6, 7, 8]
index 14 (N=47): every T65 enable is IRQ-masked for k in [1, 2, 3, 4, 5, 6]
index 12 (N=32): every T65 enable is IRQ-masked for k in [1, 2, 3, 4]
index  9 (N=16): every T65 enable is IRQ-masked for k in [1, 2]
index  4 (N= 6): every T65 enable is IRQ-masked for k in [1]
index  1 (N= 2): every T65 enable is IRQ-masked for k in [1]
PAL BA-low 0.0547 -> 63x * (1-frac) = 59.55x
NTSC BA-low 0.0629 -> 63x * (1-frac) = 59.04x
Dormann N=63: 9.78e+07 fast clocks -> 7.8 min at 208k clk/s
Dormann N=47: 1.31e+08 fast clocks -> 10.5 min at 208k clk/s
Dormann N=16: 3.85e+08 fast clocks -> 30.8 min at 208k clk/s
Dormann N= 2: 3.08e+09 fast clocks -> 246.8 min at 208k clk/s
```


## plan_ps.py

PAL MMCM plan search under Vivado's 1 ps period rounding.

```python
# Search MMCM plans (Fin = clk_fpga_2) where c64 = fast/2 exactly AND Vivado's
# ps-rounded periods (master modelled as 6.666 ns) stay exactly 2:1, including
# the fast clock's falling edge. Design-stage arithmetic only.
from fractions import Fraction as F

MASTER_PS = F(6666)          # Vivado's clk_fpga_2 period (clocks.rpt:28)
FIN = F(150_000_000)         # physical input
targets = {"PAL": F(31527954), "NTSC": F(32727266)}

def rnd(x):  # round half away from zero, ps
    return int(x + F(1, 2)) if x >= 0 else -int(-x + F(1, 2))

res = {}
for std, tgt in targets.items():
    hits = []
    for D in range(1, 21):
        pfd = FIN / D
        if pfd < 10e6 or pfd > 450e6:
            continue
        for m8 in range(2 * 8, 64 * 8 + 1):
            M = F(m8, 8)
            vco = FIN * M / D
            if vco < 600e6 or vco > 1440e6:
                continue
            for Of in range(2, 64):          # fast output, integer divide
                Oc = 2 * Of                  # c64 output, integer divide
                if Oc > 128:
                    continue
                fc = vco / Oc
                ppm = float((fc - tgt) / tgt * 1_000_000)
                if abs(ppm) > 60:
                    continue
                pf_exact = MASTER_PS * D * Of / M
                pc_exact = MASTER_PS * D * Oc / M
                pf, pc = rnd(pf_exact), rnd(pc_exact)
                ff = rnd(pf_exact / 2)       # fast fall edge
                ok = (pc == 2 * pf)
                hits.append((abs(ppm), ppm, D, float(M), Oc, Of, float(vco / 1e6),
                             float(pf_exact), pf, pc, ff, ok, M.denominator != 1))
    hits.sort()
    res[std] = hits
    print(std)
    for h in hits[:14]:
        print("  ppm %+8.2f D=%2d M=%7.3f Oc=%3d Of=%2d VCO=%8.3f fast=%.4fps -> %d, c64 -> %d, fastfall %d, exact2:1=%s fracM=%s"
              % h[1:])
    good = [h for h in hits if h[11]]
    print("  best ps-exact plans:")
    for h in good[:6]:
        print("  ppm %+8.2f D=%2d M=%7.3f Oc=%3d Of=%2d VCO=%8.3f fast=%.4fps -> %d, c64 -> %d, fastfall %d, exact2:1=%s fracM=%s"
              % h[1:])
```

Output:

```text
PAL
  ppm    -5.59 D= 9 M= 56.750 Oc= 30 Of=15 VCO= 945.833 fast=15857.4449ps -> 15857, c64 -> 31715, fastfall 7929, exact2:1=False fracM=True
  best ps-exact plans:
NTSC
  ppm    +0.21 D= 5 M= 24.000 Oc= 22 Of=11 VCO= 720.000 fast=15276.2500ps -> 15276, c64 -> 30553, fastfall 7638, exact2:1=False fracM=False
  ppm    +0.21 D= 5 M= 48.000 Oc= 44 Of=22 VCO=1440.000 fast=15276.2500ps -> 15276, c64 -> 30553, fastfall 7638, exact2:1=False fracM=False
  ppm    +0.21 D=10 M= 48.000 Oc= 22 Of=11 VCO= 720.000 fast=15276.2500ps -> 15276, c64 -> 30553, fastfall 7638, exact2:1=False fracM=False
  ppm    +0.21 D=11 M= 48.000 Oc= 20 Of=10 VCO= 654.545 fast=15276.2500ps -> 15276, c64 -> 30553, fastfall 7638, exact2:1=False fracM=False
  ppm   +46.71 D= 7 M= 48.875 Oc= 32 Of=16 VCO=1047.321 fast=15275.5396ps -> 15276, c64 -> 30551, fastfall 7638, exact2:1=False fracM=True
  ppm   +46.71 D= 8 M= 48.875 Oc= 28 Of=14 VCO= 916.406 fast=15275.5396ps -> 15276, c64 -> 30551, fastfall 7638, exact2:1=False fracM=True
  ppm   -48.02 D= 6 M= 47.125 Oc= 36 Of=18 VCO=1178.125 fast=15276.9867ps -> 15277, c64 -> 30554, fastfall 7638, exact2:1=True fracM=True
  ppm   -48.02 D= 9 M= 47.125 Oc= 24 Of=12 VCO= 785.417 fast=15276.9867ps -> 15277, c64 -> 30554, fastfall 7638, exact2:1=True fracM=True
  best ps-exact plans:
  ppm   -48.02 D= 6 M= 47.125 Oc= 36 Of=18 VCO=1178.125 fast=15276.9867ps -> 15277, c64 -> 30554, fastfall 7638, exact2:1=True fracM=True
  ppm   -48.02 D= 9 M= 47.125 Oc= 24 Of=12 VCO= 785.417 fast=15276.9867ps -> 15277, c64 -> 30554, fastfall 7638, exact2:1=True fracM=True
```


## vicread_model.py

Fast VIC read cost per speed index against the C64U and U64E measurements.

```python
# Scratch model: cost of a fast VIC/SID register read in the CPU island.
# Rule under test: a fast-I/O read enable at fast clock f needs the previous
# T65 enable at f-2 or earlier (one clock to capture the register mux into
# fio_q, then the enable). Local cycles need only a grant.
# Grants: Bresenham, rotated so the first grant is at f=5 (baseline 2.7).
import sys

def grants(N, F=64, off=5, gap=4):
    if N >= F - 1:
        m = [True] * F
        if N == F - 1:
            m[gap % F] = False
        return m
    m = [False] * F
    acc = 0
    for s in range(F):
        acc += N
        if acc >= F:
            acc -= F
            m[s] = True
    first = m.index(True)
    sh = (off - first) % F
    return [m[(s - sh) % F] for s in range(F)]

def run(seq, N, F=64, vgap=2):
    m = grants(N, F)
    t = 5          # next candidate clock
    last = -10     # clock of the last enable
    for c in seq:
        need = 1 if c == 'L' else vgap
        while True:
            if m[t % F] and t - last >= need:
                last = t
                t += 1
                break
            t += 1
    return t

def per_read(N, F=64, vgap=2, cls='V', reps=4000):
    seq = list('LLL' + cls) * reps
    t = run(seq, N, F, vgap)
    return t / reps / F   # phi2 per 4-cycle lda

if __name__ == '__main__':
    print('C64U-like frame (64 fast clocks per phi2)')
    for idx, N in enumerate([1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 47, 63]):
        r = per_read(N, cls='L'); v = per_read(N, cls='V')
        print('idx %2d N=%2d  RAM %.4f  VIC %.4f  ratio %.3f  extra slots/read %.2f'
              % (idx, N, r, v, v / r, (v - r) * N))
    print()
    print('U64E-like frame (48 slots per phi2), 16384 reads, phi2 ticks')
    for N, lab in [(32, 32), (39, 40), (47, 48)]:
        r = per_read(N, F=48, cls='L') * 16384; v = per_read(N, F=48, cls='V') * 16384
        print('label %d N=%d  RAM %.0f  VIC %.0f  ratio %.3f' % (lab, N, r, v, v / r))
    print('measured U64E fw3.15: 32: 2062/2064  40: 1692/2064  48: 1404/1765')
    print('measured C64U idx15: RAM 0.064, $D012 0.080, $D419 0.085')
    print()
    print('read-gap variants (phi2 per 4-cycle lda), C64U frame')
    for vg in (2, 3, 4):
        print('vgap', vg, ' '.join('%d:%.4f' % (N, per_read(N, vgap=vg)) for N in (32, 40, 47, 63)))
    print('RAM   ', ' '.join('%d:%.4f' % (N, per_read(N, cls='L')) for N in (32, 40, 47, 63)))
```

Output:

```text
C64U-like frame (64 fast clocks per phi2)
idx  0 N= 1  RAM 3.9998  VIC 3.9998  ratio 1.000  extra slots/read 0.00
idx  1 N= 2  RAM 1.9999  VIC 1.9999  ratio 1.000  extra slots/read 0.00
idx  2 N= 3  RAM 1.3333  VIC 1.3333  ratio 1.000  extra slots/read 0.00
idx  3 N= 4  RAM 1.0000  VIC 1.0000  ratio 1.000  extra slots/read 0.00
idx  4 N= 6  RAM 0.6666  VIC 0.6666  ratio 1.000  extra slots/read 0.00
idx  5 N= 8  RAM 0.5000  VIC 0.5000  ratio 1.000  extra slots/read 0.00
idx  6 N=10  RAM 0.4000  VIC 0.4000  ratio 1.000  extra slots/read 0.00
idx  7 N=12  RAM 0.3333  VIC 0.3333  ratio 1.000  extra slots/read 0.00
idx  8 N=14  RAM 0.2857  VIC 0.2857  ratio 1.000  extra slots/read 0.00
idx  9 N=16  RAM 0.2500  VIC 0.2500  ratio 1.000  extra slots/read 0.00
idx 10 N=20  RAM 0.2000  VIC 0.2000  ratio 1.000  extra slots/read 0.00
idx 11 N=24  RAM 0.1667  VIC 0.1667  ratio 1.000  extra slots/read 0.00
idx 12 N=32  RAM 0.1250  VIC 0.1250  ratio 1.000  extra slots/read 0.00
idx 13 N=40  RAM 0.1000  VIC 0.1250  ratio 1.250  extra slots/read 1.00
idx 14 N=47  RAM 0.0851  VIC 0.1000  ratio 1.175  extra slots/read 0.70
idx 15 N=63  RAM 0.0635  VIC 0.0790  ratio 1.243  extra slots/read 0.97

U64E-like frame (48 slots per phi2), 16384 reads, phi2 ticks
label 32 N=32  RAM 2048  VIC 2049  ratio 1.000
label 40 N=39  RAM 1681  VIC 1821  ratio 1.083
label 48 N=47  RAM 1395  VIC 1725  ratio 1.237
measured U64E fw3.15: 32: 2062/2064  40: 1692/2064  48: 1404/1765
measured C64U idx15: RAM 0.064, $D012 0.080, $D419 0.085

read-gap variants (phi2 per 4-cycle lda), C64U frame
vgap 2 32:0.1250 40:0.1250 47:0.1000 63:0.0790
vgap 3 32:0.1563 40:0.1250 47:0.1177 63:0.0952
vgap 4 32:0.1563 40:0.1250 47:0.1277 63:0.1111
RAM    32:0.1250 40:0.1000 47:0.0851 63:0.0635
```


## vicpath_model.py

Loop costs with posted writes and barriers, and the #203 pixel pattern.

```python
# Scratch model of the fast VIC register path in the CPU island (design v2).
# Clock t = absolute fast clock; enable "at t" = T65 Enable on the edge that
# ends clock t. c64 edges end odd t (f = 2p+h, e_p ends f = 2p+1).
# Rules modelled:
#  L  local cycle: needs a grant at t.
#  V  fast VIC read: grant at t, no enable at t-1 (fio_q captured at end t-1),
#     and every pending write to the same register done (done visible at C3+2).
#  W  fast VIC write (posted): grant at t, FIFO not full (slot freed at done).
#  A  ACK-class write ($D019/$D01A): posted like W, then no enable of any
#     kind before its C3+2.
# Pop: one entry per c64 edge; C1 = first odd t_c >= push+1 and > last C1;
# landing C2 = C1+2; done C3 = C2+2.
import sys

def grants(N, F=64, off=5, gap=4):
    if N >= F - 1:
        m = [True] * F
        if N == F - 1:
            m[gap % F] = False
        return m
    m = [False] * F
    acc = 0
    for s in range(F):
        acc += N
        if acc >= F:
            acc -= F
            m[s] = True
    first = m.index(True)
    sh = (off - first) % F
    return [m[(s - sh) % F] for s in range(F)]

def run(prog, N, reps, depth=4, raw='precise'):
    """prog: list of (kind, reg) with kind in L V W A; reg is a name or None."""
    m = grants(N)
    t = 5
    last_en = -10
    pending = []          # (reg, ready_time) ready_time = C3+2
    last_c1 = -1
    barrier = -1          # no enable before this clock (ACK)
    for _ in range(reps):
        for kind, reg in prog:
            while True:
                pending = [p for p in pending if p[1] > t]
                ok = m[t % 64] and t >= barrier
                if kind == 'V':
                    ok = ok and (t - last_en >= 2)
                    if raw == 'precise':
                        ok = ok and all(p[0] != reg for p in pending)
                    else:
                        ok = ok and not pending
                if kind in 'WA':
                    ok = ok and len(pending) < depth
                if ok:
                    break
                t += 1
            last_en = t
            if kind in 'WA':
                c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                if c1 <= last_c1:
                    c1 = last_c1 + 2
                last_c1 = c1
                ready = c1 + 4 + 2
                pending.append((reg, ready))
                if kind == 'A':
                    barrier = ready
            t += 1
    return t

def P(s):
    out = []
    for tok in s.split():
        k, _, r = tok.partition(':')
        out.append((k, r or None))
    return out

LOOPS = {
    'unrolled lda $d012':                  'L L L V:d012',
    'unrolled lda $3000 (RAM)':            'L L L L',
    'sta $d020,x / dex / bne':             'L L L V:d0xx W:d020 L L L L L',
    'poll: lda $d012 / cmp # / bne':       'L L L V:d012 L L L L L',
    'stx $d020 then poll $d012 (raster bar)': 'L L L W:d020 L L L V:d012 L L L L L',
    'unrolled inc $d020':                  'L L L V:d020 W:d020 W:d020',
    '#203 lda #0/sta $d020/lda #1/sta $d020': 'L L L L L W:d020 L L L L L W:d020',
    'unrolled sta $d020 / sta $d021':      'L L L W:d020 L L L W:d021',
    'irq ack: sta $d019 / (next instr)':   'L L L A:d019 L L',
}

if __name__ == '__main__':
    reps = 2000
    print('fast clocks per loop iteration (and phi2 per iteration)')
    hdr = ' '.join('%10s' % ('N=%d' % n) for n in (16, 32, 40, 47, 63))
    print('%-42s %s' % ('', hdr))
    for name, s in LOOPS.items():
        prog = P(s)
        cells = []
        for N in (16, 32, 40, 47, 63):
            t = run(prog, N, reps)
            cells.append('%6.2f/%.3f' % (t / reps, t / reps / 64))
        print('%-42s %s' % (name, ' '.join('%10s' % c for c in cells)))
    print()
    print('global RAW barrier variant (all pending writes), N=63:')
    for name in ('stx $d020 then poll $d012 (raster bar)', 'unrolled inc $d020', 'sta $d020,x / dex / bne'):
        a = run(P(LOOPS[name]), 63, reps) / reps
        b = run(P(LOOPS[name]), 63, reps, raw='global') / reps
        print('  %-40s precise %.2f  global %.2f fast clocks' % (name, a, b))
    # #203 pixel pattern at N=47 and 63: colour per pixel (8 fast clocks per pixel)
    print()
    for N in (47, 63):
        m = grants(N)
        t = 5; last = -10; c1last = -1; lands = []
        prog = P(LOOPS['#203 lda #0/sta $d020/lda #1/sta $d020'])
        col = 0
        for _ in range(400):
            for k, r in prog:
                while not m[t % 64]:
                    t += 1
                if k == 'W':
                    c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                    if c1 <= c1last:
                        c1 = c1last + 2
                    c1last = c1
                    lands.append((c1 + 2, col)); col ^= 1
                t += 1
        # pixel edges end clocks t with (t % 8) == 7 (e_3, e_7, ...: every 4th c64 edge)
        pix = []
        li = 0; cur = None
        end = lands[-1][0]
        for te in range(7, end, 8):
            while li < len(lands) and lands[li][0] < te:
                cur = lands[li][1]; li += 1
            pix.append(cur)
        pix = pix[10:]
        runs = []
        r = 1
        for a, b in zip(pix, pix[1:]):
            if a == b:
                r += 1
            else:
                runs.append(r); r = 1
        from collections import Counter
        c = Counter(runs)
        print('N=%d  #203 loop: colour run lengths in pixels -> %s' % (N, dict(sorted(c.items()))))
```

Output:

```text
fast clocks per loop iteration (and phi2 per iteration)
                                                 N=16       N=32       N=40       N=47       N=63
unrolled lda $d012                          16.00/0.250   8.00/0.125   8.00/0.125   6.40/0.100   5.05/0.079
unrolled lda $3000 (RAM)                    16.00/0.250   8.00/0.125   6.40/0.100   5.45/0.085   4.07/0.064
sta $d020,x / dex / bne                     40.00/0.625  20.00/0.313  16.00/0.250  14.94/0.233  11.13/0.174
poll: lda $d012 / cmp # / bne               36.00/0.563  18.00/0.281  16.00/0.250  12.80/0.200  10.11/0.158
stx $d020 then poll $d012 (raster bar)      52.00/0.813  26.00/0.406  21.34/0.333  18.83/0.294  14.22/0.222
unrolled inc $d020                          24.00/0.375  12.00/0.188  12.00/0.188  11.30/0.176  10.11/0.158
#203 lda #0/sta $d020/lda #1/sta $d020      48.00/0.750  24.00/0.375  19.20/0.300  16.34/0.255  12.19/0.191
unrolled sta $d020 / sta $d021              32.00/0.500  16.00/0.250  12.80/0.200  10.90/0.170   8.13/0.127
irq ack: sta $d019 / (next instr)           28.00/0.438  18.00/0.281  16.00/0.250  14.22/0.222  12.19/0.191

global RAW barrier variant (all pending writes), N=63:
  stx $d020 then poll $d012 (raster bar)   precise 14.22  global 16.01 fast clocks
  unrolled inc $d020                       precise 10.11  global 10.11 fast clocks
  sta $d020,x / dex / bne                  precise 11.13  global 11.13 fast clocks

N=47  #203 loop: colour run lengths in pixels -> {1: 772, 2: 17}
N=63  #203 loop: colour run lengths in pixels -> {1: 224, 2: 187}
```


## sid_fio_model.py

SID cost under the rejected non-posted variant, kept for the record.

```python
# Fast SID path cost model (design stage, [I]).
# Fast clock f = 0..63 per native cycle; c64 edges at end of odd f (h = f & 1 == 1).
# Grant table: N Bresenham-spaced ones per 64, rotated so the first one is at f = 5.
# A T65 enable needs a granted fast edge. Local cycle: enable at first grant >= g+1.
# FIO cycle (request registered at end f_a = g+1):
#   V-R: completion edge = f_a + 2 + h(f_a)  (c64 executes at first c64 edge after f_a)
#   V-C: completion edge = f_a + 1 + (1 - h(f_a))  (c64 captures T65 address at first c64 edge >= f_a)
# Then enable at the first grant >= completion edge.
import sys

def table(n):
    if n >= 64:
        return [1] * 64
    row = [0] * 64
    acc = 0
    ones = []
    for i in range(64):
        acc += n
        if acc >= 64:
            acc -= 64
            ones.append(i)
    # rotate so the first one is at f = 5
    shift = 5 - ones[0]
    for o in ones:
        row[(o + shift) % 64] = 1
    return row

def run(n, pattern, variant, cycles=20000):
    """pattern: list of 'L'/'F' per CPU bus cycle, repeated. Returns fast clocks per pattern."""
    row = table(n)
    g = 4  # last enable at end of f=4 (absolute fast clock index)
    def granted(t):
        return row[t % 64] == 1
    def next_grant(t):
        while not granted(t):
            t += 1
        return t
    start = g
    reps = 0
    k = 0
    while k < cycles:
        for c in pattern:
            fa = g + 1
            if c == 'L':
                g = next_grant(fa)
            else:
                h = fa & 1
                if variant == 'R':
                    comp = fa + 2 + h
                else:
                    comp = fa + 1 + (1 - h)
                g = next_grant(comp)
            k += 1
        reps += 1
    return (g - start) / reps

speeds = [(1, 2), (2, 3), (3, 4), (4, 6), (5, 8), (6, 10), (7, 12), (8, 14), (9, 16),
          (10, 20), (11, 24), (12, 32), (13, 40), (14, 47), (15, 63)]
lda_ram = ['L'] * 4
lda_io = ['L'] * 3 + ['F']
print("idx N   RAM(phi2/read)  D419 V-R   V-C    | extra slots V-R V-C")
for idx, n in speeds:
    r = run(n, lda_ram, 'R') / 64
    vr = run(n, lda_io, 'R') / 64
    vc = run(n, lda_io, 'C') / 64
    print(f"{idx:3d} {n:2d}  {r:.4f}          {vr:.4f}     {vc:.4f} | {(vr-r)*n:.2f} {(vc-r)*n:.2f}")

# sidBurst from Doom irqtest: ldx #0; 25 x (lda abs,x / sta abs,x / inx / cpx / bne)
# per register: lda abs,x 4L ; sta abs,x 3L + F(dummy read) + F(write) ; inx 2L ; cpx 2L ; bne 3L
burst = []
for i in range(25):
    burst += ['L'] * 4 + ['L'] * 3 + ['F', 'F'] + ['L'] * 2 + ['L'] * 2 + ['L'] * 3
for idx, n in [(12, 32), (14, 47), (15, 63)]:
    for v in 'RC':
        t = run(n, burst, v, cycles=len(burst) * 50)
        print(f"sidBurst idx {idx} V-{v}: {t:.0f} fast clocks = {t/64:.2f} phi2 (us)")
    # all-local reference
    t = run(n, ['L'] * len(burst), 'R', cycles=len(burst) * 50)
    print(f"sidBurst idx {idx} if FIO cost 1 slot: {t/64:.2f} us")
```

Output:

```text
idx N   RAM(phi2/read)  D419 V-R   V-C    | extra slots V-R V-C
  1  2  1.9999          1.9999     1.9999 | 0.00 0.00
  2  3  1.3333          1.3333     1.3333 | 0.00 0.00
  3  4  1.0000          1.0000     1.0000 | 0.00 0.00
  4  6  0.6666          0.6666     0.6666 | 0.00 0.00
  5  8  0.5000          0.5000     0.5000 | 0.00 0.00
  6 10  0.4000          0.4000     0.4000 | 0.00 0.00
  7 12  0.3333          0.3333     0.3333 | 0.00 0.00
  8 14  0.2857          0.2857     0.2857 | 0.00 0.00
  9 16  0.2500          0.2500     0.2500 | 0.00 0.00
 10 20  0.2000          0.2500     0.2000 | 1.00 0.00
 11 24  0.1667          0.1875     0.1667 | 0.50 0.00
 12 32  0.1250          0.1562     0.1562 | 1.00 1.00
 13 40  0.1000          0.1250     0.1250 | 1.00 1.00
 14 47  0.0851          0.1250     0.1111 | 1.88 1.22
 15 63  0.0635          0.0952     0.0938 | 2.00 1.91
sidBurst idx 12 V-R: 900 fast clocks = 14.06 phi2 (us)
sidBurst idx 12 V-C: 900 fast clocks = 14.06 phi2 (us)
sidBurst idx 12 if FIO cost 1 slot: 12.50 us
sidBurst idx 14 V-R: 659 fast clocks = 10.29 phi2 (us)
sidBurst idx 14 V-C: 615 fast clocks = 9.62 phi2 (us)
sidBurst idx 14 if FIO cost 1 slot: 8.51 us
sidBurst idx 15 V-R: 550 fast clocks = 8.59 phi2 (us)
sidBurst idx 15 V-C: 464 fast clocks = 7.26 phi2 (us)
sidBurst idx 15 if FIO cost 1 slot: 6.35 us
```


## lead_model.py

Costs of the merged fast I/O path: write barrier, fetch-ordered local writes, shared VIC/SID FIFO.

```python
# Lead-architect cost model for FINAL_DESIGN_v2 (design stage, [I]).
# Unified fast I/O (FIO) port: VIC and SID share the 2-clock read and the
# posted 4-entry FIFO. Rules (fast clock t; c64 edges end odd t):
#  L  local read/opcode: grant at t
#  M  local RAM/colour write: grant at t; with O3, every VIC entry done
#  V  FIO read (VIC or SID, incl. dummy reads): grant, no enable at t-1,
#     global RAW barrier (no pending FIO entry of any target)
#  W  FIO write (VIC):  grant, FIFO not full; tagged VIC (O3)
#  S  FIO write (SID):  grant, FIFO not full; not tagged VIC
#  A  ACK-class write ($D019/$D01A): as W, then no enable before done+ACK
# Pop: one entry per c64 edge. C1 = first odd t >= push+1 and > last C1;
# C2 = C1+2 (landing); C3 = C2+2 (done); island may use done at C3+2.
# ACK entries: done at C2 + 2*FIO_ACK_EDGES.

def grants(N, F=64, off=5, gap=4):
    if N >= F - 1:
        m = [True] * F
        if N == F - 1:
            m[gap % F] = False
        return m
    m = [False] * F
    acc = 0
    for s in range(F):
        acc += N
        if acc >= F:
            acc -= F
            m[s] = True
    first = m.index(True)
    sh = (off - first) % F
    return [m[(s - sh) % F] for s in range(F)]


def run(prog, N, reps, depth=4, o3=True, ack_edges=2):
    m = grants(N)
    t = 5
    last_en = -10
    pend = []            # (is_vic, ready)
    last_c1 = -1
    barrier = -1
    for _ in range(reps):
        for k in prog:
            while True:
                pend = [p for p in pend if p[1] > t]
                ok = m[t % 64] and t >= barrier
                if k == 'V':
                    ok = ok and (t - last_en >= 2) and not pend
                elif k in 'WSA':
                    ok = ok and len(pend) < depth
                elif k == 'M' and o3:
                    ok = ok and not any(p[0] for p in pend)
                if ok:
                    break
                t += 1
            last_en = t
            if k in 'WSA':
                c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                if c1 <= last_c1:
                    c1 = last_c1 + 2
                last_c1 = c1
                c2 = c1 + 2
                done = c2 + (2 * ack_edges if k == 'A' else 2)
                ready = done + 2
                pend.append((k != 'S', ready))
                if k == 'A':
                    barrier = ready
            t += 1
    return t / reps


LOOPS = {
    'unrolled lda $d012 / lda $d419':      'LLLV',
    'unrolled lda $3000 (RAM)':            'LLLL',
    'sta $d020,x / dex / bne':             'LLLVWLLLLL',
    'lda $d012 / cmp # / bne':             'LLLVLLLLL',
    'stx $d020 then poll $d012':           'LLLWLLLVLLLLL',
    'unrolled inc $d020':                  'LLLVWW',
    'unrolled sta $d020 / sta $0400 (O3)': 'LLLWLLLM',
    'same, O3 off':                        None,
    'sta $d019 + 2-cycle instr (ACK=2)':   'LLLALL',
    'sta $d019 + 2-cycle instr (ACK=1)':   None,
    'Doom sidBurst iteration':             'LLLLLLLVSLLLLLLL',
}

if __name__ == '__main__':
    reps = 3000
    Ns = (6, 16, 32, 40, 47, 63)
    print('%-38s ' % '' + ' '.join('%13s' % ('N=%d' % n) for n in Ns))
    for name, s in LOOPS.items():
        kw = {}
        if s is None:
            if name.startswith('same'):
                s = LOOPS['unrolled sta $d020 / sta $0400 (O3)']; kw = {'o3': False}
            else:
                s = LOOPS['sta $d019 + 2-cycle instr (ACK=2)']; kw = {'ack_edges': 1}
        cells = []
        for N in Ns:
            fc = run(s, N, reps, **kw)
            cells.append('%6.2f/%.3f' % (fc, fc / 64))
        print('%-38s ' % name + ' '.join('%13s' % c for c in cells))
    print()
    for std, tf in (('PAL', 1e3 / 63.0556), ('NTSC', 1e3 / 65.4545)):
        for N in (32, 47, 63):
            fc = run(LOOPS['Doom sidBurst iteration'], N, reps)
            print('%s Doom 25-register burst, N=%d: %.2f us' % (std, N, 25 * fc * tf / 1000))
```

Output:

```text
                                                 N=6          N=16          N=32          N=40          N=47          N=63
unrolled lda $d012 / lda $d419           42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    6.40/0.100    5.05/0.079
unrolled lda $3000 (RAM)                 42.66/0.667   16.00/0.250    8.00/0.125    6.40/0.100    5.45/0.085    4.07/0.064
sta $d020,x / dex / bne                 106.67/1.667   40.00/0.625   20.00/0.313   16.00/0.250   14.93/0.233   11.13/0.174
lda $d012 / cmp # / bne                  96.00/1.500   36.00/0.563   18.00/0.281   16.00/0.250   12.80/0.200   10.11/0.158
stx $d020 then poll $d012               138.66/2.167   52.00/0.813   26.00/0.406   22.00/0.344   20.00/0.313   16.00/0.250
unrolled inc $d020                       64.00/1.000   24.00/0.375   12.00/0.188   12.00/0.188   11.30/0.176   10.11/0.158
unrolled sta $d020 / sta $0400 (O3)      85.33/1.333   32.00/0.500   16.00/0.250   14.00/0.219   12.80/0.200   12.00/0.188
same, O3 off                             85.33/1.333   32.00/0.500   16.00/0.250   12.80/0.200   10.89/0.170    8.13/0.127
sta $d019 + 2-cycle instr (ACK=2)        64.00/1.000   32.00/0.500   20.00/0.313   18.00/0.281   16.00/0.250   14.22/0.222
sta $d019 + 2-cycle instr (ACK=1)        64.00/1.000   28.00/0.438   18.00/0.281   16.00/0.250   14.22/0.222   12.19/0.190
Doom sidBurst iteration                 170.66/2.667   64.00/1.000   32.00/0.500   26.67/0.417   22.59/0.353   17.23/0.269

PAL Doom 25-register burst, N=32: 12.69 us
PAL Doom 25-register burst, N=47: 8.96 us
PAL Doom 25-register burst, N=63: 6.83 us
NTSC Doom 25-register burst, N=32: 12.22 us
NTSC Doom 25-register burst, N=47: 8.63 us
NTSC Doom 25-register burst, N=63: 6.58 us
```


## final_model.py

Final barrier rules: side-effect reads, selective clears, φ2 forwards, the 26-cycle Doom `miPlay` loop. Loads `lead_model.py`.

```python
# Cost model for the finalized FINAL_DESIGN_v2 (design stage, [I]).
# Extends lead_model.py with the barrier rules adopted after review:
#  - every barrier flag is registered from the post-push state (no stale clock);
#  - FIO reads wait for queued WR entries only, plus side-effect entries that can
#    change the value read (CLR of the same register, LATCH for write-only SID
#    registers); a read that pushes needs a free slot; a slot frees when done;
#  - VIC $1E/$1F reads push a CLR entry only when the returned byte is nonzero.
# Fast clock t; c64 edges end odd t; pop one entry per c64 edge.
#
# Access kinds:
#  L local read/opcode            M local write (O3: waits for fetch-affecting VIC entries)
#  V FIO read, no side effect     T SID $19-$1C read (pushes LATCH)
#  C VIC $1E/$1F read, nonzero data (waits for same-kind CLR, pushes CLR)
#  O SID write-only register read (waits for LATCH)
#  W VIC write, not fetch-affecting     Y VIC write, fetch-affecting (O3)
#  S SID write                          A ack-class write ($D019/$D01A)
#  F phi2 forward (CIA, I/O1/I/O2): needs all entries done; cutoff f=30; resume f=4
import importlib.util, os
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('lm', os.path.join(here, 'lead_model.py'))
lm = importlib.util.module_from_spec(spec); spec.loader.exec_module(lm)
grants = lm.grants


def run(prog, N, reps, depth=4, ack_edges=2, start=5, trace=None):
    m = grants(N)
    t = start
    last_en = -10
    pend = []          # dicts: kind, ready (first t an enable may depend on it), acc (accept edge)
    last_c1 = -1
    barrier = -1
    for _ in range(reps):
        for k in prog:
            if k == 'F':
                while any(p['ready'] > t for p in pend):
                    t += 1
                f = t % 64
                frame = t - f
                en = frame + (64 if f <= 30 else 128) + 4
                t = en
                last_en = t
                t += 1
                continue
            while True:
                pend = [p for p in pend if p['ready'] > t]
                occ = len(pend)   # a slot is free only once its entry is done (full_q from fio_done)
                ok = m[t % 64] and t >= barrier
                wr_pend = any(p['kind'] in 'WYSA' for p in pend)
                if k in 'VTCO':
                    ok = ok and (t - last_en >= 2) and not wr_pend
                    if k == 'C':
                        ok = ok and not any(p['kind'] == 'C' for p in pend)
                    if k == 'O':
                        ok = ok and not any(p['kind'] == 'T' for p in pend)
                    if k in 'TC':
                        ok = ok and occ < depth
                elif k in 'WYSA':
                    ok = ok and occ < depth
                elif k == 'M':
                    ok = ok and not any(p['kind'] == 'Y' for p in pend)
                if ok:
                    break
                t += 1
            last_en = t
            if trace is not None:
                trace.append((k, t))
            if k in 'WYSATC':
                c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                if c1 <= last_c1:
                    c1 = last_c1 + 2
                last_c1 = c1
                c2 = c1 + 2
                done = c2 + (2 * ack_edges if k == 'A' else 2)
                ready = done + 2
                pend.append({'kind': k, 'ready': ready, 'acc': c1, 'c2': c2})
                if k == 'A':
                    barrier = ready
            t += 1
    return (t - start) / reps


LOOPS = {
    'unrolled lda $d012':                    'LLLV',
    'unrolled lda $d419 / $d41a (LATCH)':    'LLLT',
    'unrolled lda $d01e, collision present': 'LLLC',
    'unrolled lda $d01e, no collision':      'LLLV',
    'unrolled lda $d41d after lda $d41b':    'LLLTLLLO',
    'unrolled lda $3000 (RAM)':              'LLLL',
    'sta $d020,x / dex / bne':               'LLLVWLLLLL',
    'lda $d012 / cmp # / bne':               'LLLVLLLLL',
    'stx $d020 then poll $d012':             'LLLWLLLVLLLLL',
    'unrolled inc $d020':                    'LLLVWW',
    'sta $d019 + 2-cycle instr':             'LLLALL',
    'Doom burst, 16-cycle (v2 figure)':      'LLLLLLLVSLLLLLLL',
    'Doom miPlay loop (26 cycles)':          'LLLL' 'LL' 'LLLL' 'LL' 'LLLVS' 'LLLLMM' 'LLL',
    'Doom miPlay loop, phi2 fallback':       'LLLL' 'LL' 'LLLL' 'LL' 'LLLFF' 'LLLLMM' 'LLL',
}

if __name__ == '__main__':
    reps = 2000
    Ns = (6, 16, 32, 40, 47, 63)
    print('%-40s ' % 'fast clocks / phi2 per iteration' + ' '.join('%13s' % ('N=%d' % n) for n in Ns))
    for name, s in LOOPS.items():
        cells = []
        for N in Ns:
            fc = run(s, N, reps)
            cells.append('%6.2f/%.3f' % (fc, fc / 64))
        print('%-40s ' % name + ' '.join('%13s' % c for c in cells))
    print()
    for depth in (1, 2, 4):
        print('depth %d: unrolled inc $d020 N=63 %.2f fc; lda $d419 %.2f fc'
              % (depth, run('LLLVWW', 63, reps, depth=depth), run('LLLT', 63, reps, depth=depth)))
    print()
    for std, tf in (('PAL', 1e3 / 63.0556), ('NTSC', 1e3 / 65.4545)):
        for N in (6, 16, 32, 47, 63):
            fc = run(LOOPS['Doom miPlay loop (26 cycles)'], N, reps)
            fb = run(LOOPS['Doom miPlay loop, phi2 fallback'], N, reps)
            print('%s 25-register miPlay burst, N=%2d: %.2f us (phi2 fallback %.1f us); x1970/s = %.2f%%'
                  % (std, N, 25 * fc * tf / 1000, 25 * fb * tf / 1000, 25 * fc * tf / 1000 * 1970 / 1e4))
```

Output:

```text
fast clocks / phi2 per iteration                   N=6          N=16          N=32          N=40          N=47          N=63
unrolled lda $d012                         42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    6.40/0.100    5.05/0.079
unrolled lda $d419 / $d41a (LATCH)         42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    6.40/0.100    5.05/0.079
unrolled lda $d01e, collision present      42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    8.00/0.125    8.00/0.125
unrolled lda $d01e, no collision           42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    6.40/0.100    5.05/0.079
unrolled lda $d41d after lda $d41b         85.33/1.333   32.00/0.500   16.00/0.250   16.00/0.250   14.22/0.222   12.19/0.190
unrolled lda $3000 (RAM)                   42.66/0.667   16.00/0.250    8.00/0.125    6.40/0.100    5.45/0.085    4.06/0.063
sta $d020,x / dex / bne                   106.66/1.667   40.00/0.625   20.00/0.312   16.00/0.250   14.93/0.233   11.13/0.174
lda $d012 / cmp # / bne                    96.00/1.500   36.00/0.562   18.00/0.281   16.00/0.250   12.80/0.200   10.11/0.158
stx $d020 then poll $d012                 138.66/2.167   52.00/0.812   26.00/0.406   22.00/0.344   20.00/0.312   16.00/0.250
unrolled inc $d020                         63.99/1.000   24.00/0.375   12.00/0.187   12.00/0.187   11.29/0.176   10.10/0.158
sta $d019 + 2-cycle instr                  63.99/1.000   32.00/0.500   20.00/0.312   18.00/0.281   16.00/0.250   14.22/0.222
Doom burst, 16-cycle (v2 figure)          170.66/2.667   64.00/1.000   32.00/0.500   26.67/0.417   22.59/0.353   17.23/0.269
Doom miPlay loop (26 cycles)              277.33/4.333  104.00/1.625   52.00/0.812   42.67/0.667   36.27/0.567   27.43/0.429
Doom miPlay loop, phi2 fallback           383.98/6.000  255.98/4.000  192.01/3.000  191.97/3.000  191.97/3.000  128.00/2.000

depth 1: unrolled inc $d020 N=63 16.00 fc; lda $d419 8.00 fc
depth 2: unrolled inc $d020 N=63 10.10 fc; lda $d419 5.05 fc
depth 4: unrolled inc $d020 N=63 10.10 fc; lda $d419 5.05 fc

PAL 25-register miPlay burst, N= 6: 109.95 us (phi2 fallback 152.2 us); x1970/s = 21.66%
PAL 25-register miPlay burst, N=16: 41.23 us (phi2 fallback 101.5 us); x1970/s = 8.12%
PAL 25-register miPlay burst, N=32: 20.62 us (phi2 fallback 76.1 us); x1970/s = 4.06%
PAL 25-register miPlay burst, N=47: 14.38 us (phi2 fallback 76.1 us); x1970/s = 2.83%
PAL 25-register miPlay burst, N=63: 10.87 us (phi2 fallback 50.8 us); x1970/s = 2.14%
NTSC 25-register miPlay burst, N= 6: 105.92 us (phi2 fallback 146.7 us); x1970/s = 20.87%
NTSC 25-register miPlay burst, N=16: 39.72 us (phi2 fallback 97.8 us); x1970/s = 7.83%
NTSC 25-register miPlay burst, N=32: 19.86 us (phi2 fallback 73.3 us); x1970/s = 3.91%
NTSC 25-register miPlay burst, N=47: 13.85 us (phi2 fallback 73.3 us); x1970/s = 2.73%
NTSC 25-register miPlay burst, N=63: 10.48 us (phi2 fallback 48.9 us); x1970/s = 2.06%
```


## top_rows_model.py

The `final_model.py` and `final.py` rules at 48 and 64 slots, with the C64U rows 47 and 63 for comparison. Loads `final_model.py`, `vicpath_model.py` and (through `final_model.py`) `lead_model.py`.

```python
# Cost model for the C64 turbo design (docs/C64_TURBO_DESIGN.md): full top rows
# (index 14 -> N=48, index 15 -> N=64),
# with the C64U rows (47, 63) kept for comparison (the hidden c64u_top bit).
# Design stage, [I]. Reuses these models unchanged (see docs/C64_TURBO_MODELS.md):
#  - final_model.py (fast VIC/SID FIO rules, barriers, posted FIFO, forwards);
#  - vicpath_model.py (#203 pixel pattern);
#  - the forward/Amdahl rules of final.py and perf.py (cutoff f=30,
#    resume f=4 -> next cycle ready at f=5, rotated grants, gap at f=4 only for N=63).
# Grants: N >= 63 -> all 64 slots, minus f=4 when N == 63 (lead_model.grants).
import importlib.util
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
# Sibling models are looked up in this script's own folder first (the layout of
# docs/C64_TURBO_MODELS.md), then in ../v2 (the design-stage layout).
SEARCH = (HERE, os.path.join(HERE, '..', 'v2'))


def find(name):
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


fm = load('final_model', find('final_model.py'))
vp = load('vicpath_model', find('vicpath_model.py'))
grants = fm.grants

TABLE_C64U = [1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 47, 63]
TABLE_V3 = [1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64]


def fwd_run(seq, N, CUT=30, RES=5):
    """final.py rules: L = local slot, I = phi2 forward."""
    m = grants(N)
    t = RES
    for c in seq:
        if c == 'L':
            while not m[t % 64]:
                t += 1
            t += 1
        else:
            f = t % 64
            b = t - f
            t = (b + 64 if f <= CUT else b + 128) + RES
    return t


FWD_LOOPS = {
    'RAM lda abs,x/dex/bne': 'LLLLLLLLL',
    'lda $dc01,x/dex/bne': 'LLLILLLLL',
    'sta $de00,x/dex/bne': 'LLLIILLLLL',
    'unrolled lda $de80': 'LLLI',
    'fill $0400,x+$d800,x colour fwd': 'LLLLLLLLII' + 'LLLLL',
    'fill colour local (dummy read 2 slots)': 'LLLLL' + 'LLLLL' + 'LL' + 'LLL',
}


def main():
    Ns = (6, 16, 32, 40, 47, 48, 63, 64)
    print('== forward-class loops: phi2 (native cycles) per iteration ==')
    for k, v in FWD_LOOPS.items():
        row = []
        for N in Ns:
            s = list(v) * 300
            t = fwd_run(s, N)
            per = t / 300 / 64
            row.append('%d:%.3f' % (N, per))
        print('%-40s %s' % (k, ' '.join(row)))
    # colour-local fill has a 2-slot dummy read: model it as one extra local slot
    print('fill colour local, speedup vs 15 stock cycles (2-slot dummy read = 16 slots):')
    for N in Ns:
        s = list('L' * 16) * 300
        t = fwd_run(s, N)
        print('  N=%d %.1fx' % (N, 15 * 300 * 64 / t))
    print('RAM loop speedup (9-cycle loop):', ' '.join(
        'N=%d %.2fx' % (N, 9 * 300 * 64 / fwd_run(list('L' * 9) * 300, N)) for N in Ns))

    print()
    print('== FIO loops (final_model.py rules): fast clocks / phi2 per iteration ==')
    reps = 2000
    for name, s in fm.LOOPS.items():
        cells = []
        for N in Ns:
            fc = fm.run(s, N, reps)
            cells.append('%d:%.2f/%.4f' % (N, fc, fc / 64))
        print('%-40s %s' % (name, ' '.join(cells)))
    print()
    for N in (47, 48, 63, 64):
        ram = fm.run('LLLL', N, reps) / 64
        io = fm.run('LLLV', N, reps) / 64
        print('N=%d unrolled RAM %.4f  VIC/SID %.4f  extra slots per read %.2f' % (
            N, ram, io, (io - ram) * N))
    print()
    for std, tf in (('PAL', 1e3 / 63.0556), ('NTSC', 1e3 / 65.4545)):
        for N in (47, 48, 63, 64):
            fc = fm.run(fm.LOOPS['Doom miPlay loop (26 cycles)'], N, reps)
            f16 = fm.run(fm.LOOPS['Doom burst, 16-cycle (v2 figure)'], N, reps)
            fb = fm.run(fm.LOOPS['Doom miPlay loop, phi2 fallback'], N, reps)
            print('%s N=%d miPlay burst %.2f us (16-cycle %.2f us; phi2 fallback %.1f us); '
                  'x1970/s = %.2f%% (16-cycle %.2f%%)' % (
                      std, N, 25 * fc * tf / 1000, 25 * f16 * tf / 1000, 25 * fb * tf / 1000,
                      25 * fc * tf / 1000 * 1970 / 1e4, 25 * f16 * tf / 1000 * 1970 / 1e4))
    print()
    print('micro-costs at N=64 vs 63 (fast clocks):')
    for N in (63, 64):
        print('  N=%d sta $d019+2cyc %.2f (ack_edges=1: %.2f); raster bar %.2f; '
              'lda $d41b/$d41d pair %.2f' % (
                  N, fm.run('LLLALL', N, reps), fm.run('LLLALL', N, reps, ack_edges=1),
                  fm.run('LLLWLLLVLLLLL', N, reps), fm.run('LLLTLLLO', N, reps)))
        print('  N=%d sta $d018/sta $0400 unrolled (O3) %.2f; colour reg instead %.2f' % (
            N, fm.run('LLLY' 'LLLM', N, reps) / 1, fm.run('LLLW' 'LLLM', N, reps)))

    print()
    print('raster bar with FIO_RAW_PRECISE (vicpath_model.py precise rule) vs global barrier:')
    for N in (47, 48, 63, 64):
        prog = vp.P(vp.LOOPS['stx $d020 then poll $d012 (raster bar)'])
        a = vp.run(prog, N, reps) / reps
        b = vp.run(prog, N, reps, raw='global') / reps
        c = fm.run('LLLVLLLV', N, reps)
        print('  N=%d precise %.2f global %.2f fast clocks; two plain FIO reads %.2f' % (N, a, b, c))

    print()
    print('== random phi2 I/O share (Amdahl; final.py rules, seed 3) ==')
    random.seed(3)
    for p in (0.1, 0.5, 1, 2, 5, 10):
        n = 200000
        s = ['I' if random.random() * 100 < p else 'L' for _ in range(n)]
        print('I/O %4.1f%%: ' % p + '  '.join(
            'N=%d %.1fx' % (N, n * 64 / fwd_run(s, N)) for N in (64, 63, 48, 47, 16)))

    print()
    print('== beyond 64x, base N=64 (perf.py method) ==')
    for p in (0.5, 1, 2, 5):
        n = 200000
        random.seed(11)
        s = ['I' if random.random() * 100 < p else 'L' for _ in range(n)]
        t64 = fwd_run(s, 64) / 64
        base = n / t64
        loc = sum(1 for c in s if c == 'L')
        io_cost = t64 - loc / 64.0

        def eff(rx):
            return n / (loc / rx + io_cost)
        print('I/O %.1f%%: 64x->%.1f  80x->%.1f  96x->%.1f' % (p, base, eff(80), eff(96)))

    print()
    print('== badline timing enabled, screen on (no sprites) ==')
    for name, lines, cyc in (('PAL', 312, 63), ('NTSC', 263, 65)):
        frac = 25 * 43 / (lines * cyc)
        print('%s BA-low %.4f: N=63 %.2fx  N=64 %.2fx  N=47 %.2fx  N=48 %.2fx' % (
            name, frac, 63 * (1 - frac), 64 * (1 - frac), 47 * (1 - frac), 48 * (1 - frac)))

    print()
    print('== #203 pattern (vicpath_model.py rules) ==')
    from collections import Counter
    for N in (47, 48, 63, 64):
        m = grants(N)
        t = 5
        c1last = -1
        lands = []
        prog = vp.P(vp.LOOPS['#203 lda #0/sta $d020/lda #1/sta $d020'])
        col = 0
        for _ in range(400):
            for k, r in prog:
                while not m[t % 64]:
                    t += 1
                if k == 'W':
                    c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                    if c1 <= c1last:
                        c1 = c1last + 2
                    c1last = c1
                    lands.append((c1 + 2, col))
                    col ^= 1
                t += 1
        pix = []
        li = 0
        cur = None
        end = lands[-1][0]
        for te in range(7, end, 8):
            while li < len(lands) and lands[li][0] < te:
                cur = lands[li][1]
                li += 1
            pix.append(cur)
        pix = pix[10:]
        runs = []
        r = 1
        for a, b in zip(pix, pix[1:]):
            if a == b:
                r += 1
            else:
                runs.append(r)
                r = 1
        c = Counter(runs)
        print('N=%d #203 colour run lengths (pixels) -> %s; colour lasts %.3f pixel' % (
            N, dict(sorted(c.items())), 6 * 8 / (N / 8 * 8)))

    print()
    print('== xahmol speed_probe scaling (16 lines at 63x, about 21-22 at 47x, PAL [M]) ==')
    for N in (47, 48, 63, 64):
        print('N=%d: about %.1f lines (16 x 63 / N)' % (N, 16 * 63 / N))


if __name__ == '__main__':
    main()
```

Output:

```text
== forward-class loops: phi2 (native cycles) per iteration ==
RAM lda abs,x/dex/bne                    6:1.500 16:0.563 32:0.281 40:0.225 47:0.192 48:0.188 63:0.143 64:0.141
lda $dc01,x/dex/bne                      6:1.999 16:1.998 32:1.001 40:1.001 47:1.001 48:1.001 63:1.001 64:1.001
sta $de00,x/dex/bne                      6:2.999 16:2.998 32:2.001 40:2.001 47:2.001 48:2.001 63:2.001 64:2.001
unrolled lda $de80                       6:1.000 16:1.000 32:1.000 40:1.000 47:1.000 48:1.000 63:1.000 64:1.000
fill $0400,x+$d800,x colour fwd          6:3.999 16:3.001 32:2.001 40:2.001 47:2.001 48:2.001 63:2.001 64:2.001
fill colour local (dummy read 2 slots)   6:2.500 16:0.938 32:0.469 40:0.375 47:0.319 48:0.313 63:0.238 64:0.235
fill colour local, speedup vs 15 stock cycles (2-slot dummy read = 16 slots):
  N=6 5.6x
  N=16 15.0x
  N=32 30.0x
  N=40 37.5x
  N=47 44.0x
  N=48 45.0x
  N=63 59.0x
  N=64 59.9x
RAM loop speedup (9-cycle loop): N=6 6.00x N=16 16.00x N=32 31.98x N=40 39.96x N=47 46.94x N=48 47.95x N=63 62.90x N=64 63.88x

== FIO loops (final_model.py rules): fast clocks / phi2 per iteration ==
unrolled lda $d012                       6:42.66/0.6666 16:16.00/0.2500 32:8.00/0.1250 40:8.00/0.1250 47:6.40/0.1000 48:6.00/0.0937 63:5.05/0.0789 64:5.00/0.0781
unrolled lda $d419 / $d41a (LATCH)       6:42.66/0.6666 16:16.00/0.2500 32:8.00/0.1250 40:8.00/0.1250 47:6.40/0.1000 48:6.00/0.0937 63:5.05/0.0789 64:5.00/0.0781
unrolled lda $d01e, collision present    6:42.66/0.6666 16:16.00/0.2500 32:8.00/0.1250 40:8.00/0.1250 47:8.00/0.1250 48:8.00/0.1250 63:8.00/0.1250 64:8.00/0.1250
unrolled lda $d01e, no collision         6:42.66/0.6666 16:16.00/0.2500 32:8.00/0.1250 40:8.00/0.1250 47:6.40/0.1000 48:6.00/0.0937 63:5.05/0.0789 64:5.00/0.0781
unrolled lda $d41d after lda $d41b       6:85.33/1.3333 16:32.00/0.5000 32:16.00/0.2500 40:16.00/0.2500 47:14.22/0.2222 48:14.00/0.2187 63:12.19/0.1905 64:12.00/0.1875
unrolled lda $3000 (RAM)                 6:42.66/0.6666 16:16.00/0.2500 32:8.00/0.1250 40:6.40/0.1000 47:5.45/0.0851 48:5.33/0.0833 63:4.06/0.0635 64:4.00/0.0625
sta $d020,x / dex / bne                  6:106.66/1.6666 16:40.00/0.6250 32:20.00/0.3125 40:16.00/0.2500 47:14.93/0.2333 48:14.00/0.2187 63:11.13/0.1739 64:11.00/0.1719
lda $d012 / cmp # / bne                  6:96.00/1.4999 16:36.00/0.5625 32:18.00/0.2812 40:16.00/0.2500 47:12.80/0.2000 48:12.00/0.1875 63:10.11/0.1579 64:10.00/0.1562
stx $d020 then poll $d012                6:138.66/2.1666 16:52.00/0.8125 32:26.00/0.4062 40:22.00/0.3437 47:20.00/0.3125 48:20.00/0.3125 63:16.00/0.2500 64:16.00/0.2500
unrolled inc $d020                       6:63.99/0.9999 16:24.00/0.3750 32:12.00/0.1875 40:12.00/0.1875 47:11.29/0.1765 48:12.00/0.1875 63:10.10/0.1579 64:10.00/0.1562
sta $d019 + 2-cycle instr                6:63.99/0.9999 16:32.00/0.5000 32:20.00/0.3125 40:18.00/0.2812 47:16.00/0.2500 48:16.00/0.2500 63:14.22/0.2222 64:14.00/0.2188
Doom burst, 16-cycle (v2 figure)         6:170.66/2.6666 16:64.00/1.0000 32:32.00/0.5000 40:26.67/0.4167 47:22.59/0.3529 48:22.00/0.3437 63:17.23/0.2692 64:17.00/0.2656
Doom miPlay loop (26 cycles)             6:277.33/4.3333 16:104.00/1.6250 32:52.00/0.8125 40:42.67/0.6666 47:36.27/0.5667 48:36.00/0.5625 63:27.43/0.4286 64:27.00/0.4219
Doom miPlay loop, phi2 fallback          6:383.98/5.9997 16:255.98/3.9998 32:192.01/3.0001 40:191.97/2.9996 47:191.97/2.9996 48:191.97/2.9996 63:128.00/2.0001 64:128.00/2.0001

N=47 unrolled RAM 0.0851  VIC/SID 0.1000  extra slots per read 0.70
N=48 unrolled RAM 0.0833  VIC/SID 0.0937  extra slots per read 0.50
N=63 unrolled RAM 0.0635  VIC/SID 0.0789  extra slots per read 0.97
N=64 unrolled RAM 0.0625  VIC/SID 0.0781  extra slots per read 1.00

PAL N=47 miPlay burst 14.38 us (16-cycle 8.96 us; phi2 fallback 76.1 us); x1970/s = 2.83% (16-cycle 1.76%)
PAL N=48 miPlay burst 14.27 us (16-cycle 8.72 us; phi2 fallback 76.1 us); x1970/s = 2.81% (16-cycle 1.72%)
PAL N=63 miPlay burst 10.87 us (16-cycle 6.83 us; phi2 fallback 50.8 us); x1970/s = 2.14% (16-cycle 1.35%)
PAL N=64 miPlay burst 10.70 us (16-cycle 6.74 us; phi2 fallback 50.8 us); x1970/s = 2.11% (16-cycle 1.33%)
NTSC N=47 miPlay burst 13.85 us (16-cycle 8.63 us; phi2 fallback 73.3 us); x1970/s = 2.73% (16-cycle 1.70%)
NTSC N=48 miPlay burst 13.75 us (16-cycle 8.40 us; phi2 fallback 73.3 us); x1970/s = 2.71% (16-cycle 1.66%)
NTSC N=63 miPlay burst 10.48 us (16-cycle 6.58 us; phi2 fallback 48.9 us); x1970/s = 2.06% (16-cycle 1.30%)
NTSC N=64 miPlay burst 10.31 us (16-cycle 6.49 us; phi2 fallback 48.9 us); x1970/s = 2.03% (16-cycle 1.28%)

micro-costs at N=64 vs 63 (fast clocks):
  N=63 sta $d019+2cyc 14.22 (ack_edges=1: 12.19); raster bar 16.00; lda $d41b/$d41d pair 12.19
  N=63 sta $d018/sta $0400 unrolled (O3) 12.00; colour reg instead 8.13
  N=64 sta $d019+2cyc 14.00 (ack_edges=1: 12.00); raster bar 16.00; lda $d41b/$d41d pair 12.00
  N=64 sta $d018/sta $0400 unrolled (O3) 12.00; colour reg instead 8.00

raster bar with FIO_RAW_PRECISE (vicpath_model.py precise rule) vs global barrier:
  N=47 precise 18.83 global 20.00 fast clocks; two plain FIO reads 12.80
  N=48 precise 18.00 global 20.00 fast clocks; two plain FIO reads 12.00
  N=63 precise 14.22 global 16.01 fast clocks; two plain FIO reads 10.11
  N=64 precise 14.00 global 16.00 fast clocks; two plain FIO reads 10.00

== random phi2 I/O share (Amdahl; final.py rules, seed 3) ==
I/O  0.1%: N=64 59.9x  N=63 59.1x  N=48 45.7x  N=47 44.8x  N=16 15.8x
I/O  0.5%: N=64 47.9x  N=63 47.4x  N=48 38.4x  N=47 37.9x  N=16 14.9x
I/O  1.0%: N=64 38.6x  N=63 38.3x  N=48 32.3x  N=47 31.8x  N=16 13.9x
I/O  2.0%: N=64 27.8x  N=63 27.7x  N=48 24.3x  N=47 24.1x  N=16 12.3x
I/O  5.0%: N=64 15.8x  N=63 15.8x  N=48 14.5x  N=47 14.4x  N=16 9.2x
I/O 10.0%: N=64 9.5x  N=63 9.5x  N=48 8.9x  N=47 8.9x  N=16 6.6x

== beyond 64x, base N=64 (perf.py method) ==
I/O 0.5%: 64x->47.8  80x->56.1  96x->63.5
I/O 1.0%: 64x->38.4  80x->43.6  96x->47.9
I/O 2.0%: 64x->27.7  80x->30.3  96x->32.3
I/O 5.0%: 64x->15.8  80x->16.6  96x->17.1

== badline timing enabled, screen on (no sprites) ==
PAL BA-low 0.0547: N=63 59.55x  N=64 60.50x  N=47 44.43x  N=48 45.37x
NTSC BA-low 0.0629: N=63 59.04x  N=64 59.98x  N=47 44.04x  N=48 44.98x

== #203 pattern (vicpath_model.py rules) ==
N=47 #203 colour run lengths (pixels) -> {1: 772, 2: 17}; colour lasts 1.021 pixel
N=48 #203 colour run lengths (pixels) -> {1: 789}; colour lasts 1.000 pixel
N=63 #203 colour run lengths (pixels) -> {1: 224, 2: 187}; colour lasts 0.762 pixel
N=64 #203 colour run lengths (pixels) -> {1: 197, 2: 196}; colour lasts 0.750 pixel

== xahmol speed_probe scaling (16 lines at 63x, about 21-22 at 47x, PAL [M]) ==
N=47: about 21.4 lines (16 x 63 / N)
N=48: about 21.0 lines (16 x 63 / N)
N=63: about 16.0 lines (16 x 63 / N)
N=64: about 15.8 lines (16 x 63 / N)
```


## rev_fastio_latch.py

Reviewer model that found finding 1 of design section 14.1. Loads `lead_model.py`.

```python
# Reviewer scratch model (design stage). Copy of lead_model.run with one added
# access kind:
#   R = FIO read that pushes a side-effect entry (SID $19-$1C LATCH, or VIC
#       $1E/$1F clear), as FINAL_DESIGN_v2 section 2.13.4 specifies.
# Everything else is identical to lead_model.py (same grants, pop, barrier).
import importlib.util, sys, os

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('lm', os.path.join(here, 'lead_model.py'))
lm = importlib.util.module_from_spec(spec); spec.loader.exec_module(lm)
grants = lm.grants


def run(prog, N, reps, depth=4, o3=True, ack_edges=2):
    m = grants(N)
    t = 5
    last_en = -10
    pend = []
    last_c1 = -1
    barrier = -1
    for _ in range(reps):
        for k in prog:
            while True:
                pend = [p for p in pend if p[1] > t]
                ok = m[t % 64] and t >= barrier
                if k in 'VR':
                    ok = ok and (t - last_en >= 2) and not pend
                elif k in 'WSA':
                    ok = ok and len(pend) < depth
                elif k == 'M' and o3:
                    ok = ok and not any(p[0] for p in pend)
                if ok:
                    break
                t += 1
            last_en = t
            if k in 'WSAR':
                c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                if c1 <= last_c1:
                    c1 = last_c1 + 2
                last_c1 = c1
                c2 = c1 + 2
                done = c2 + (2 * ack_edges if k == 'A' else 2)
                ready = done + 2
                pend.append((k in 'WA', ready))
                if k == 'A':
                    barrier = ready
            t += 1
    return t / reps


if __name__ == '__main__':
    reps = 3000
    Ns = (6, 16, 32, 40, 47, 63)
    rows = {
        'lda $d012 (V, no push)  [design]': 'LLLV',
        'lda $d419/$d41a/$d41b (R, LATCH push)': 'LLLR',
        'lda $d01e (R, CLR push)': 'LLLR',
        'RAM lda $3000': 'LLLL',
    }
    print('%-40s ' % '' + ' '.join('%13s' % ('N=%d' % n) for n in Ns))
    for name, s in rows.items():
        cells = []
        for N in Ns:
            fc = run(s, N, reps)
            cells.append('%6.2f/%.3f' % (fc, fc / 64))
        print('%-40s ' % name + ' '.join('%13s' % c for c in cells))
    print('C64U idx15 [M]: RAM 0.064, $D012 0.080, $D41A 0.080, $D419 0.085')
```

Output:

```text
                                                   N=6          N=16          N=32          N=40          N=47          N=63
lda $d012 (V, no push)  [design]           42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    6.40/0.100    5.05/0.079
lda $d419/$d41a/$d41b (R, LATCH push)      42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    8.00/0.125    8.00/0.125
lda $d01e (R, CLR push)                    42.66/0.667   16.00/0.250    8.00/0.125    8.00/0.125    8.00/0.125    8.00/0.125
RAM lda $3000                              42.66/0.667   16.00/0.250    8.00/0.125    6.40/0.100    5.45/0.085    4.07/0.064
C64U idx15 [M]: RAM 0.064, $D012 0.080, $D41A 0.080, $D419 0.085
```


## rev_collision_alias.py

Reviewer model that found finding 2 of design section 14.1. Loads `lead_model.py`.

```python
# Reviewer scratch model (design stage, [I]).
# FINAL_DESIGN_v2 2.13.4: a $D01E read samples M2M at the end of X-1 (pre-edge),
# pushes CLR, and the clear lands at C2 (clear wins over the OR at that edge).
# Pixel edges are the c64 edges ending fast clocks t with t % 8 == 7
# (e_3, e_7, ... = f 7, 15, ...).
# A collision bit set at pixel edge P is reported by the next read only if
#   C2(k) < P < X(k+1) - 1   (strict; P == X(k+1)-1 is not in the pre-edge sample).
# For a persistent overlap (bit re-set at every pixel edge), the loop sees the
# collision iff some read interval contains such a P.
import importlib.util, os
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('lm', os.path.join(here, 'lead_model.py'))
lm = importlib.util.module_from_spec(spec); spec.loader.exec_module(lm)


def trace(prog, N, reps, start):
    m = lm.grants(N)
    t = start
    last_en = -10
    pend = []
    last_c1 = -1
    reads = []   # (X, C2)
    for _ in range(reps):
        for k in prog:
            while True:
                pend = [p for p in pend if p > t]
                ok = m[t % 64]
                if k == 'R':
                    ok = ok and (t - last_en >= 2) and not pend
                if ok:
                    break
                t += 1
            last_en = t
            if k == 'R':
                c1 = t + 1 if (t + 1) % 2 == 1 else t + 2
                if c1 <= last_c1:
                    c1 = last_c1 + 2
                last_c1 = c1
                c2 = c1 + 2
                pend.append(c2 + 2 + 2)   # done + island registration
                reads.append((t, c2))
            t += 1
    return reads


def visible_fraction(prog, N, reps=2000, start=5):
    reads = trace(prog, N, reps, start)
    vis = 0
    for (x0, c2), (x1, _) in zip(reads, reads[1:]):
        if any(P % 8 == 7 for P in range(c2 + 1, x1 - 1)):
            vis += 1
    return vis / (len(reads) - 1), reads


if __name__ == '__main__':
    loops = {'unrolled lda $d01e': 'LLLR',
             'lda $d01e / beq (taken)': 'LLLRLLL',
             'lda $d01e / and # / beq': 'LLLRLLLLL'}
    for name, p in loops.items():
        for N in (63, 47, 40, 32):
            res = []
            for start in range(5, 13):
                f, reads = visible_fraction(p, N, start=start)
                res.append(f)
            per = (reads[-1][0] - reads[0][0]) / (len(reads) - 1)
            print('%-26s N=%2d period %.2f fc  visible-interval fraction by start phase: %s'
                  % (name, N, per, ' '.join('%.2f' % r for r in res)))
```

Output:

```text
unrolled lda $d01e         N=63 period 8.00 fc  visible-interval fraction by start phase: 1.00 1.00 0.00 0.00 0.00 0.00 0.00 0.00
unrolled lda $d01e         N=47 period 8.00 fc  visible-interval fraction by start phase: 1.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00
unrolled lda $d01e         N=40 period 8.00 fc  visible-interval fraction by start phase: 1.00 0.00 0.00 0.00 0.00 0.00 1.00 1.00
unrolled lda $d01e         N=32 period 8.00 fc  visible-interval fraction by start phase: 0.00 0.00 0.00 0.00 0.00 1.00 1.00 0.00
lda $d01e / beq (taken)    N=63 period 8.00 fc  visible-interval fraction by start phase: 0.01 0.00 0.00 0.00 0.00 0.01 0.01 0.01
lda $d01e / beq (taken)    N=47 period 10.67 fc  visible-interval fraction by start phase: 0.67 0.67 0.67 0.67 0.67 0.33 0.33 0.33
lda $d01e / beq (taken)    N=40 period 12.00 fc  visible-interval fraction by start phase: 1.00 1.00 1.00 1.00 1.00 1.00 1.00 1.00
lda $d01e / beq (taken)    N=32 period 14.00 fc  visible-interval fraction by start phase: 1.00 1.00 1.00 1.00 1.00 1.00 1.00 1.00
lda $d01e / and # / beq    N=63 period 10.11 fc  visible-interval fraction by start phase: 0.53 0.53 0.53 0.53 0.53 0.53 0.53 0.53
lda $d01e / and # / beq    N=47 period 12.80 fc  visible-interval fraction by start phase: 0.80 1.00 1.00 1.00 0.80 1.00 1.00 1.00
lda $d01e / and # / beq    N=40 period 16.00 fc  visible-interval fraction by start phase: 1.00 1.00 1.00 1.00 1.00 1.00 1.00 1.00
lda $d01e / and # / beq    N=32 period 18.00 fc  visible-interval fraction by start phase: 1.00 1.00 1.00 1.00 1.00 1.00 1.00 1.00
```


## balow_calc.py

BA-low exposure and #665 arithmetic.

```python
# Scratch arithmetic for balow.md (design stage only).
# BA-low exposure of a phi2 forward with badline timing disabled,
# engine build (D5: forward waits for BA=1) vs I/O-only ownership.

def run(name, cycles_per_line, lines, badlines, ba_bad=43, ba_spr_line=0):
    total = cycles_per_line * lines
    # badline window: BA low from cycle 12 to 54 inclusive = 43 cycles
    low_bad = badlines * ba_bad
    low_spr = lines * ba_spr_line
    low = low_bad + low_spr
    frac = low / total
    # expected extra wait for a random forward: P(in window) * E[remaining]
    # windows treated as independent blocks; E[remaining] = (len+1)/2
    e_bad = badlines * ba_bad * (ba_bad + 1) / 2 / total
    e_spr = (lines * ba_spr_line * (ba_spr_line + 1) / 2 / total) if ba_spr_line else 0
    print(f"{name}: BA-low {frac*100:.1f}% of cycles; mean extra wait per random "
          f"phi2 forward (D5) {e_bad+e_spr:.2f} us; I/O-only: 0")

# PAL 6569: 63 cycles x 312 lines, 25 badlines (200 display lines)
run("PAL screen on, no sprites", 63, 312, 25)
# 8 sprites on every line: BA low cycles 55..62 + 0..10 approx = 19 per line
run("PAL screen on, 8 sprites all lines", 63, 312, 25, ba_spr_line=19)
# NTSC 6567R8: 65 x 263
run("NTSC screen on, no sprites", 65, 263, 25)

for std, line_hz in (("PAL", 15625.0), ("NTSC", 15734.26)):
    print(f"{std}: badline repetition {line_hz/8:.1f} Hz (every 8th raster line)")

# Digi at 8 kHz NMI: period 125 us. D5 stall up to 43 us when the
# handler's CIA ICR read lands in a badline window.
period = 1e6 / 8000
print(f"8 kHz digi: D5 worst sample delay 43 us = {43/period*100:.0f}% of a period;"
      f" I/O-only bound 1 phi2 (~1.0 us) = {1.0/period*100:.1f}%")
```

Output:

```text
PAL screen on, no sprites: BA-low 5.5% of cycles; mean extra wait per random phi2 forward (D5) 1.20 us; I/O-only: 0
PAL screen on, 8 sprites all lines: BA-low 35.6% of cycles; mean extra wait per random phi2 forward (D5) 4.22 us; I/O-only: 0
NTSC screen on, no sprites: BA-low 6.3% of cycles; mean extra wait per random phi2 forward (D5) 1.38 us; I/O-only: 0
PAL: badline repetition 1953.1 Hz (every 8th raster line)
NTSC: badline repetition 1966.8 Hz (every 8th raster line)
8 kHz digi: D5 worst sample delay 43 us = 34% of a period; I/O-only bound 1 phi2 (~1.0 us) = 0.8%
```


## ntsc_plans.py

NTSC MMCM plan search with a ps-exact 2:1 test, and constants.

```python
# NTSC MMCM plan check and search for the C64 CPU island (design-stage arithmetic).
# c64_clk = VCO/Oc, c64_cpu_clk = VCO/Of with Oc = 2*Of (integer divides only).
# Physical input: FCLK2 = 33.333333 MHz * 36 / 4 / 2 = 150 MHz (PS7 xci).
# Vivado model: master clk_fpga_2 = 6.666 ns, generated periods rounded to 1 ps.
# ps-exact: round(c64) == 2 * round(fast) when each period is rounded on its own.
# That holds under every tie rule iff frac(fast ps) < 1/4 or > 3/4; the test below
# keeps 0.02 ps of margin on the fast period (0.04 ps on the c64 period).
from fractions import Fraction as F
import math

FIN = F(150_000_000)
MASTER_PS = F(6666)
IDEAL = F(14_318_181_818_181, 1_000_000) * 16 / 7      # placeholder, replaced below
IDEAL = F(315_000_000, 88) * 4 * 16 / 7                # 4 x 3.579545.. MHz x 16/7 = 360/11 MHz
CONST = F(32_727_266)                                  # MT_C64_NTSC_HZ / create_project.tcl
assert IDEAL == F(360_000_000, 11)

VCO_MIN, VCO_MAX = 600e6, 1440e6     # DS187 v1.21 Table 72, -2
PFD_MIN, PFD_MAX = 10e6, 500e6

def ppm(f, ref):
    return float((f - ref) / ref * 1_000_000)

def ps_model(D, M, O):
    return MASTER_PS * D * O / M        # exact ps under the 6.666 ns master

def frac(x):
    return x - math.floor(x)

MODES = ("half-even", "half-up", "half-down")

def rnd(x, mode):
    fl = math.floor(x); d = x - fl
    if d > F(1, 2): return fl + 1
    if d < F(1, 2): return fl
    return {"half-even": fl + (fl % 2), "half-up": fl + 1, "half-down": fl}[mode]

def ps_exact(fr):
    return fr < F(23, 100) or fr > F(77, 100)

def exact_all_modes(pf):
    return all(rnd(2 * pf, m) == 2 * rnd(pf, m) for m in MODES)

def report(D, M, Oc, Of, label):
    vco = FIN * M / D
    fc, ff = vco / Oc, vco / Of
    pc, pf = ps_model(D, M, Oc), ps_model(D, M, Of)
    print(f"{label}: D={D} M={float(M)} VCO={float(vco)/1e6:.4f} MHz PFD={float(FIN/D)/1e6:.3f} MHz")
    print(f"   c64 {float(fc)/1e6:.6f} MHz ({ppm(fc, IDEAL):+.3f} ppm vs 360/11 MHz, {ppm(fc, CONST):+.3f} ppm vs 32.727266)")
    print(f"   fast {float(ff)/1e6:.6f} MHz, period {1e9/float(ff):.4f} ns, half {0.5e9/float(ff):.4f} ns")
    print(f"   vivado model: c64 {float(pc):.4f} ps, fast {float(pf):.4f} ps, frac(fast)={float(frac(pf)):.4f}"
          f"  -> ps-exact={ps_exact(frac(pf))}")
    for mode in MODES:
        print(f"      rounding {mode:9s}: c64 {rnd(pc, mode)} ps, fast {rnd(pf, mode)} ps, exact 2:1 = {rnd(pc, mode) == 2 * rnd(pf, mode)}")
    return vco

print("== Baseline NTSC plan ==")
report(5, F(24), 22, 11, "baseline")
for expr, v in (("6.666*5*22/24", 6.666 * 5 * 22 / 24), ("6.666*22*5/24", 6.666 * 22 * 5 / 24)):
    d = F(v) - F(305525, 10000)
    side = "below" if d < 0 else "above"
    print(f"   IEEE double {expr} = {v!r} ns: {float(d):+.1e} ns, {side} the 30.5525 ns tie")
print("== Baseline PAL plan (for comparison) ==")
pal_ideal = F(17_734_475) * 16 / 9      # 4.43361875 MHz x 4 = 17.734475 MHz; x16/9 = 31.527955 MHz
vco = FIN * F(227, 4) / 9
print(f"   PAL c64 {float(vco/30)/1e6:.6f} MHz, {ppm(vco/30, pal_ideal):+.2f} ppm vs 17.734475x16/9")

print("\n== NTSC search: integer Of, Oc = 2 Of, |ppm vs 360/11| <= 60 ==")
hits = []
for D in range(1, 16):
    pfd = FIN / D
    if not (PFD_MIN <= pfd <= PFD_MAX):
        continue
    for m8 in range(16, 64 * 8 + 1):
        M = F(m8, 8)
        vco = FIN * M / D
        if not (VCO_MIN <= vco <= VCO_MAX):
            continue
        for Of in range(1, 65):
            Oc = 2 * Of
            if Oc > 128:
                continue
            fc = vco / Oc
            e = ppm(fc, IDEAL)
            if abs(e) > 60:
                continue
            pf = ps_model(D, M, Of)
            fr = frac(pf)
            robust = ps_exact(fr)
            assert ps_model(D, M, Oc) == 2 * pf
            assert not robust or exact_all_modes(pf)
            hits.append((abs(e), e, D, float(M), Oc, Of, float(vco) / 1e6, float(pf), float(fr), robust,
                         M.denominator != 1, float(vco) >= 1439e6))
hits.sort()
for h in hits[:30]:
    print("  ppm %+8.3f D=%2d M=%7.3f Oc=%3d Of=%2d VCO=%9.3f fast=%.3fps frac=%.3f ps-exact=%s fracM=%s atVCOmax=%s" % h[1:])
print("  ps-exact (margin 0.02 ps), integer M:")
for h in [h for h in hits if h[9] and not h[10]][:8]:
    print("  ppm %+8.3f D=%2d M=%7.3f Oc=%3d Of=%2d VCO=%9.3f fast=%.3fps frac=%.3f ps-exact=%s fracM=%s atVCOmax=%s" % h[1:])
print("  ps-exact (margin 0.02 ps), any M:")
for h in [h for h in hits if h[9]][:8]:
    print("  ppm %+8.3f D=%2d M=%7.3f Oc=%3d Of=%2d VCO=%9.3f fast=%.3fps frac=%.3f ps-exact=%s fracM=%s atVCOmax=%s" % h[1:])

for h in hits:
    if h[2] in (7, 8) and h[3] == 48.875:
        pf = ps_model(h[2], F(391, 8), h[5])
        rf, rc = rnd(pf, "half-up"), rnd(2 * pf, "half-up")
        assert all(rnd(pf, m) == rf and rnd(2 * pf, m) == rc for m in MODES)
        print("  D=%d M=48.875: fast %d ps, c64 %d ps under every tie rule; 2 x fast = %d; tie-free but not ps-exact"
              % (h[2], rf, rc, 2 * rf))
grid = [F(k, 1000) for k in range(1000) if F(k, 1000) not in (F(1, 4), F(3, 4))]
mism = [f for f in grid if (f < F(1, 4) or f > F(3, 4)) != exact_all_modes(15276 + f)]
print(f"  rule check on a 1/1000 frac grid (ties excluded): exact 2:1 under all modes iff frac < 1/4 or > 3/4; mismatches {len(mism)}")

print("\n== NTSC frame and timing constants ==")
phi2 = IDEAL / 32
print(f"  phi2 {float(phi2):.3f} Hz; line 65 cycles = {65e6/float(phi2):.3f} us; frame 65x263 = {65*263} cycles = {float(phi2)/(65*263):.4f} Hz")
print(f"  PAL frame 63x312 = {63*312} cycles at {float(pal_ideal/32):.2f} Hz = {float(pal_ideal/32)/(63*312):.4f} Hz")
print(f"  2.0 s reset window: NTSC {int(2*phi2)} native cycles, PAL {int(2*pal_ideal/32)} native cycles")
print(f"  badline share (25 lines x 43 stolen cycles, screen on): NTSC {25*43/(65*263)*100:.2f} %, PAL {25*43/(63*312)*100:.2f} %")
print(f"  index-15 RAM loop with badline timing on: NTSC {63*(1-25*43/(65*263)):.2f}x, PAL {63*(1-25*43/(63*312)):.2f}x")
print(f"  60 Hz output repeats one NTSC frame every {1/(60-float(phi2)/(65*263)):.2f} s")
print(f"  timebase.tas 100 frames: {65*263*100} ticks (NTSC 6567R8), {63*312*100} (PAL)")
print(f"  xahmol probe scaled to NTSC lines (63/65): 92 -> {92*63/65:.1f}, 21 -> {21*63/65:.1f}, 16 -> {16*63/65:.1f}")
print(f"  fast clocks per native cycle: {float((FIN*24/5/11)/phi2):.6f}")
print(f"  IIgs fast clock 57.272728 MHz period {1e3/57.272728:.3f} ns vs NTSC island {1e9/float(FIN*24/5/11):.3f} ns")
```

Output:

```text
== Baseline NTSC plan ==
baseline: D=5 M=24.0 VCO=720.0000 MHz PFD=30.000 MHz
   c64 32.727273 MHz (+0.000 ppm vs 360/11 MHz, +0.206 ppm vs 32.727266)
   fast 65.454545 MHz, period 15.2778 ns, half 7.6389 ns
   vivado model: c64 30552.5000 ps, fast 15276.2500 ps, frac(fast)=0.2500  -> ps-exact=False
      rounding half-even: c64 30552 ps, fast 15276 ps, exact 2:1 = True
      rounding half-up  : c64 30553 ps, fast 15276 ps, exact 2:1 = False
      rounding half-down: c64 30552 ps, fast 15276 ps, exact 2:1 = True
   IEEE double 6.666*5*22/24 = 30.5525 ns: -1.6e-15 ns, below the 30.5525 ns tie
   IEEE double 6.666*22*5/24 = 30.552500000000006 ns: +5.5e-15 ns, above the 30.5525 ns tie
== Baseline PAL plan (for comparison) ==
   PAL c64 31.527778 MHz, -5.64 ppm vs 17.734475x16/9

== NTSC search: integer Of, Oc = 2 Of, |ppm vs 360/11| <= 60 ==
  ppm   +0.000 D= 5 M= 24.000 Oc= 22 Of=11 VCO=  720.000 fast=15276.250ps frac=0.250 ps-exact=False fracM=False atVCOmax=False
  ppm   +0.000 D= 5 M= 48.000 Oc= 44 Of=22 VCO= 1440.000 fast=15276.250ps frac=0.250 ps-exact=False fracM=False atVCOmax=True
  ppm   +0.000 D=10 M= 48.000 Oc= 22 Of=11 VCO=  720.000 fast=15276.250ps frac=0.250 ps-exact=False fracM=False atVCOmax=False
  ppm   +0.000 D=11 M= 48.000 Oc= 20 Of=10 VCO=  654.545 fast=15276.250ps frac=0.250 ps-exact=False fracM=False atVCOmax=False
  ppm  +46.503 D= 7 M= 48.875 Oc= 32 Of=16 VCO= 1047.321 fast=15275.540ps frac=0.540 ps-exact=False fracM=True atVCOmax=False
  ppm  +46.503 D= 8 M= 48.875 Oc= 28 Of=14 VCO=  916.406 fast=15275.540ps frac=0.540 ps-exact=False fracM=True atVCOmax=False
  ppm  -48.225 D= 6 M= 47.125 Oc= 36 Of=18 VCO= 1178.125 fast=15276.987ps frac=0.987 ps-exact=True fracM=True atVCOmax=False
  ppm  -48.225 D= 9 M= 47.125 Oc= 24 Of=12 VCO=  785.417 fast=15276.987ps frac=0.987 ps-exact=True fracM=True atVCOmax=False
  ps-exact (margin 0.02 ps), integer M:
  ps-exact (margin 0.02 ps), any M:
  ppm  -48.225 D= 6 M= 47.125 Oc= 36 Of=18 VCO= 1178.125 fast=15276.987ps frac=0.987 ps-exact=True fracM=True atVCOmax=False
  ppm  -48.225 D= 9 M= 47.125 Oc= 24 Of=12 VCO=  785.417 fast=15276.987ps frac=0.987 ps-exact=True fracM=True atVCOmax=False
  D=7 M=48.875: fast 15276 ps, c64 30551 ps under every tie rule; 2 x fast = 30552; tie-free but not ps-exact
  D=8 M=48.875: fast 15276 ps, c64 30551 ps under every tie rule; 2 x fast = 30552; tie-free but not ps-exact
  rule check on a 1/1000 frac grid (ties excluded): exact 2:1 under all modes iff frac < 1/4 or > 3/4; mismatches 0

== NTSC frame and timing constants ==
  phi2 1022727.273 Hz; line 65 cycles = 63.556 us; frame 65x263 = 17095 cycles = 59.8261 Hz
  PAL frame 63x312 = 19656 cycles at 985248.61 Hz = 50.1246 Hz
  2.0 s reset window: NTSC 2045454 native cycles, PAL 1970497 native cycles
  badline share (25 lines x 43 stolen cycles, screen on): NTSC 6.29 %, PAL 5.47 %
  index-15 RAM loop with badline timing on: NTSC 59.04x, PAL 59.55x
  60 Hz output repeats one NTSC frame every 5.75 s
  timebase.tas 100 frames: 1709500 ticks (NTSC 6567R8), 1965600 (PAL)
  xahmol probe scaled to NTSC lines (63/65): 92 -> 89.2, 21 -> 20.4, 16 -> 15.5
  fast clocks per native cycle: 64.000000
  IIgs fast clock 57.272728 MHz period 17.460 ns vs NTSC island 15.278 ns
```

## governor_credit.py

Forward-completion credit against the old rule and the rejected own-grant rule: I/O loops at N = 1-64, the `meas_c` cart column, the exhaustive periodic-pattern maximum and the governor invariants. Loads `perf.py` and `final.py`.

```python
# Completion credit in the speed governor (design section 2.7), design stage [I].
# Loads final.py's grant table (gr) and forward rule; final.py imports perf.py.
# Both are copied verbatim from C64_TURBO_MODELS.md into this script's folder and
# loaded by path from here, with their own printed output discarded, so this
# script also runs under python -I.
#
# Rules (64 fast clocks per phi2, f = 0..63; as final.py):
#  L  granted local cycle: enable at the end of the first granted clock at or
#     after the ready clock; the next cycle is ready one clock later.
#  F  phi2 forward requested at the ready clock f_r: it runs in this native
#     cycle if f_r <= 30, else in the next; its completion enable is at end
#     f = 4 of the following frame, and the next cycle is ready at f = 5.
# Completion enable variants:
#  design  no grant term (final.py as written).
#  credit  needs fwd_credit_q | grant_q. The credit is set by any grant in
#          [f_r, completion clock]; without it the enable waits for the next grant.
#  naive   waits for the first grant at or after f = 4 (rejected).
# Not modelled: ACKWAIT/DMA_WAIT exits (later than f = 4, so the credit is set
# at every N), and the credit clear on a mode change (single mode here).
import contextlib, importlib.util, io, itertools, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(mod)
    return mod


perf = load('perf')
final = load('final')
CUT, RES = 30, 5
_tables = {}


def grants(N):
    if N not in _tables:
        _tables[N] = final.gr(N)
    return _tables[N]


def run(seq, N, mode='design', t=RES, enables=None):
    """Return the ready clock after seq; append each enable clock to enables."""
    m = grants(N)
    for c in seq:
        if c == 'L':
            while not m[t % 64]:
                t += 1
            en = t
        else:
            req = t
            f = t % 64
            b = t - f
            en = (b + 64 if f <= CUT else b + 128) + RES - 1   # end f = 4
            if mode == 'credit':
                if not any(m[x % 64] for x in range(req, en + 1)):
                    while not m[en % 64]:
                        en += 1
            elif mode == 'naive':
                while not m[en % 64]:
                    en += 1
        if enables is not None:
            enables.append(en)
        t = en + 1
    return t


def phi2_per(seq, N, mode, reps=300):
    """phi2 per repetition, computed as final.py prints it."""
    return run(list(seq) * reps, N, mode) / reps / 64.0


def steady_rate(pat, N, mode):
    """Exact long-run CPU cycles per phi2 of a repeated pattern (cycle detection)."""
    seen = {}
    t = RES
    k = 0
    while True:
        key = t % 64
        if key in seen:
            k0, t0 = seen[key]
            return len(pat) * (k - k0) * 64.0 / (t - t0)
        seen[key] = (k, t)
        t = run(pat, N, mode, t)
        k += 1


def window_and_prefix(pat, N, mode, ops=1536):
    """Max enables in any 64-clock window, and the largest excess of enables over grants so far."""
    reps = max(1, ops // len(pat))
    ens = []
    run(list(pat) * reps, N, mode, RES, ens)
    m = grants(N)
    wmax = 0
    j = 0
    for i, e in enumerate(ens):
        while ens[j] < e - 63:
            j += 1
        wmax = max(wmax, i - j + 1)
    cum = list(itertools.accumulate(1 if x else 0 for x in m))   # grants in 0..s
    excess = 0
    for i, e in enumerate(ens):
        g = (e // 64) * sum(m) + cum[e % 64]                        # grants in clocks 0..e
        excess = max(excess, i + 1 - g)
    return wmax, excess


NS = [1, 2, 3, 6, 16, 32, 48, 64]
MODES = ('design', 'credit', 'naive')
ROWS = [('unrolled lda $de80 (LLLF)', 'LLLF'),
        ('lda $dc01,x / dex / bne', 'LLLFLLLLL'),
        ('sta $de00,x / dex / bne', 'LLLFFLLLLL'),
        ('FF (two back-to-back forwards)', 'FF'),
        ('inc $de00 (RMW on I/O, LLLFFF)', 'LLLFFF'),
        ('RAM lda abs,x / dex / bne', 'LLLLLLLLL')]

print('== Check: design mode reproduces final.run for every row and N ==')
ok = all(run(list(v) * 300, N, 'design') == final.run(list(v) * 300, N)
         for _, v in ROWS for N in NS + list(final.TABLE))
print('identical:', ok)

print('\n== phi2 per iteration (per pair for FF) ==')
for name, v in ROWS:
    print(name)
    for mode in MODES:
        print('  %-7s' % mode, ' '.join('%d:%.2f' % (N, phi2_per(v, N, mode)) for N in NS))

print('\n== meas_c cart column (unrolled lda $de80), phi2 per access, N = 1..8 ==')
print('U64E reference [M, gist839 meas_c]: 2 at 3-5 MHz, 1 at 6 MHz and above; no 2 MHz figure, none at 1 MHz with badline timing disabled')
for mode in MODES:
    print('  %-7s' % mode, ' '.join('%d:%.2f' % (N, phi2_per('LLLF', N, mode)) for N in (1, 2, 3, 4, 6, 8)))

print('\n== N = 1 trace, unrolled lda $de80 (op, frame, f of the enable) ==')
for mode in ('design', 'credit'):
    ens = []
    run(list('LLLF') * 2, 1, mode, RES, ens)
    print('%-7s' % mode, [(op, e // 64, e % 64) for op, e in zip('LLLF' * 2, ens)])

print('\n== N = 1 (index 0, badline timing disabled): long-run CPU cycles per phi2, stock = 1.000 ==')
for name, v in ROWS[:3] + [('lda ($fb),y from I/O (LLLLF)', 'LLLLF'),
                           ('lda $dc0d / and # / bne poll', 'LLLFLLLL')] + ROWS[4:]:
    if v == 'FF':
        continue
    print('%-32s' % name, '  '.join('%s %.3f' % (md, steady_rate(v, 1, md)) for md in MODES))

print('\n== final.py loops at N = 1: design (as final.py prints) -> credit ==')
for name, v in final.loops.items():
    print('%-34s 1:%.2f -> 1:%.2f' % (name, phi2_per(v, 1, 'design'), phi2_per(v, 1, 'credit')))
same = all(phi2_per(v, N, 'credit') == phi2_per(v, N, 'design')
           for v in final.loops.values() for N in list(final.TABLE)[1:] + [48, 64])
print('credit equals design for every final.py loop at every N >= 2 (final.TABLE, 48, 64):', same)

print('\n== Credit always set at N >= 2: every forward window [f_r, completion] holds a grant ==')
always = []
for N in range(1, 65):
    m = grants(N)
    miss = [fr for fr in range(64)
            if not any(m[x % 64] for x in range(fr, (64 if fr <= CUT else 128) + RES))]
    if N == 1:
        print('N = 1: no grant in the window for f_r =', '%d..%d' % (miss[0], miss[-1]))
    else:
        always.append(not miss)
print('N = 2..64: always set:', all(always))

print('\n== Max long-run CPU cycles per phi2 over periodic L/F patterns, length 1..10 ==')
pats = [''.join(p) for n in range(1, 11) for p in itertools.product('LF', repeat=n)]
for N in [1, 2, 3, 4, 6, 8, 12, 16, 32, 48, 64]:
    out = []
    for mode in ('design', 'credit'):
        best, arg = 0.0, ''
        for p in pats:
            r = steady_rate(p, N, mode)
            if r > best + 1e-9:
                best, arg = r, p
        out.append('%s %.3f (%s)' % (mode, best, arg))
    print('N=%2d  ' % N + '  '.join(out))

print('\n== Governor invariants over the same patterns (about 1536 bus cycles each; excess = enables minus grants since start) ==')
for N in [1, 2, 3, 6, 16, 64]:
    out = []
    for mode in ('design', 'credit'):
        w, x = 0, 0
        for p in pats:
            wm, ex = window_and_prefix(p, N, mode)
            w = max(w, wm)
            x = max(x, ex)
        out.append('%s: window max %d (limit N+1 = %d), enables ahead of grants %d'
                   % (mode, w, N + 1, x))
    print('N=%2d  ' % N + ' | '.join(out))
```

Output:

```text
== Check: design mode reproduces final.run for every row and N ==
identical: True

== phi2 per iteration (per pair for FF) ==
unrolled lda $de80 (LLLF)
  design  1:3.00 2:2.00 3:2.00 6:1.00 16:1.00 32:1.00 48:1.00 64:1.00
  credit  1:4.00 2:2.00 3:2.00 6:1.00 16:1.00 32:1.00 48:1.00 64:1.00
  naive   1:4.00 2:3.00 3:2.00 6:2.00 16:1.00 32:1.00 48:1.00 64:1.00
lda $dc01,x / dex / bne
  design  1:8.00 2:5.00 3:3.00 6:2.00 16:2.00 32:1.00 48:1.00 64:1.00
  credit  1:9.00 2:5.00 3:3.00 6:2.00 16:2.00 32:1.00 48:1.00 64:1.00
  naive   1:9.00 2:5.00 3:4.00 6:2.00 16:2.00 32:1.00 48:1.00 64:1.00
sta $de00,x / dex / bne
  design  1:9.00 2:6.00 3:4.00 6:3.00 16:3.00 32:2.00 48:2.00 64:2.00
  credit  1:10.00 2:6.00 3:4.00 6:3.00 16:3.00 32:2.00 48:2.00 64:2.00
  naive   1:10.00 2:6.00 3:5.00 6:3.00 16:3.00 32:2.00 48:2.00 64:2.00
FF (two back-to-back forwards)
  design  1:2.00 2:2.00 3:2.00 6:2.00 16:2.00 32:2.00 48:2.00 64:2.00
  credit  1:2.00 2:2.00 3:2.00 6:2.00 16:2.00 32:2.00 48:2.00 64:2.00
  naive   1:2.00 2:2.00 3:2.00 6:2.00 16:2.00 32:2.00 48:2.00 64:2.00
inc $de00 (RMW on I/O, LLLFFF)
  design  1:5.00 2:4.00 3:4.00 6:3.00 16:3.00 32:3.00 48:3.00 64:3.00
  credit  1:6.00 2:4.00 3:4.00 6:3.00 16:3.00 32:3.00 48:3.00 64:3.00
  naive   1:6.00 2:5.00 3:4.00 6:4.00 16:3.00 32:3.00 48:3.00 64:3.00
RAM lda abs,x / dex / bne
  design  1:9.00 2:4.50 3:3.00 6:1.50 16:0.56 32:0.28 48:0.19 64:0.14
  credit  1:9.00 2:4.50 3:3.00 6:1.50 16:0.56 32:0.28 48:0.19 64:0.14
  naive   1:9.00 2:4.50 3:3.00 6:1.50 16:0.56 32:0.28 48:0.19 64:0.14

== meas_c cart column (unrolled lda $de80), phi2 per access, N = 1..8 ==
U64E reference [M, gist839 meas_c]: 2 at 3-5 MHz, 1 at 6 MHz and above; no 2 MHz figure, none at 1 MHz with badline timing disabled
  design  1:3.00 2:2.00 3:2.00 4:2.00 6:1.00 8:1.00
  credit  1:4.00 2:2.00 3:2.00 4:2.00 6:1.00 8:1.00
  naive   1:4.00 2:3.00 3:2.00 4:2.00 6:2.00 8:1.00

== N = 1 trace, unrolled lda $de80 (op, frame, f of the enable) ==
design  [('L', 0, 5), ('L', 1, 5), ('L', 2, 5), ('F', 3, 4), ('L', 3, 5), ('L', 4, 5), ('L', 5, 5), ('F', 6, 4)]
credit  [('L', 0, 5), ('L', 1, 5), ('L', 2, 5), ('F', 3, 5), ('L', 4, 5), ('L', 5, 5), ('L', 6, 5), ('F', 7, 5)]

== N = 1 (index 0, badline timing disabled): long-run CPU cycles per phi2, stock = 1.000 ==
unrolled lda $de80 (LLLF)        design 1.333  credit 1.000  naive 1.000
lda $dc01,x / dex / bne          design 1.125  credit 1.000  naive 1.000
sta $de00,x / dex / bne          design 1.111  credit 1.000  naive 1.000
lda ($fb),y from I/O (LLLLF)     design 1.250  credit 1.000  naive 1.000
lda $dc0d / and # / bne poll     design 1.143  credit 1.000  naive 1.000
inc $de00 (RMW on I/O, LLLFFF)   design 1.200  credit 1.000  naive 1.000
RAM lda abs,x / dex / bne        design 1.000  credit 1.000  naive 1.000

== final.py loops at N = 1: design (as final.py prints) -> credit ==
RAM lda abs,x/dex/bne              1:9.00 -> 1:9.00
lda $dc01,x/dex/bne                1:8.00 -> 1:9.00
sta $d020,x/dex/bne                1:9.00 -> 1:10.00
sta $d020/dex/bne                  1:8.00 -> 1:9.00
unrolled lda $de80                 1:3.00 -> 1:4.00
fill $0400,x+$d800,x colour fwd    1:14.00 -> 1:15.00
fill colour local                  1:15.00 -> 1:15.00
credit equals design for every final.py loop at every N >= 2 (final.TABLE, 48, 64): True

== Credit always set at N >= 2: every forward window [f_r, completion] holds a grant ==
N = 1: no grant in the window for f_r = 6..30
N = 2..64: always set: True

== Max long-run CPU cycles per phi2 over periodic L/F patterns, length 1..10 ==
N= 1  design 2.000 (LF)  credit 1.000 (L)
N= 2  design 2.000 (L)  credit 2.000 (L)
N= 3  design 3.000 (L)  credit 3.000 (L)
N= 4  design 4.000 (L)  credit 4.000 (L)
N= 6  design 6.000 (L)  credit 6.000 (L)
N= 8  design 8.000 (L)  credit 8.000 (L)
N=12  design 12.000 (L)  credit 12.000 (L)
N=16  design 16.000 (L)  credit 16.000 (L)
N=32  design 32.000 (L)  credit 32.000 (L)
N=48  design 48.000 (L)  credit 48.000 (L)
N=64  design 64.000 (L)  credit 64.000 (L)

== Governor invariants over the same patterns (about 1536 bus cycles each; excess = enables minus grants since start) ==
N= 1  design: window max 2 (limit N+1 = 2), enables ahead of grants 768 | credit: window max 2 (limit N+1 = 2), enables ahead of grants 0
N= 2  design: window max 3 (limit N+1 = 3), enables ahead of grants 0 | credit: window max 3 (limit N+1 = 3), enables ahead of grants 0
N= 3  design: window max 4 (limit N+1 = 4), enables ahead of grants 0 | credit: window max 4 (limit N+1 = 4), enables ahead of grants 0
N= 6  design: window max 7 (limit N+1 = 7), enables ahead of grants 0 | credit: window max 7 (limit N+1 = 7), enables ahead of grants 0
N=16  design: window max 16 (limit N+1 = 17), enables ahead of grants 0 | credit: window max 16 (limit N+1 = 17), enables ahead of grants 0
N=64  design: window max 64 (limit N+1 = 65), enables ahead of grants 0 | credit: window max 64 (limit N+1 = 65), enables ahead of grants 0
```

## nmi_rearm.py

NMI hold and rearm compared with no latch, the old set/clear latch and stock, and turbo → exact switch rules (the carry against the wait it replaced): directed cases S0-S12 and fixed-seed fuzz turbo only, with exact/turbo switches, and with a forced switch after pulses on any clock.

```python
# nmi_rearm.py: NMI edges across turbo stalls (C64_TURBO_DESIGN.md section 2.9).
# Compares no latch, the v3 draft's set/clear latch, the hold-and-rearm rule
# and stock (6510 / MiSTer exact mode) on directed cases and fixed-seed fuzz,
# and compares turbo->exact switch rules.
#
# Time base: fast clock t, 64 per native cycle, f = t % 64. CIA irq_n changes
# at e_0 (end f=1), so a native change is visible from t = 64*c + 2. The island
# registers the native term with a 2-clock lag (design section 5.9).
# T65 NMI detector (T65.vhd:784-789): on each Enable, an edge if the previous
# sample was 1 and the shown value is 0; the shown value becomes the previous
# sample. Only one previous sample is kept.
# Stock samples once per native cycle at R (end f=63), RDY-held cycles
# included (multitini_c64_cpu_scheduler.vhd:40-43). NMIAct is one flag, so
# edges while the CPU is halted with an NMI pending merge into one NMI.
# Exact mode in the island: one enable at end f=0 with B0 = the native term at
# the previous R.
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


def e0(c):
    return c * F + 2


def run(island, native, init, turbo_en, requests=None, mode0='turbo'):
    """Returns (T65 edge times, enable times, late). turbo_en(t): grant/stall
    pattern in turbo. requests: {t: mode} requested from clock t on.
    late: turbo enables that ran while a turbo->exact switch was due."""
    requests = requests or {}
    reg = registered(native, init)
    mode = want = mode0
    start = F if mode0 == 'exact' else 0
    ens, late = [], 0
    sw = island.sw
    for t in range(len(native)):
        want = requests.get(t, want)
        fall = t > 0 and reg[t - 1] == 1 and reg[t] == 0
        if mode == 'turbo':
            if t >= start and turbo_en(t):
                if want == 'exact' and island.due:
                    late += 1
                island.sample(t, island.shown_turbo(fall, reg[t]))
                ens.append(t)
                ok = {'nowait': True, 'carry': True, 'two': True, 'seen1': True,
                      'tclear': True, 'force0': island.force == 0}.get(sw, island.live)
                island.due = want == 'exact'
                if want == 'exact' and ok:
                    mode = 'exact'
                    island.due = False
                    island.hold = 0
                    if sw in ('carry', 'two', 'seen1', 'tclear'):
                        island.carry = True
                        island.carry_n = 0
                    else:
                        island.fell = island.force = 0
                    c = t // F
                    start = (c + 1) * F if t % F < 28 else (c + 2) * F
            else:
                island.idle(fall)
        elif t >= start and t % F == 0:
            island.sample(t, island.shown_exact(fall, native[t - 1]))  # B0 at R
            ens.append(t)
            if want == 'turbo':
                mode = 'turbo'
                island.carry = False
                if sw not in ('carry', 'two', 'seen1'):
                    island.fell = island.force = 0
                island.hold = 0
                if sw == 'seen1':
                    island.seen = 1
                start = t + 5
        elif island.carry:
            island.idle(fall)
        else:
            island.fell = island.force = 0
    return island.edges, ens, late



def stock(native, halted, last0):
    """NMIs a 6510 / exact mode takes: one sample per native cycle at R.
    halted(c): RDY holds the CPU in cycle c; a second edge merges."""
    last, pend, taken = last0, 0, 0
    for c in range(len(native) // F):
        v = native[c * F + F - 1]
        if last == 1 and v == 0 and not pend:
            taken += 1
            pend = 1
        last = v
        if not halted(c):
            pend = 0
    return taken


def stall_en(lo, hi):
    return lambda t: not (lo < t < hi)


def stall_halt(lo, hi):
    return lambda c: lo < c * F and (c + 1) * F <= hi


def case(name, changes, init, cols, turbo_en, halted, T=80 * F,
         requests=None, mode0='turbo'):
    native = wave(changes, init, T)
    out = []
    for label, v, sw in cols:
        isl = Island(v, init, sw)
        edges, _, late = run(isl, native, init, turbo_en, requests, mode0)
        out.append(f"{label}={len(edges)}" + (f" L{late}" if requests else ''))
    out.append(f"stock={stock(native, halted, init)}")
    print(f"{name:50s} " + ' '.join(out))


BASE = (('none', 'none', 'wait'), ('latch', 'latch', 'wait'),
        ('rearm', 'rearm', 'carry'))

print("NMIs taken (T65 edges). none = no latch, latch = v3 draft set/clear latch,")
print("rearm = design rule, stock = 6510 / exact mode with halt merging.")
print("Switch rules: carry = design rule, wait = the rule it replaced.")
print("L = turbo enables that ran while a turbo->exact switch was due.")
print()
print("Directed cases")
H = (10 * F, 50 * F)  # 40-cycle BA halt
case("S0 1-cycle pulse in halt, last sample high",
     [(e0(20), 0), (e0(21), 1)], 1, BASE, stall_en(*H), stall_halt(*H))
case("S1 last low; rise, fall in halt",
     [(e0(20), 1), (e0(30), 0)], 0, BASE, stall_en(*H), stall_halt(*H))
case("S1b last low; rise, 1-cycle low, rise in halt",
     [(e0(20), 1), (e0(30), 0), (e0(31), 1)], 0, BASE, stall_en(*H), stall_halt(*H))

# CIA2: the handler's LDA $DD0D loads at e_15 of cycle X (BA high) and retires
# at R(X). CIA2 releases irq_n at e_0(X+1) (mos6526.v:518-526); a refresh-cycle
# badline drops BA at the same edge, so halt_q withholds the completion enable
# until BA is high again about 40 cycles later. Badline timing off: ACKWAIT
# exit at about end f=13 of X+1.
X = 10
LAST = X * F + 20                 # last enable before the forward
ON = (X + 41) * F + 5             # completion after the BA halt
OFF = (X + 1) * F + 13            # completion after ACKWAIT
cia2 = [(e0(X + 1), 1), (e0(31), 0)]   # second CIA2 source fires in cycle 30
uf = [(e0(X + 1), 1), (e0(X + 2), 0)]  # underflow on the acknowledge edge
case("S2 CIA2 ack, BA halt, second event (timing on)",
     cia2, 0, BASE, stall_en(LAST, ON), stall_halt(X * F + F, ON))
case("S2' same, badline timing off",
     cia2, 0, BASE, stall_en(LAST, OFF), stall_halt(LAST, OFF))
case("S2c underflow on the ack edge (timing on)",
     uf, 0, BASE, stall_en(LAST, ON), stall_halt(X * F + F, ON))
case("S2c' same, badline timing off",
     uf, 0, BASE, stall_en(LAST, OFF), stall_halt(LAST, OFF))
case("S3 two 1-cycle pulses in one halt",
     [(e0(20), 0), (e0(21), 1), (e0(25), 0), (e0(26), 1)], 1, BASE,
     stall_en(*H), stall_halt(*H))
case("S4 plain fall, no stall", [(e0(20), 0)], 1, BASE,
     stall_en(0, 0), stall_halt(0, 0))

SAME = (('rearm', 'rearm', 'carry'), ('setwins', 'setwins', 'carry'),
        ('clearwins', 'clearwins', 'carry'), ('latch', 'latch', 'wait'))
case("S5a fall on an enable's clock, last high",
     [(e0(20), 0)], 1, SAME, stall_en(0, 0), stall_halt(0, 0))
TF = e0(40) + LAG                 # registered fall lands on the first enable
case("S5b last low; rise in halt; fall on resume clock",
     [(e0(20), 1), (e0(40), 0)], 0, SAME, stall_en(10 * F, TF), stall_halt(10 * F, TF))

SW = (('carry', 'rearm', 'carry'), ('two', 'rearm', 'two'),
      ('wait', 'rearm', 'wait'), ('force0', 'rearm', 'force0'),
      ('nowait', 'rearm', 'nowait'))
case("S6 rearm pending at turbo->exact (pulses)",
     [(e0(15), 1), (e0(30), 0), (e0(31), 1)], 0, SW, stall_en(*H), stall_halt(*H),
     requests={20 * F: 'exact'})
SW7 = (('carry', 'rearm', 'carry'), ('seen1', 'rearm', 'seen1'))
case("S7 exact->turbo with B0 low, then S2",
     cia2, 0, SW7, stall_en(X * F, ON), stall_halt(X * F + F, ON),
     requests={X * F: 'turbo'}, mode0='exact')
case("S8 held 0 at turbo->exact, new fall before B0",
     [(e0(20), 0), (e0(21), 1), (e0(51), 0)], 1, SW,
     stall_en(10 * F, 50 * F + 40), stall_halt(10 * F, 50 * F + 40),
     requests={30 * F: 'exact'})

# Guard trip: the last enable before a forwarded $DD00 write (LAST) sampled
# the line low. During the forward a non-CIA2 source (RESTORE once wired, a
# cartridge) releases the line for half a cycle and pulls it low again. The
# write trips the disk guard; B1 captures it at e_1 of the completion cycle,
# so the switch to exact is due at the completion enable (CMP, end f=4).
CMP = (X + 1) * F + 5
NOHALT = stall_halt(0, 0)
case("S9 guard trip, completion enable is case 3",
     [(X * F + 30, 1), ((X + 1) * F, 0)], 0, SW, stall_en(LAST, CMP), NOHALT,
     requests={(X + 1) * F + 3: 'exact'})
case("S9b guard trip, completion is case 2, line high",
     [(X * F + 30, 0), ((X + 1) * F, 1)], 1, SW, stall_en(LAST, CMP), NOHALT,
     requests={(X + 1) * F + 3: 'exact'})
# Forced-exact cause (cartridge attach, loader start) while force_q = 1: S6's
# line; the halt ends with a case-3 enable at 50*F, a forward follows, and the
# cause arrives during that forward. Its completion enable is case 1.
CMP2 = 52 * F + 5
case("S10 forced cause while force_q = 1, line high",
     [(e0(15), 1), (e0(30), 0), (e0(31), 1)], 0, SW,
     lambda t: stall_en(*H)(t) and stall_en(50 * F, CMP2)(t), stall_halt(*H),
     requests={51 * F: 'exact'})
# S6's line plus a fall in the first exact cycle: the forced 0 of the carry is
# shown while the line is high again, so the next fall needs a further rearm.
case("S11 S6, then a fall in the first exact cycle",
     [(e0(15), 1), (e0(30), 0), (e0(31), 1), (e0(51), 0)], 0, SW,
     stall_en(*H), stall_halt(*H), requests={20 * F: 'exact'})
# S8's line, a 1-cycle rise after the new fall, and exact->turbo requested at
# the first exact enable, while the carry's force_q = 1.
case("S12 S8, exact->turbo while force_q = 1",
     [(e0(20), 0), (e0(21), 1), (e0(51), 0), (e0(52), 1)], 1,
     (('carry', 'rearm', 'carry'), ('tclear', 'rearm', 'tclear'),
      ('wait', 'rearm', 'wait')),
     stall_en(10 * F, 50 * F + 40), stall_halt(10 * F, 50 * F + 40),
     requests={30 * F: 'exact', 52 * F: 'turbo'})


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


def fuzz_turbo(variant, n=3000, seed=1):
    """Turbo only; native changes on random c64 edges (any even clock)."""
    rng = random.Random(seed)
    tot = [0, 0, 0, 0]
    for _ in range(n):
        T = 40 * F
        init = rng.randint(0, 1)
        ts = sorted(rng.sample(range(2, T - 4 * F, 2), rng.randint(1, 6)))
        ch, v = [], init
        for t in ts:
            v ^= 1
            ch.append((t, v))
        native = wave(ch, init, T)
        en = random_enables(rng, T)
        edges, ens, _ = run(Island(variant, init), native, init, en.__contains__)
        for i, x in enumerate(check(edges, ens, registered(native, init))):
            tot[i] += x
    return tot


def fuzz_modes(variant, switch, n=2000, seed=2):
    """Random exact/turbo switches; native changes at e_0, at most one per
    native cycle (as CIA2), so exact mode alone never misses a fall."""
    rng = random.Random(seed)
    tot = [0, 0, 0, 0, 0, 0]
    for _ in range(n):
        T = 60 * F
        init = rng.randint(0, 1)
        cyc = sorted(rng.sample(range(1, 55), rng.randint(1, 8)))
        ch, v = [], init
        for c in cyc:
            v ^= 1
            ch.append((e0(c), v))
        native = wave(ch, init, T)
        mode0 = rng.choice(('exact', 'turbo'))
        req, m = {}, mode0
        for t in sorted(rng.sample(range(F, T - 4 * F), rng.randint(1, 4))):
            m = 'turbo' if m == 'exact' else 'exact'
            req[t] = m
        en = random_enables(rng, T)
        isl = Island(variant, init, switch)
        edges, ens, late = run(isl, native, init, en.__contains__, req, mode0)
        for i, x in enumerate(check(edges, ens, registered(native, init))):
            tot[i] += x
        tot[4] += late
        tot[5] = max(tot[5], isl.carry_max)
    return tot


def fuzz_trip(switch, n=3000, seed=3):
    """One forced turbo->exact switch due at a random clock r. Before r - 4
    the line changes on random even clocks (RESTORE, cartridge pulses); from r
    on, at e_0 at most once per native cycle, so exact mode alone never misses
    a fall."""
    rng = random.Random(seed)
    tot = [0, 0, 0, 0, 0, 0]
    for _ in range(n):
        T = 40 * F
        init = rng.randint(0, 1)
        r = rng.randrange(8 * F, 30 * F)
        ts = sorted(rng.sample(range(2, r - 4, 2), rng.randint(0, 6)))
        ts += [e0(c) for c in sorted(rng.sample(range(r // F + 1, T // F - 2),
                                                rng.randint(0, 4)))]
        ch, v = [], init
        for t in ts:
            v ^= 1
            ch.append((t, v))
        native = wave(ch, init, T)
        en = random_enables(rng, T)
        isl = Island('rearm', init, switch)
        edges, ens, late = run(isl, native, init, en.__contains__, {r: 'exact'})
        for i, x in enumerate(check(edges, ens, registered(native, init))):
            tot[i] += x
        tot[4] += late
        tot[5] = max(tot[5], isl.carry_max)
    return tot


print()
print("Fuzz, turbo only: 3000 runs, seed 1 (missed = fall not taken within two")
print("enables; extra = NMI with no fall; later = taken one enable later)")
for v in ('none', 'latch', 'rearm', 'setwins', 'clearwins'):
    m, x, l, nf = fuzz_turbo(v)
    print(f"  {v:10s} falls={nf} missed={m} extra={x} later={l}")
print()
print("Fuzz, exact/turbo switches: 2000 runs, seed 2 (L = turbo enables that")
print("ran while a turbo->exact switch was due; C = most exact enables that")
print("one carry took from the rule)")
for label, v, sw in (('latch', 'latch', 'wait'), ('carry', 'rearm', 'carry'),
                     ('two', 'rearm', 'two'), ('wait', 'rearm', 'wait'),
                     ('force0', 'rearm', 'force0'), ('nowait', 'rearm', 'nowait'),
                     ('seen1', 'rearm', 'seen1')):
    m, x, l, nf, late, cmax = fuzz_modes(v, sw)
    print(f"  {label:10s} falls={nf} missed={m} extra={x} later={l} L={late} C={cmax}")
print()
print("Fuzz, forced switch after any-clock pulses: 3000 runs, seed 3")
for sw in ('carry', 'two', 'wait', 'force0', 'nowait'):
    m, x, l, nf, late, cmax = fuzz_trip(sw)
    print(f"  {sw:10s} falls={nf} missed={m} extra={x} later={l} L={late} C={cmax}")
```

Output:

```text
NMIs taken (T65 edges). none = no latch, latch = v3 draft set/clear latch,
rearm = design rule, stock = 6510 / exact mode with halt merging.
Switch rules: carry = design rule, wait = the rule it replaced.
L = turbo enables that ran while a turbo->exact switch was due.

Directed cases
S0 1-cycle pulse in halt, last sample high         none=0 latch=1 rearm=1 stock=1
S1 last low; rise, fall in halt                    none=0 latch=0 rearm=1 stock=1
S1b last low; rise, 1-cycle low, rise in halt      none=0 latch=0 rearm=1 stock=1
S2 CIA2 ack, BA halt, second event (timing on)     none=0 latch=0 rearm=1 stock=1
S2' same, badline timing off                       none=1 latch=1 rearm=1 stock=1
S2c underflow on the ack edge (timing on)          none=0 latch=0 rearm=1 stock=1
S2c' same, badline timing off                      none=1 latch=1 rearm=1 stock=1
S3 two 1-cycle pulses in one halt                  none=0 latch=1 rearm=1 stock=1
S4 plain fall, no stall                            none=1 latch=1 rearm=1 stock=1
S5a fall on an enable's clock, last high           rearm=1 setwins=2 clearwins=1 latch=1 stock=1
S5b last low; rise in halt; fall on resume clock   rearm=1 setwins=1 clearwins=0 latch=0 stock=1
S6 rearm pending at turbo->exact (pulses)          carry=1 L0 two=1 L0 wait=1 L2 force0=1 L1 nowait=0 L0 stock=1
S7 exact->turbo with B0 low, then S2               carry=1 L0 seen1=0 L0 stock=1
S8 held 0 at turbo->exact, new fall before B0      carry=2 L0 two=2 L0 wait=2 L1 force0=1 L0 nowait=1 L0 stock=2
S9 guard trip, completion enable is case 3         carry=1 L0 two=1 L0 wait=1 L1 force0=1 L1 nowait=1 L0 stock=1
S9b guard trip, completion is case 2, line high    carry=1 L0 two=1 L0 wait=1 L1 force0=1 L0 nowait=1 L0 stock=1
S10 forced cause while force_q = 1, line high      carry=1 L0 two=1 L0 wait=1 L1 force0=1 L0 nowait=1 L0 stock=1
S11 S6, then a fall in the first exact cycle       carry=2 L0 two=1 L0 wait=2 L2 force0=2 L1 nowait=1 L0 stock=2
S12 S8, exact->turbo while force_q = 1             carry=2 L0 tclear=1 L0 wait=2 L1 stock=2

Fuzz, turbo only: 3000 runs, seed 1 (missed = fall not taken within two
enables; extra = NMI with no fall; later = taken one enable later)
  none       falls=5239 missed=1269 extra=0 later=53
  latch      falls=5239 missed=737 extra=0 later=0
  rearm      falls=5239 missed=0 extra=0 later=733
  setwins    falls=5239 missed=6 extra=82 later=725
  clearwins  falls=5239 missed=6 extra=0 later=727

Fuzz, exact/turbo switches: 2000 runs, seed 2 (L = turbo enables that
ran while a turbo->exact switch was due; C = most exact enables that
one carry took from the rule)
  latch      falls=4573 missed=287 extra=0 later=0 L=349 C=0
  carry      falls=4573 missed=0 extra=0 later=284 L=0 C=2
  two        falls=4573 missed=0 extra=0 later=284 L=0 C=2
  wait       falls=4573 missed=0 extra=0 later=285 L=110 C=0
  force0     falls=4573 missed=2 extra=0 later=281 L=49 C=0
  nowait     falls=4573 missed=6 extra=0 later=277 L=0 C=0
  seen1      falls=4573 missed=19 extra=0 later=265 L=0 C=2

Fuzz, forced switch after any-clock pulses: 3000 runs, seed 3
  carry      falls=7541 missed=0 extra=0 later=1236 L=0 C=3
  two        falls=7541 missed=4 extra=0 later=1232 L=0 C=2
  wait       falls=7541 missed=0 extra=0 later=1236 L=457 C=0
  force0     falls=7541 missed=18 extra=0 later=1218 L=214 C=0
  nowait     falls=7541 missed=74 extra=0 later=1158 L=0 C=0
```

## vic_t2_sticky.py

T2 sticky-state check: DUT vs collapsed reference vs per-event scoreboard vs stock with separate-cycle writes.

```python
# T2 reference check for the VIC's sticky state (design stage only).
# Rules from hdl/core/c64/ports/video_vicII_656x.vhd, evaluated on every
# c64_clk edge with pre-edge register values:
#   rasterEnable (617-623): rasterY = 48 and DEN = 1 -> 1; rasterY = 248 -> 0
#   IRST / rasterIrqDone (899-907): ack pulse -> IRST := 0; then
#       rasterY /= rasterCmp -> done := 0
#       elif done = 0        -> done := 1, IRST := 1   (set wins over the ack)
#   $D019 write (1584, 1692): resetRasterIrq := di(0) for one clock (a pulse)
#   $D019 read (1799): IRQ & "111" & ILP & IMMC & IMBC & IRST, so with only
#       IRST pending an RMW reads $F1 and its dummy write is $F1.
# Edge numbering: E = 32 * cycle + q, where e_q ends phase q of that cycle.
# A landing at edge E changes the register at E; the rules first see it at E+1.
#
# DUT:   every fast landing at its own edge (landing rule 1).
# REF:   the collapsed T2 reference. A landing at e_q of cycle k maps to stock
#        cycle k-1 if q <= 14, else k; the last value per register per mapped
#        cycle wins. Stock strobes: $D011/$D012 land at e_3 of the next cycle
#        (myWr_b), $D019 at e_31 of the write cycle (myWr_a).
# SB:    the per-event scoreboard. Each landed value, in landing order, is
#        seen by the rules once (DEN and rasterCmp values; $D019 ack masks).
# STOCK: a stock C64 issuing the same writes in separate cycles: the first in
#        the mapped cycle of the first landing, each later one a cycle later.
# The whole run stays on raster line 48 (rasterY fixed).

RASTER_Y = 48
CYCLES = 20
K = 10  # native cycle that holds the fast landings


def rules(st, pulse):
    """One c64 edge of the sticky rules on pre-edge state st; returns new bits."""
    ren = st['ren']
    if RASTER_Y == 48 and st['den']:
        ren = 1
    if RASTER_Y == 248:
        ren = 0
    irst, done = st['irst'], st['done']
    if pulse:
        irst = 0
    if RASTER_Y != st['cmp']:
        done = 0
    elif not st['done']:
        done, irst = 1, 1
    return ren, irst, done


def run_edges(landings, init):
    """landings: {edge: (reg, value)}; reg in DEN, CMP, ACK. Returns the final
    (rasterEnable, IRST) and the first cycle whose e_15 shows each bit changed
    from its initial value."""
    st = dict(init)
    pulse = 0
    first = {'ren': None, 'irst': None}
    for e in range(32 * CYCLES):
        ren, irst, done = rules(st, pulse)
        pulse = 0
        if e in landings:
            reg, val = landings[e]
            if reg == 'DEN':
                st['den'] = val
            elif reg == 'CMP':
                st['cmp'] = val
            else:
                pulse = val & 1
        st['ren'], st['irst'], st['done'] = ren, irst, done
        if e % 32 == 15:
            for b in ('ren', 'irst'):
                if st[b] != init[b] and first[b] is None:
                    first[b] = e // 32
    return (st['ren'], st['irst']), first


def stock_edge(cycle, reg):
    return 32 * cycle + 31 if reg == 'ACK' else 32 * (cycle + 1) + 3


def mapped_cycle(edge):
    k, q = divmod(edge, 32)
    return k - 1 if q <= 14 else k


def ref_collapse(fast):
    last = {}
    for e, reg, val in fast:
        last[(mapped_cycle(e), reg)] = val
    return {stock_edge(j, reg): (reg, val) for (j, reg), val in last.items()}


def stock_separate(fast):
    j0 = mapped_cycle(fast[0][0])
    return {stock_edge(j0 + i, reg): (reg, val)
            for i, (e, reg, val) in enumerate(fast)}


def scoreboard(fast, init):
    st = dict(init)
    ren, irst, done = rules(st, 0)
    st['ren'], st['irst'], st['done'] = ren, irst, done
    for e, reg, val in sorted(fast):
        pulse = 0
        if reg == 'DEN':
            st['den'] = val
        elif reg == 'CMP':
            st['cmp'] = val
        else:
            pulse = val & 1
        ren, irst, done = rules(st, pulse)
        st['ren'], st['irst'], st['done'] = ren, irst, done
    ren, irst, done = rules(st, 0)
    return (ren, irst)


def case(name, fast, **kw):
    init = dict(den=0, cmp=100, done=0, irst=0, ren=0)
    init.update(kw)
    dut, f_dut = run_edges({e: (r, v) for e, r, v in fast}, init)
    ref, f_ref = run_edges(ref_collapse(fast), init)
    stk, f_stk = run_edges(stock_separate(fast), init)
    sb = scoreboard(fast, init)
    print(name)
    print("  final (rasterEnable, IRST):    DUT %s  REF %s  SB %s  STOCK %s"
          % (dut, ref, sb, stk))
    print("  first e_15 change (ren, irst): DUT %s  REF %s  STOCK %s"
          % ((f_dut['ren'], f_dut['irst']), (f_ref['ren'], f_ref['irst']),
             (f_stk['ren'], f_stk['irst'])))
    print("  REF vs DUT %s; SB vs DUT %s; DUT vs STOCK %s"
          % ("match" if ref == dut else "MISMATCH",
             "match" if sb == dut else "MISMATCH",
             "match" if dut == stk else "MISMATCH"))


E = 32 * K
print("All runs on raster line 48; landings in native cycle %d; e_15 compare." % K)
case("A  DEN pulse: $D011 DEN=1 at e_2, DEN=0 at e_3",
     [(E + 2, 'DEN', 1), (E + 3, 'DEN', 0)])
case("A2 DEN pulse: $D011 DEN=1 at e_2, DEN=0 at e_5",
     [(E + 2, 'DEN', 1), (E + 5, 'DEN', 0)])
case("B  temporary match: $D012=48 at e_2, $D012=100 at e_3",
     [(E + 2, 'CMP', 48), (E + 3, 'CMP', 100)])
case("C  INC $D012 (read 48): dummy 48 at e_4, final 49 at e_5",
     [(E + 4, 'CMP', 48), (E + 5, 'CMP', 49)])
case("D  re-arm: compare 48 matched and acked; $D012=49 at e_2, 48 at e_3",
     [(E + 2, 'CMP', 49), (E + 3, 'CMP', 48)], cmp=48, done=1)
case("E  DEC $D019, IRST pending: $F1 at e_2, $F0 at e_7",
     [(E + 2, 'ACK', 0xF1), (E + 7, 'ACK', 0xF0)], cmp=48, done=1, irst=1)
case("F  LSR $D019, IRST pending: $F1 at e_2, $78 at e_7",
     [(E + 2, 'ACK', 0xF1), (E + 7, 'ACK', 0x78)], cmp=48, done=1, irst=1)
case("G  ASL $D019, IRST pending: $F1 at e_2, $E2 at e_7",
     [(E + 2, 'ACK', 0xF1), (E + 7, 'ACK', 0xE2)], cmp=48, done=1, irst=1)
case("H  control: one $D011 DEN=1 landing at e_2",
     [(E + 2, 'DEN', 1)])
case("I  control: one $D012=48 landing at e_2",
     [(E + 2, 'CMP', 48)])
case("J  control: one STA $D019 #$01 landing at e_2, IRST pending",
     [(E + 2, 'ACK', 0x01)], cmp=48, done=1, irst=1)
```

Output:

```text
All runs on raster line 48; landings in native cycle 10; e_15 compare.
A  DEN pulse: $D011 DEN=1 at e_2, DEN=0 at e_3
  final (rasterEnable, IRST):    DUT (1, 0)  REF (0, 0)  SB (1, 0)  STOCK (1, 0)
  first e_15 change (ren, irst): DUT (10, None)  REF (None, None)  STOCK (10, None)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
A2 DEN pulse: $D011 DEN=1 at e_2, DEN=0 at e_5
  final (rasterEnable, IRST):    DUT (1, 0)  REF (0, 0)  SB (1, 0)  STOCK (1, 0)
  first e_15 change (ren, irst): DUT (10, None)  REF (None, None)  STOCK (10, None)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
B  temporary match: $D012=48 at e_2, $D012=100 at e_3
  final (rasterEnable, IRST):    DUT (0, 1)  REF (0, 0)  SB (0, 1)  STOCK (0, 1)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 10)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
C  INC $D012 (read 48): dummy 48 at e_4, final 49 at e_5
  final (rasterEnable, IRST):    DUT (0, 1)  REF (0, 0)  SB (0, 1)  STOCK (0, 1)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 10)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
D  re-arm: compare 48 matched and acked; $D012=49 at e_2, 48 at e_3
  final (rasterEnable, IRST):    DUT (0, 1)  REF (0, 0)  SB (0, 1)  STOCK (0, 1)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 11)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
E  DEC $D019, IRST pending: $F1 at e_2, $F0 at e_7
  final (rasterEnable, IRST):    DUT (0, 0)  REF (0, 1)  SB (0, 0)  STOCK (0, 0)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 10)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
F  LSR $D019, IRST pending: $F1 at e_2, $78 at e_7
  final (rasterEnable, IRST):    DUT (0, 0)  REF (0, 1)  SB (0, 0)  STOCK (0, 0)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 10)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
G  ASL $D019, IRST pending: $F1 at e_2, $E2 at e_7
  final (rasterEnable, IRST):    DUT (0, 0)  REF (0, 1)  SB (0, 0)  STOCK (0, 0)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, None)  STOCK (None, 10)
  REF vs DUT MISMATCH; SB vs DUT match; DUT vs STOCK match
H  control: one $D011 DEN=1 landing at e_2
  final (rasterEnable, IRST):    DUT (1, 0)  REF (1, 0)  SB (1, 0)  STOCK (1, 0)
  first e_15 change (ren, irst): DUT (10, None)  REF (10, None)  STOCK (10, None)
  REF vs DUT match; SB vs DUT match; DUT vs STOCK match
I  control: one $D012=48 landing at e_2
  final (rasterEnable, IRST):    DUT (0, 1)  REF (0, 1)  SB (0, 1)  STOCK (0, 1)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, 10)  STOCK (None, 10)
  REF vs DUT match; SB vs DUT match; DUT vs STOCK match
J  control: one STA $D019 #$01 landing at e_2, IRST pending
  final (rasterEnable, IRST):    DUT (0, 0)  REF (0, 0)  SB (0, 0)  STOCK (0, 0)
  first e_15 change (ren, irst): DUT (None, 10)  REF (None, 10)  STOCK (None, 10)
  REF vs DUT match; SB vs DUT match; DUT vs STOCK match
```

## sid_edge_stage.py

Literal edge-stage model: kept and merged gate/test pulse cases with expected engine samples.

```python
# SID edge stage: literal model of the EDGE_STAGE pseudocode in
# C64_TURBO_DESIGN.md 2.13.7 (same text as C64_TURBO_FAST_SID.md 3.4).
# One voice control register r ($x04/$x0B/$x12) of one chip.
#
# Events, one per c64 edge, in order:
#   CE          ce_1m edge with no fast write
#   ('ce', d)   ce_1m edge with a fast write of d landing on it
#   ('w', d)    fast write of d landing inside the window (not on ce_1m)
#   ('nat', d)  native write (exact mode or REU DMA); the stock branch writes r
#   ('rst',)    reset (the stock reset also clears r, sid_top.sv:180-199)
# At each ce_1m edge the engine samples r BEFORE the edge updates it.
# The printed line lists those engine samples in order: the gate bit
# (bit 0) or, for byte cases, the whole register.

CE = ('ce',)


class EdgeStage:
    def __init__(self, r0, enabled=True):
        self.r, self.stage, self.pend, self.flag = r0, 0, 0, 0
        self.enabled = enabled
        self.samples = []

    def edge(self, ev):
        kind = ev[0]
        w = ev[1] if (kind == 'w' or (kind == 'ce' and len(ev) == 2)) else None
        if kind == 'ce':
            self.samples.append(self.r)          # engine samples the old r
        if not self.enabled:                     # EDGE_STAGE = 0: level sampling
            if kind == 'rst':
                self.r = 0
            elif kind in ('w', 'nat') or w is not None:
                self.r = ev[1]
            return
        if kind == 'rst':
            self.pend, self.flag = 0, 0
            self.r = 0
        elif kind == 'nat':
            self.pend, self.flag = 0, 0
            self.r = ev[1]
        elif kind == 'ce':
            if self.pend and w is not None:
                self.r, self.stage, self.pend, self.flag = self.stage, w, 1, 1
            elif self.pend:
                self.r, self.pend, self.flag = self.stage, 0, 1
            elif w is not None:
                self.r, self.pend, self.flag = w, 0, 1
            else:
                self.pend, self.flag = 0, 0
        elif kind == 'w':
            if self.flag:
                self.stage, self.pend = w, 1
            else:
                self.r, self.flag = w, 1


def run(name, r0, events, byte=False, enabled=True):
    s = EdgeStage(r0, enabled)
    for ev in events:
        s.edge(ev)
    fmt = (lambda v: '$%02X' % v) if byte else (lambda v: str(v & 1))
    print('%-58s %s' % (name, ' '.join(fmt(v) for v in s.samples)))


ON, OFF = 0x41, 0x40          # pulse waveform, gate on / off
T1, T0 = 0x49, 0x41           # test bit (bit 3) set / clear, gate on

print('Engine samples at successive ce_1m edges (first = edge before the first write)')
print()
print('Kept (window starts without applying a stage):')
run('K1 one window 1->0->1', ON,
    [CE, ('w', OFF), ('w', ON), CE, CE, CE])
run('K2 one window 0->1->0', OFF,
    [CE, ('w', ON), ('w', OFF), CE, CE, CE])
run('K3 one window test 0->1->0', T0,
    [CE, ('w', T1), ('w', T0), CE, CE, CE], byte=True)
run('K4 one window $41,$40,$21 (three writes)', OFF,
    [CE, ('w', 0x41), ('w', 0x40), ('w', 0x21), CE, CE, CE], byte=True)
run('K5 pulse window, then a window with one write ($40)', ON,
    [CE, ('w', OFF), ('w', ON), CE, ('w', OFF), CE, CE, CE])
run('K6 pulse window, then $40 on the ce_1m edge alone', ON,
    [CE, ('w', OFF), ('w', ON), ('ce', OFF), CE, CE, CE])
run('K7 two pulse windows with an empty window between', ON,
    [CE, ('w', OFF), ('w', ON), CE, CE, ('w', OFF), ('w', ON), CE, CE, CE])
print()
print('Merged (window starts by applying a stage; latest write wins):')
run('M1 two consecutive windows, each 1->0->1', ON,
    [CE, ('w', OFF), ('w', ON), CE, ('w', OFF), ('w', ON), CE, CE, CE])
run('M2 two consecutive windows, each test 0->1->0', T0,
    [CE, ('w', T1), ('w', T0), CE, ('w', T1), ('w', T0), CE, CE, CE], byte=True)
run('M3 pulse window, then $40 on the ce_1m edge, then $41', ON,
    [CE, ('w', OFF), ('w', ON), ('ce', OFF), ('w', ON), CE, CE, CE])
run('M4 pulse window, one-write window, then a 0->1->0 window', ON,
    [CE, ('w', OFF), ('w', ON), CE, ('w', OFF), CE, ('w', ON), ('w', OFF), CE, CE, CE])
print()
print('Cancel, reset and EDGE_STAGE = 0:')
run('C1 pulse window, then a native write $40 before ce_1m', ON,
    [CE, ('w', OFF), ('w', ON), ('nat', OFF), CE, CE, CE], byte=True)
run('C2 pulse window, then reset before ce_1m', ON,
    [CE, ('w', OFF), ('w', ON), ('rst',), CE, CE, CE], byte=True)
run('C3 EDGE_STAGE = 0, one window 1->0->1', ON,
    [CE, ('w', OFF), ('w', ON), CE, CE, CE], enabled=False)
print()
print('Reference, 1 MHz code (one native write per cycle, lands at e_29):')
run('R1 1->0->1->0->1', ON,
    [CE, ('nat', OFF), CE, ('nat', ON), CE, ('nat', OFF), CE, ('nat', ON), CE, CE])
```

Output:

```text
Engine samples at successive ce_1m edges (first = edge before the first write)

Kept (window starts without applying a stage):
K1 one window 1->0->1                                      1 0 1 1
K2 one window 0->1->0                                      0 1 0 0
K3 one window test 0->1->0                                 $41 $49 $41 $41
K4 one window $41,$40,$21 (three writes)                   $40 $41 $21 $21
K5 pulse window, then a window with one write ($40)        1 0 1 0 0
K6 pulse window, then $40 on the ce_1m edge alone          1 0 1 0 0
K7 two pulse windows with an empty window between          1 0 1 0 1 1

Merged (window starts by applying a stage; latest write wins):
M1 two consecutive windows, each 1->0->1                   1 0 1 1 1
M2 two consecutive windows, each test 0->1->0              $41 $49 $41 $41 $41
M3 pulse window, then $40 on the ce_1m edge, then $41      1 0 1 1 1
M4 pulse window, one-write window, then a 0->1->0 window   1 0 1 0 0 0

Cancel, reset and EDGE_STAGE = 0:
C1 pulse window, then a native write $40 before ce_1m      $41 $40 $40 $40
C2 pulse window, then reset before ce_1m                   $41 $00 $00 $00
C3 EDGE_STAGE = 0, one window 1->0->1                      1 1 1 1

Reference, 1 MHz code (one native write per cycle, lands at e_29):
R1 1->0->1->0->1                                           1 0 1 0 1 1
```

## svf_persistence.py

How long a one-cycle F0, routing or resonance difference lasts in the SID filter integrators.

```python
# How long a one-cycle filter difference lasts in the SID filter.
# Fixed-point model of the state-variable filter in
# hdl/core/c64/ports/sid_filter.sv, states 2-5 (one step per native cycle):
#   vlp = clamp18(vlp - (F0*vbp >> 17))
#   vbp = clamp18(vbp - (F0*vhp >> 17))
#   vhp = clamp18((1/Q<<10 * vbp - ((vlp + vi) << 10)) >> 10)
# with 18-bit integrators vlp/vbp/vhp (sid_filter.sv:91-93), F0 = w0*T << 17
# and 1/Q << 10 from the 6581 table (sid_filter.sv:70-74).
# Two runs differ only during ONE native cycle: a filter register write that
# the filter samples one cycle earlier (or later) than stock. The input is a
# square wave of about 440 Hz on one routed voice.
# For each case the script prints, over four start points:
#   big  = last native cycle (after the difference) at which some integrator
#          differs by more than 4 state LSB (1 LSB of the 16-bit tap output);
#   zero = native cycles until the states are equal again ("never" if they
#          are still unequal after LIMIT cycles: a fixed-point limit cycle
#          keeps a residue of a few LSB).
# Once the states are equal they stay equal, because both runs see the same
# input from then on.
import math

Q_RES0, Q_RES15 = 1448, 600          # 6581 1/Q<<10 at resonance 0 and 15
LO, HI = -(1 << 17), (1 << 17) - 1
LIMIT = 100000
SMALL = 4


def clamp18(x):
    return LO if x < LO else HI if x > HI else x


def step(st, f0, q, vi):
    vlp, vbp, vhp = st
    vlp = clamp18(vlp + ((-(f0 * vbp)) >> 17))
    vbp = clamp18(vbp + ((-(f0 * vhp)) >> 17))
    vhp = clamp18(((-(vlp + vi)) * 1024 + q * vbp) >> 10)
    return (vlp, vbp, vhp)


def voice(t):                        # about 440 Hz square, wide 18-bit scale
    return 20000 if (t // 1136) % 2 == 0 else -20000


def f0_of(fc):
    return int(2 * math.pi * fc / 1e6 * (1 << 17))


def persistence(fc, kind, t0):
    f0 = f0_of(fc)
    st = (0, 0, 0)
    for t in range(t0):              # settle on the input
        st = step(st, f0, Q_RES0, voice(t))
    a = b = st
    big = 0
    for t in range(t0, t0 + LIMIT):
        fa, qa, route_a = f0, Q_RES0, True
        if t == t0:                  # run A differs for this one cycle
            if kind == 'F0':
                fa = f0 + 200
            elif kind == 'route':
                route_a = False
            elif kind == '1/Q':
                qa = Q_RES15
        a = step(a, fa, qa, voice(t) if route_a else 0)
        b = step(b, f0, Q_RES0, voice(t))
        if a == b:
            return big, t - t0
        if max(abs(x - y) for x, y in zip(a, b)) > SMALL:
            big = t - t0
    return big, None


T0S = (50000, 50568, 51136, 51704)    # four start points over one input period
print('One-cycle difference in one filter input; four start points per row')
print('%-40s %-15s %s' % ('case', 'big (min-max)', 'zero'))
for fc in (200, 1000, 4000):
    for kind, label in (('F0', 'F0 +200'), ('route', 'routing bit'),
                        ('1/Q', 'resonance 0 -> 15')):
        r = [persistence(fc, kind, t0) for t0 in T0S]
        bigs = [x[0] for x in r]
        zeros = ' '.join('never' if x[1] is None else str(x[1]) for x in r)
        name = 'Fc ~%d Hz (F0 %d), %s' % (fc, f0_of(fc), label)
        print('%-40s %6d - %-6d %s' % (name, min(bigs), max(bigs), zeros))
print('Mode_Vol ($18) feeds only the tap mix, the direct path and the volume')
print('(sid_filter.sv:116-125, 155-160, 210), never vlp/vbp/vhp, so a $18')
print('difference changes one output sample only.')
```

Output:

```text
One-cycle difference in one filter input; four start points per row
case                                     big (min-max)   zero
Fc ~200 Hz (F0 164), F0 +200               4227 - 5194   never never never never
Fc ~200 Hz (F0 164), routing bit           2651 - 3659   5492 never never never
Fc ~200 Hz (F0 164), resonance 0 -> 15      855 - 3659   6005 never never 3184
Fc ~1000 Hz (F0 823), F0 +200               184 - 752    1439 871 1584 1016
Fc ~1000 Hz (F0 823), routing bit           803 - 1130   1652 1756 925 2152
Fc ~1000 Hz (F0 823), resonance 0 -> 15     200 - 739    1652 1084 1487 1016
Fc ~4000 Hz (F0 3294), F0 +200                0 - 204    269 0 269 569
Fc ~4000 Hz (F0 3294), routing bit          263 - 637    359 737 421 927
Fc ~4000 Hz (F0 3294), resonance 0 -> 15      0 - 254    398 1 288 1
Mode_Vol ($18) feeds only the tap mix, the direct path and the volume
(sid_filter.sv:116-125, 155-160, 210), never vlp/vbp/vhp, so a $18
difference changes one output sample only.
```

## preload_cache.py

REU image preload against a write-back PS data cache: old flush-after-copy rule, aligned pre-invalidate and non-cacheable mapping.

```python
# REU image preload and the PS data cache (design section 6.4 (a), (b)).
# Design-stage model, standard library only, deterministic.
#
# Models a PS write-back, write-allocate data cache with 32-byte lines over
# the REU DDR aperture, which REU DMA writes directly through HP2 (the HP
# ports do not snoop the PS caches). Dirty state is kept per line, so the
# stale-byte counts are an upper bound: a cache that tracks dirty state at a
# finer grain writes back fewer bytes.
#
# Rules compared:
#   old      cacheable aperture; :reudump invalidates, then reads (lines stay
#            resident); the preload writes each block, then flushes it
#            (Xil_DCacheFlushRange = clean + invalidate of covering lines).
#   preinval cacheable aperture; as "old", plus an invalidate (no clean) of
#            the 32-byte-aligned covering range before the copy (fallback).
#   noncache aperture mapped Normal non-cacheable: every PS read and write
#            goes to DDR; only the bytes written change. No cache maintenance.

LINE = 32
BLOCK = 4096


class Aperture:
    def __init__(self, size, rule):
        self.ddr = bytearray(size)
        self.lines = {}            # line base -> [bytearray, dirty]
        self.rule = rule

    def _line(self, a):
        b = a - a % LINE
        if b not in self.lines:
            self.lines[b] = [bytearray(self.ddr[b:b + LINE]), False]
        return self.lines[b]

    def cpu_read(self, a):
        if self.rule == "noncache":
            return self.ddr[a]
        return self._line(a)[0][a % LINE]

    def cpu_write(self, a, v):
        if self.rule == "noncache":
            self.ddr[a] = v
            return
        ln = self._line(a)
        ln[0][a % LINE] = v
        ln[1] = True

    def flush(self, a, n):         # clean + invalidate covering lines
        for b in range(a - a % LINE, a + n, LINE):
            ln = self.lines.pop(b, None)
            if ln and ln[1]:
                self.ddr[b:b + LINE] = ln[0]

    def invalidate(self, a, n):    # aligned bounds: drop, no write-back
        for b in range(a - a % LINE, a + n, LINE):
            self.lines.pop(b, None)

    def dma_write(self, a, data):  # REU stash through HP2, straight to DDR
        self.ddr[a:a + len(data)] = data


def reudump(ap, a, n):
    if ap.rule != "noncache":
        ap.invalidate(a, n)        # old rule: invalidate before the read only
    return bytes(ap.cpu_read(x) for x in range(a, a + n))


def preload(ap, off, data):
    if ap.rule == "preinval":
        lo = off - off % LINE
        hi = -(-(off + len(data)) // LINE) * LINE
        ap.invalidate(lo, hi - lo)
    for i in range(0, len(data), BLOCK):
        chunk = data[i:i + BLOCK]
        for j, v in enumerate(chunk):
            ap.cpu_write(off + i + j, v)
        if ap.rule != "noncache":
            ap.flush(off + i, len(chunk))


def file_bytes(n):
    return bytes((7 * i + 3) & 0xFF for i in range(n))


def case(rule, size, area, off, n, dump=True):
    """Fill area with 0x11, optionally :reudump it, stash 0xAA over it with
    REU DMA, then preload n bytes at off. Returns stale bytes before and
    after the loaded range, and whether the loaded range is correct."""
    lo, hi = area
    ap = Aperture(size, rule)
    ap.dma_write(lo, bytes([0x11]) * (hi - lo))
    if dump:
        reudump(ap, lo, hi - lo)
    ap.dma_write(lo, bytes([0xAA]) * (hi - lo))
    data = file_bytes(n)
    preload(ap, off, data)
    before = [a for a in range(lo, off) if ap.ddr[a] != 0xAA]
    after = [a for a in range(off + n, hi) if ap.ddr[a] != 0xAA]
    loaded_ok = bytes(ap.ddr[off:off + n]) == data
    return before, after, loaded_ok


def span(addrs):
    if not addrs:
        return "none"
    return "%d (0x%06X-0x%06X)" % (len(addrs), addrs[0], addrs[-1])


print("Case A: reviewer reproduction. Dump 0x100-0x1FF, stash, 48-byte preload at 0x1F0")
for rule in ("old", "preinval", "noncache"):
    b, a, ok = case(rule, 0x400, (0x100, 0x200), 0x1F0, 48)
    print("  %-8s stale before %s; stale after %s; loaded range correct %s"
          % (rule, span(b), span(a), ok))

print("Case B: X-PRELOAD adjacent-byte case. REU 0x010000-0x011FFF filled with $11,")
print("        :reudump, stashed with $AA, then 4097-byte file at offset 0x010111")
for rule in ("old", "preinval", "noncache"):
    b, a, ok = case(rule, 0x20000, (0x010000, 0x012000), 0x010111, 4097)
    print("  %-8s stale before %s; stale after %s; loaded range correct %s"
          % (rule, span(b), span(a), ok))
b, a, ok = case("old", 0x20000, (0x010000, 0x012000), 0x010111, 4097, dump=False)
print("  old, no :reudump first: stale before %s; stale after %s; loaded range correct %s"
      % (span(b), span(a), ok))

print("Case C: bound over offsets 0x200-0x23F and lengths 1-96, all lines stale")
worst = {}
for rule in ("old", "preinval", "noncache"):
    mb = ma = 0
    aligned_bad = 0
    for off in range(0x200, 0x240):
        for n in range(1, 97):
            b, a, ok = case(rule, 0x400, (0x100, 0x380), off, n)
            assert ok
            mb = max(mb, len(b))
            ma = max(ma, len(a))
            if off % LINE == 0 and n % LINE == 0:
                aligned_bad += len(b) + len(a)
    print("  %-8s max stale before %d; max stale after %d; stale with aligned offset"
          " and length a multiple of 32: %d" % (rule, mb, ma, aligned_bad))
```

Output:

```text
Case A: reviewer reproduction. Dump 0x100-0x1FF, stash, 48-byte preload at 0x1F0
  old      stale before 16 (0x0001E0-0x0001EF); stale after none; loaded range correct True
  preinval stale before none; stale after none; loaded range correct True
  noncache stale before none; stale after none; loaded range correct True
Case B: X-PRELOAD adjacent-byte case. REU 0x010000-0x011FFF filled with $11,
        :reudump, stashed with $AA, then 4097-byte file at offset 0x010111
  old      stale before 17 (0x010100-0x010110); stale after 14 (0x011112-0x01111F); loaded range correct True
  preinval stale before none; stale after none; loaded range correct True
  noncache stale before none; stale after none; loaded range correct True
  old, no :reudump first: stale before none; stale after none; loaded range correct True
Case C: bound over offsets 0x200-0x23F and lengths 1-96, all lines stale
  old      max stale before 31; max stale after 31; stale with aligned offset and length a multiple of 32: 0
  preinval max stale before 0; max stale after 0; stale with aligned offset and length a multiple of 32: 0
  noncache max stale before 0; max stale after 0; stale with aligned offset and length a multiple of 32: 0
```
