#!/usr/bin/env python3
"""Link reviewer oracle after genuine tools/test_tft_headless.py build; no extraction/network."""
import argparse,os,pathlib,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('build');p.add_argument('--sanitize',action='store_true');p.add_argument('--gc-stress',action='store_true');p.add_argument('--tft-root',type=pathlib.Path,default=pathlib.Path('/data/deps/TFT_eSPI-upstream'));a=p.parse_args()
root=pathlib.Path.cwd();out=(root/a.build).resolve();dep=a.tft_root.resolve();boundary=root/'tests/tft/headless'
assert subprocess.check_output(['git','-C',str(dep),'rev-parse','HEAD'],text=True).strip()=='16e37595040eac69cd628e4bffb56fc30cad6299'
assert not subprocess.check_output(['git','-C',str(dep),'status','--porcelain'],text=True).strip()
flags=['-O1','-g','-ffunction-sections','-fdata-sections','-I'+str(root/'include'),'-DMCS_USER_CONFIG=0','-DARDUINO=1','-DMCS_ENABLE_TFT_ESPI=1','-DMCS_ENABLE_THREADS=0','-DMCS_ENABLE_LVGL=0']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
if a.gc_stress:flags+=['-DMCS_GC_STRESS=1']
cxx=shlex.split(os.environ.get('CXX','g++'));obj=out/'reviewer_scroll_oracle.o';exe=out/'reviewer_scroll_oracle'
cpp=flags+['-std=gnu++17','-Wall','-Wextra','-Werror','-I'+str(boundary),'-I'+str(dep),'-DUSER_SETUP_LOADED=1','-DILI9341_DRIVER=1','-DTFT_WIDTH=240','-DTFT_HEIGHT=320','-DTFT_CS=5','-DTFT_DC=2','-DLOAD_GLCD=1','-DDISABLE_ALL_LIBRARY_WARNINGS=1']
subprocess.run(cxx+cpp+['-c','tests/tft/reviewer_scroll_oracle.cpp','-o',str(obj)],check=True)
objects=sorted(str(p) for p in out.glob('*.o') if p.name not in {'test_sprite.cpp.o','reviewer_scroll_oracle.o'})
subprocess.run(cxx+flags+objects+[str(obj),'-Wl,--gc-sections','-Wl,--wrap=calloc','-lm','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
