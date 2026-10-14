#!/usr/bin/env python3
"""Build an independent public-API oracle; trusted source, no hardware IO."""
import argparse, os, pathlib, shlex, subprocess
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=pathlib.Path,required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]; out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=False)
cmd=shlex.split(os.environ.get('CC','cc'))+['-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'include'),'-DMCS_USER_CONFIG=0','-DMCS_GC_STRESS=1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
cmd+=list(map(str,sorted((root/'src').glob('*.c'))+sorted((root/'modules').glob('*/*.c'))))+[str(root/'tests/can_fd/reviewer_payload_oracle.c'),'-lm','-o',str(out/'oracle')]
(out/'command.txt').write_text(shlex.join(cmd)+'\n')
subprocess.run(cmd,cwd=root,check=True)
subprocess.run([str(out/'oracle')],cwd=root,check=True)
