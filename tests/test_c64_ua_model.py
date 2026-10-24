#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent arithmetic and voice-contract checks for the UA model."""
import sys
from fractions import Fraction
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from ua_model import Voice, deadline_limit, mix, period_seconds, upstream_periods


class UltimateAudioModelTests(unittest.TestCase):
    def test_period_and_upstream_mode_difference(self):
        for rate in (0,1,130,281,283,781,65535):
            self.assertEqual(period_seconds(rate),Fraction(2*rate+3,12500000))
            self.assertEqual(upstream_periods(100000000,rate,False,0,3),[16*rate+24]*3)
            self.assertEqual(upstream_periods(100000000,rate,True,0,3),[16*rate+32]*3)

    def test_complete_pan_and_volume_law(self):
        for sample in (-32768,-123,0,111,32767):
            for volume in range(64):
                for pan in range(16):
                    scaled=(sample*volume)//32
                    factors=(7,pan) if pan<8 else (15-pan,7)
                    self.assertEqual(mix([sample],[volume],[pan]),tuple(scaled*p//8 for p in factors))
        self.assertEqual(mix([32767]*7,[63]*7,[8]*7),(131071,131071))
        self.assertEqual(mix([-32768]*7,[63]*7,[8]*7),(-131072,-131072))
        self.assertEqual(mix([30000],[64],[0]),(0,0))

    @staticmethod
    def boundary(voice, sample=123):
        voice.next_sample=sample
        result=None
        for _ in range(voice.remaining): result=voice.step(True)
        return result

    def test_stale_loop_end_and_release_are_fixed(self):
        voice=Voice(control=5,length=4,repeat_b=4,rate=1)
        self.assertEqual(voice.step(False),(0,False))
        for address in (1,2,3):self.assertEqual(self.boundary(voice),(address,False))
        self.assertIsNone(self.boundary(voice))
        self.assertEqual((voice.state,voice.irq_set,voice.output),('finished',True,123))
        loop=Voice(control=7,length=4,repeat_a=1,repeat_b=4)
        loop.step(False)
        for _ in range(4):self.boundary(loop)
        self.assertEqual(loop.position,2)
        loop.control=6
        for _ in range(3):self.boundary(loop)
        self.assertEqual((loop.state,loop.irq_set),('finished',True))

    def test_restart_is_state_based(self):
        voice=Voice(control=3,length=20,repeat_b=10)
        voice.step(False)
        self.boundary(voice)
        voice.control=2;voice.step(False)
        voice.control=3;self.assertIsNone(voice.step(False))
        self.assertEqual(voice.position,2)
        voice.control=0;voice.step(False)
        self.assertEqual(voice.state,'idle')
        voice.control=1;self.assertEqual(voice.step(False),(0,False))

    def test_address_wrap_stride_pause_and_underrun(self):
        voice=Voice(control=0x71,start=0xfffffd,length=5,rate=1)
        self.assertEqual(voice.step(False),(0xfffffd,True))
        before=vars(voice).copy()
        for _ in range(100):self.assertIsNone(voice.step(True,paused=True))
        self.assertEqual(vars(voice),before)
        for _ in range(5):result=voice.step(True)
        self.assertEqual(result,(1,True))
        self.assertTrue(voice.underrun)
        self.assertEqual(voice.output,0)
        for _ in range(20):self.boundary(voice,-256)
        self.assertEqual(voice.state,'play') # odd length never equals stride-4 position
        self.assertEqual(voice.output,-256)

    def test_zero_length_is_full_wrap(self):
        voice=Voice(control=5,length=0)
        voice.step(False)
        voice.position=0xffffff
        self.assertEqual(self.boundary(voice),(0xffffff,False))
        self.assertEqual(voice.position,0)
        self.assertIsNone(self.boundary(voice))
        self.assertTrue(voice.irq_set)

    def test_aggregate_deadline_uses_native_32_clocks(self):
        self.assertEqual(deadline_limit(315),72)
        self.assertEqual(deadline_limit(663),122)
        self.assertEqual(deadline_limit(663,words=2),61)
        self.assertEqual(deadline_limit(663,spacing=0,periods=2),189)


if __name__=='__main__':unittest.main()
