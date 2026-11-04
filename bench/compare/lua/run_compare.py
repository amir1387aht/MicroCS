import os, subprocess, re, statistics, json
from pathlib import Path
base=str(Path(__file__).resolve().parents[3]); root=base+'/build/compare-nf'
if hasattr(os, 'sched_getaffinity'): os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
env=os.environ.copy(); env.update(DOTNET_SYSTEM_GLOBALIZATION_INVARIANT='1',DOTNET_CLI_TELEMETRY_OPTOUT='1')
runner=base+'/build/compare-lua/lua_runner'; lua=base+'/build/compare-lua'
dotnet=os.environ.get('DOTNET', 'dotnet')
work={'fib':('fib30','832040'),'loop':('loop10M','29999994'),'objects':('objects1M','980302')}; out={k:{t:[] for t in ['MicroCS','Lua','nanoFramework']} for k in work}
for r in range(3):
  for runtime in [['MicroCS','Lua','nanoFramework'],['nanoFramework','MicroCS','Lua'],['Lua','nanoFramework','MicroCS']][r]:
    if runtime=='nanoFramework':
      cmd=[dotnet,root+'/nanoclr/tools/net8.0/any/nanoFramework.nanoCLR.CLI.dll','run','-a',root+'/Bench.pe',root+'/corlib/lib/mscorlib.pe',root+'/text/lib/nanoFramework.System.Text.pe']
      p=subprocess.run(cmd,env=env,capture_output=True,text=True,check=True); open(f'{lua}/nano{r}.log','w').write(p.stdout+p.stderr)
      for k,(label,expected) in work.items():
        samples=[float(x)/1000 for x in re.findall(r'\[time\] '+label+r' (\d+) us',p.stdout)]
        assert len(samples)==3 and p.stdout.count('\n'+expected+'\n')==3,(k,samples)
        out[k][runtime].append(statistics.median(samples))
    else:
      for k,(label,expected) in work.items():
        samples=[]
        for _ in range(3):
          if runtime=='Lua':
            p=subprocess.run([runner,f'{lua}/{k}.luac'],capture_output=True,text=True,check=True)
            assert p.stdout.strip()==expected,(p.stdout,expected); samples.append(float(p.stderr.strip()))
          else:
            p=subprocess.run([base+'/build/bench_host',f'bench/{k}.cs','1'],cwd=base,capture_output=True,text=True,check=True)
            samples.append(float(re.search(r'image run\s+([\d.]+) us',p.stdout).group(1))/1000)
        out[k][runtime].append(statistics.median(samples))
    print('round',r+1,runtime,'complete',flush=True)
json.dump(out,open(f'{lua}/results.json','w'),indent=2)
for k,rs in out.items(): print(k, {t:round(statistics.median(v),3) for t,v in rs.items()})
