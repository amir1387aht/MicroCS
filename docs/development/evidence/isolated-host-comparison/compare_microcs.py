from pathlib import Path
import json
import pandas as pd

root=Path('/data/raw')
out=Path('/data/analysis/microcs-compiler-comparison');out.mkdir(exist_ok=True)
frames={}
metadata={}
for name in ['v1','v2']:
    directory=root/('microcs-idle-'+name)
    data=pd.read_json(directory/'samples.jsonl', lines=True)
    metadata[name]=json.loads((directory/'metadata.json').read_text())
    assert metadata[name]['status']=='passed' and not metadata[name]['working_tree_status']
    assert len(data)==1250 and data.verified.all() and data.notna().all().all()
    assert not data.duplicated(['workload','phase','iteration']).any()
    assert data.groupby(['workload','phase']).size().eq(25).all()
    grouped=data.groupby(['workload','phase']).agg(median_us=('elapsed_us','median'),
        p10_us=('elapsed_us',lambda x:x.quantile(.1)), p90_us=('elapsed_us',lambda x:x.quantile(.9)),
        peak_bytes=('requested_peak_bytes','max'), retained_bytes=('requested_retained_bytes','max'),
        image_bytes=('image_bytes','first'))
    # Independent direct-filter + sorted midpoint check for every headline/summary cell.
    for (w,p),row in grouped.iterrows():
        values=sorted(data.loc[(data.workload==w)&(data.phase==p),'elapsed_us'].tolist())
        assert abs(values[12]-row.median_us)<1e-9
    frames[name]=grouped
for key in ['compiler_version','python_version','os','cpu','harness_sha256','runner_sha256','workload_sha256']:
    assert metadata['v1'][key]==metadata['v2'][key], key
changed=[p for p,sha in metadata['v1']['source_sha256'].items() if sha!=metadata['v2']['source_sha256'][p]]
assert changed==['src/mcs_lexer.c'], changed
x=frames['v1']; y=frames['v2']; comparison=x.add_prefix('baseline_').join(y.add_prefix('candidate_'))
comparison['median_change_percent']=100*(comparison.candidate_median_us/comparison.baseline_median_us-1)
comparison['peak_delta_bytes']=comparison.candidate_peak_bytes-comparison.baseline_peak_bytes
comparison['retained_delta_bytes']=comparison.candidate_retained_bytes-comparison.baseline_retained_bytes
assert (comparison.peak_delta_bytes==0).all() and (comparison.retained_delta_bytes==0).all()
assert (comparison.baseline_image_bytes==comparison.candidate_image_bytes).all()
comparison.to_csv(out/'comparison.csv')
phases=['compile_opt','compile_noopt','source_run']; labels=['Compile + optimize','Compile, no optimizer','Source compile + run']
plotted=comparison.loc[[('compiler',p) for p in phases]]
assert (plotted.median_change_percent<0).all()
sections={}
for name in ['v1','v2']:
    parts=(root/('microcs-idle-'+name)/'size.txt').read_text().splitlines()[1].split()
    sections[name]={k:int(v) for k,v in zip(['text','data','bss'],parts[:3])}
text_delta=sections['v2']['text']-sections['v1']['text']; assert text_delta==56
headline_percent=-float(comparison.loc[('compiler','compile_noopt'),'median_change_percent'])
evidence={'grain':'workload, phase, iteration; 25 samples per group', 'versions':{k:v['source_commit'] for k,v in metadata.items()},
    'matching_build':'same GCC/OS/CPU, workload/expected and harness hashes; only lexer C source differs',
    'total_samples':2500, 'changed_source_files':changed, 'host_elf_sections':sections,
    'headline_noopt_reduction_percent':headline_percent, 'host_text_delta_bytes':text_delta,
    'requested_peak_and_retained_delta_all_groups':0, 'image_size_delta_all_groups':0,
    'compiler_phases':plotted.reset_index().to_dict(orient='records'),
    'comparison_plan':[
        {'grain':'compiler workload × phase × version','population':'25 fresh-VM checked samples per phase/version','unit':'host wall microseconds',
         'finding':'Compiler-heavy workload has lower median compilation/source-run time in all three measured modes in this capture.',
         'disposition':'chart','reason':'side-by-side native median timings distinguish optimizer/no-opt/source phases without a broad speedup claim'},
        {'grain':'all 5 workloads × 10 phases','population':'identical fixtures/configurations, 25 samples per group/version','unit':'microseconds and bytes',
         'finding':'Other runtime medians fluctuate; requested peaks/retained bytes and image sizes are unchanged, host text grows 56 B.',
         'disposition':'table','reason':'retain every opposing/non-improving result in full comparison CSV; do not claim universal speedup'},
        {'grain':'MCU boards','population':'not measured here','unit':'cycles/RAM/flash','finding':'No new hardware performance claim can be made.',
         'disposition':'prose','reason':'explicit missing coverage, not zero or extrapolation'}],
    'limitations':['single host capture, sequential version order, 25 repetitions; no statistical causal or universal speedup guarantee',
      'fresh VM is not cold CPU cache; tracked requested memory excludes allocator overhead and external buffers',
      'host ELF text delta is not MCU flash; minimum-profile and cross-target CI remain required',
      'no graphics/security/peripheral overhead or physical-hardware evidence; v2 release blocked']}
(out/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
spec={'version':1,'form':'categorical-bars','title':'Compiler-heavy workload is faster in this host capture',
    'aside':'Median µs from 25 checked samples per mode; no MCU claim',
    'mode':'grouped','labels':labels,'axis':{'zero':True},'colorMap':{'Current main':'neutral','v2 WIP checkpoint':'blue'},
    'series':[{'label':'Current main','role':'base','valueKind':'number','data':plotted.baseline_median_us.tolist()},
              {'label':'v2 WIP checkpoint','role':'subject','valueKind':'number','data':plotted.candidate_median_us.tolist()}]}
(out/'chart-spec.json').write_text(json.dumps(spec,indent=2)+'\n')
print(f'PASS 2500 comparable checked samples; 100 independent medians; no-opt compiler median reduction {headline_percent:.1f}%; host text +{text_delta} B; no requested memory/image growth')
