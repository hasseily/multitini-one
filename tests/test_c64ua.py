# SPDX-License-Identifier: GPL-3.0-only
"""Assemble original probes and execute their control paths on a 6502 model.

MMIO is a behavioral fixture, not an RTL/physical-board compatibility proof.
"""
from __future__ import annotations
import hashlib,json,os,subprocess,sys,unittest
from pathlib import Path
from py65.devices.mpu6502 import MPU
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/c64ua-tests'

class ProbeMemory:
    def __init__(self,ua=True,turbo=False,stale=False):
        self.ram=bytearray(65536);self.reu=bytearray(1<<24);self.ua=ua;self.turbo=turbo
        self.registers=bytearray(256);self.registers[0x2b]=255 if stale else 0
        self.flags=0;self.status=0;self.time=0.;self.deadline=None;self.commands=[0]*4
        self.ram[0xffd2]=0x60
    def __getitem__(self,a):
        if 0xdf00<=a<=0xdfff:
            if self.turbo:self.time+=1000
            offset=a&255
            if offset>=32 and self.ua:
                if self.deadline is not None and self.time>=self.deadline:self.flags|=1
                return 16 if offset&1 else self.flags
            if offset&31==0:
                value=self.status|16;self.status=0;return value
            return self.registers[offset&31]
        return self.ram[a]
    def __setitem__(self,a,value):
        value&=255
        if not 0xdf00<=a<=0xdfff:self.ram[a]=value;return
        if self.turbo:self.time+=1000
        offset=a&255
        if offset>=32 and self.ua:
            self.registers[offset]=value
            if offset&31==31:
                if value==255:self.flags=0
                elif value&1:self.flags &= ~(1<<((offset>>5)-1))
                if self.flags==0:self.deadline=None
            if offset==32:
                if value&5==5 and not value&2:
                    length=int.from_bytes(self.registers[0x29:0x2c],'big') or 1<<24
                    rate=int.from_bytes(self.registers[0x2e:0x30],'big')
                    stride=2 if value&16 else 1
                    self.deadline=self.time+((length+stride-1)//stride)*(2*rate+3)*80
                elif not value&1:self.deadline=None
            return
        offset &= 31;self.registers[offset]=value
        if offset!=1 or not value&128:return
        c=int.from_bytes(self.registers[2:4],'little');r=int.from_bytes(self.registers[4:7],'little')
        length=int.from_bytes(self.registers[7:9],'little') or 65536;fixed=self.registers[10];op=value&3
        self.commands[op]+=1;self.status=64
        for _ in range(length):
            old=self.ram[c];other=self.reu[r]
            if op in (0,2):self.reu[r]=old
            if op in (1,2):self.ram[c]=other
            if op==3 and old!=other:self.status|=32
            if not fixed&128:c=(c+1)&65535
            if not fixed&64:r=(r&0xf80000)|((r+1)&0x7ffff)

def execute(name,**options):
    memory=ProbeMemory(**options);program=(BUILD/name).read_bytes();address=int.from_bytes(program[:2],'little')
    memory.ram[address:address+len(program)-2]=program[2:]
    cpu=MPU(memory=memory,pc=0x0810);cpu.stPushWord(0x01ff);text=[]
    for _ in range(3000000):
        if cpu.pc==0x0200:return memory,bytes(value&127 if 0xc1<=value<=0xda else value for value in text)
        if cpu.pc==0xffd2:text.append(cpu.a)
        before=cpu.processorCycles;cpu.step()
        memory.time+=(cpu.processorCycles-before)*(15.625 if memory.turbo else 1000)
    raise AssertionError('Probe did not return to BASIC')

def execute_period(name):
    class PeriodMemory(ProbeMemory):
        def __init__(self):
            super().__init__(turbo=True)
            self.timer_start=0.;self.timer_latch=0xffff
        def __getitem__(self,a):
            if a in (0xdd06,0xdd07):
                self.time+=1000
                if a==0xdd06:self.timer_latch=(0xffff-int((self.time-self.timer_start)/1000))&65535
                return self.timer_latch&255 if a==0xdd06 else self.timer_latch>>8
            return super().__getitem__(a)
        def __setitem__(self,a,value):
            if a==0xdd0f:
                self.time+=1000
                if value&1:self.timer_start=self.time
            super().__setitem__(a,value)
    memory=PeriodMemory();program=(BUILD/name).read_bytes();address=int.from_bytes(program[:2],'little')
    memory.ram[address:address+len(program)-2]=program[2:]
    memory.ram[0x0314:0x0316]=bytes([0x00,0xf0])
    # A minimal KERNAL-style IRQ entry and restore, with the probe's vector in between.
    memory.ram[0xf010:0xf018]=bytes.fromhex('48 8A 48 98 48 6C 14 03')
    memory.ram[0xf000:0xf006]=bytes.fromhex('68 A8 68 AA 68 40')
    memory.ram[0xfffe:0x10000]=bytes([0x10,0xf0])
    cpu=MPU(memory=memory,pc=0x0810);cpu.stPushWord(0x01ff);text=[]
    for _ in range(2000000):
        if cpu.pc==0x0200:return memory,bytes(value&127 if 0xc1<=value<=0xda else value for value in text)
        if memory.deadline is not None and memory.time>=memory.deadline:memory.flags|=1
        before=cpu.processorCycles
        if memory.flags:cpu.irq()
        if cpu.pc==0xffd2:text.append(cpu.a)
        cpu.step()
        # Current NTSC release maximum is twice 32,727,273 Hz.
        memory.time+=(cpu.processorCycles-before)*(1e9/(2*32727273))
    raise AssertionError('Period probe did not return to BASIC')

class C64AudioProbes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']=os.environ.get('PATH',os.environ.get('Path',''))
        cls.env=env
        subprocess.run([sys.executable,str(ROOT/'c64ua/build.py'),'--build-dir',str(BUILD)],env=env,check=True,capture_output=True)
    def test_reproducible_rebuild_and_provenance(self):
        subprocess.run([sys.executable,str(ROOT/'c64ua/build.py'),'--build-dir',str(BUILD),'--check'],env=self.env,check=True,capture_output=True)
        record=json.loads((BUILD/'validation.json').read_text())
        self.assertEqual(len(record['outputs']),7)
        for name,entry in record['outputs'].items():self.assertEqual(hashlib.sha256((BUILD/name).read_bytes()).hexdigest(),entry['sha256'])
    def test_detection_native_turbo_absent_and_stale(self):
        for turbo,stale,ua,found in ((False,False,True,True),(True,False,True,True),(False,True,True,True),(True,True,True,False),(True,False,False,False)):
            memory,text=execute('detect.prg',turbo=turbo,stale=stale,ua=ua)
            self.assertEqual(b'ULTIMATE AUDIO DETECTED' in text,found,(turbo,stale,ua,text))
            if ua:self.assertEqual((memory.registers[32],memory.flags,memory.deadline),(0,0,None))
    def test_background_descriptors_samples_and_gate_modes(self):
        for name,rate in (('background-48k.prg',130),('background-cap.prg',0)):
            memory,text=execute(name)
            self.assertIn(b'7 AUDIO VOICES RUNNING',text)
            self.assertEqual(memory.commands,[1,0,0,0])
            self.assertEqual(memory.reu[0xf00000:0xf07000],memory.ram[0x3000:0xa000])
            for voice in range(7):
                base=32*(voice+1);regs=memory.registers
                self.assertEqual(regs[base],0x53);self.assertEqual(regs[base+15],rate)
                self.assertEqual(int.from_bytes(regs[base+5:base+8],'big'),0xf00000+voice*4096)
    def test_full_reu_stress_control_flow_and_data(self):
        memory,text=execute('stress.prg')
        self.assertIn(b'REU STRESS PASS',text)
        self.assertEqual(memory.commands,[513,768,256,256])
        self.assertEqual(memory.reu[0xe00000],0)
        self.assertTrue(all(memory.registers[32*(voice+1)]==0x53 for voice in range(7)))
    def test_period_timeout_allows_slowest_sample_at_maximum_turbo(self):
        for name in ('period.prg','period16.prg'):
            memory,text=execute_period(name)
            self.assertNotIn(b'TIMEOUT',text,(name,text))
            rows=text.splitlines()
            self.assertEqual(len(rows),6,(name,text))
            self.assertTrue(all(len(row)==4 for row in rows),(name,text))
            self.assertGreater(int(rows[-1],16),10000)
            self.assertEqual(memory.ram[0x0314:0x0316],bytes([0x00,0xf0]))
            self.assertEqual((memory.ram[0xdd0f],memory.registers[32],memory.flags),(0,0,0))

if __name__=='__main__':unittest.main()
