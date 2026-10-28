#!/usr/bin/env python3
"""Exercise the actual repaired LVGL 8 bit writer under non-recovering UBSan."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('lvgl',type=Path);a=p.parse_args()
spec=importlib.util.spec_from_file_location('patch',Path(__file__).resolve().parents[2]/'tools/patch_lvgl8_font.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
if patch.major_version(a.lvgl)!=8:
    print('Not the LVGL 8 writer; no extracted test required');raise SystemExit(0)
source=(a.lvgl/'src/font/lv_font_fmt_txt.c').read_text()
m=re.search(r'static inline void bits_write\([^;]+?\)\s*\{',source)
if not m:raise SystemExit('Missing LVGL 8 bits_write')
pos=m.end();depth=1
while depth:
    if source[pos]=='{':depth+=1
    elif source[pos]=='}':depth-=1
    pos+=1
function=source[m.start():pos]
program='''#include <stdint.h>
#include <stdio.h>
'''+function+'''
int main(void) {
    static const unsigned convert[8]={0,2,4,6,9,11,13,15};
    unsigned checks=0;
    for(unsigned len=1;len<=8;++len) {
        if(len!=1 && len!=2 && len!=3 && len!=4 && len!=8)continue;
        unsigned width=len==3 ? 4 : len;
        for(unsigned byte=0;byte<3;++byte)for(unsigned start=0;start+width<=8;++start)
        for(unsigned val=0;val<(1u<<len);++val)for(unsigned initial=0;initial<256;++initial) {
            uint8_t buf[5]={0xa5,0xa5,0xa5,0xa5,0xa5};buf[byte+1]=(uint8_t)initial;
            bits_write(buf+1,byte*8+start,(uint8_t)val,(uint8_t)len);
            unsigned shift=8-start-width;
            /* The writer preserves the already-written prefix and clears the
             * not-yet-written suffix; preserve native stream semantics. */
            unsigned prefix_mask=0xffu << (8-start);
            unsigned scaled=len==3 ? convert[val] : val;
            unsigned expected=(initial & prefix_mask)|(scaled<<shift);
            for(unsigned n=0;n<5;++n)if(buf[n]!=(n==byte+1 ? expected : 0xa5))return 2;
            ++checks;
        }
    }
    printf("LVGL 8 actual font writer: %u exhaustive checks passed\\n",checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp);(path/'writer.c').write_text(program)
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(path/'writer.c'),'-o',str(path/'writer')],check=True)
    subprocess.run([str(path/'writer')],check=True)
