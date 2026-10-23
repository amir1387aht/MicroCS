"""Assemble-check and emulate the exact PIO instructions, without a CPU ISR."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
text=(root/'include/mcs_rp2_encoder.h').read_text()
code=[int(x,16) for x in re.findall(r'0x[0-9a-f]+',text.split('mcs_rp2_encoder_code[] = {')[1].split('};')[0])]
source=(root/'ports/rp2/encoder.pio').read_text()
labels={};lines=[]
for line in source.splitlines():
 line=line.split(';')[0].split('//')[0].strip()
 if not line or line.startswith('.'):continue
 if line.endswith(':'):labels[line[:-1]]=len(lines)
 else:lines.append(line.upper())
assembled=[]
for op in lines:
 if op.startswith('JMP '):
  target=op[4:];condition=0
  if target.startswith('Y--,'):condition=128;target=target.split(',')[1].strip()
  assembled.append(condition+labels[target.lower()])
 else:assembled.append({'MOV ISR, Y':0xa0c2,'PUSH NOBLOCK':0x8000,'OUT ISR, 2':0x60c2,'IN PINS, 2':0x4002,'MOV OSR, ISR':0xa0e6,'MOV PC, ISR':0xa0a6,'MOV Y, ~Y':0xa04a}[op])
assert code==assembled and len(code)==24
class PIO:
 def __init__(self):self.y=0;self.isr=0;self.osr=0;self.pc=15;self.fifo=[]
 def sample(self,pins):
  for _ in range(40):
   instr=code[self.pc];following=15 if self.pc==23 else self.pc+1
   if instr<0x2000:
    if instr&0xe0:self.y=(self.y-1)&0xffffffff
    following=instr&31
   elif instr==0xa0c2:self.isr=self.y
   elif instr==0x8000:
    if len(self.fifo)<4:self.fifo.append(self.isr)
    self.isr=0
   elif instr==0x60c2:self.isr=self.osr&3;self.osr>>=2
   elif instr==0x4002:self.isr=((self.isr<<2)|pins)&0xffffffff
   elif instr==0xa0e6:self.osr=self.isr
   elif instr==0xa0a6:following=self.isr&31
   elif instr==0xa04a:self.y=(~self.y)&0xffffffff
   else:raise AssertionError(hex(instr))
   self.pc=following
  return self.y
p=PIO();p.sample(3);baseline=p.y
for _ in range(10000):
 for phase in [1,0,2,3]:p.sample(phase)
assert p.y==(baseline+40000)&0xffffffff and len(p.fifo)==4
for _ in range(10000):
 for phase in [2,0,1,3]:p.sample(phase)
assert p.y==baseline
p.y=0xfffffffd
for phase in [1,0,2,3]:p.sample(phase)
assert p.y==1
before=p.y;p.sample(1);p.sample(3);assert p.y==before # chatter cancels
p.sample(0);assert p.y==before # illegal diagonal does not count
print('PASS PIO: source/opcodes match, autonomous x4 counting, full FIFO, reverse, wrap and invalid transitions')
