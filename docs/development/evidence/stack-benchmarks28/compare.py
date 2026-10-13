from pathlib import Path
import pandas as pd
import json, statistics, hashlib
RAW = Path('/data/raw/microcs-stack26')
OUT = Path('/data/analysis')
frames = {}
for variant in ['baseline','fixed']:
    frames[variant] = pd.read_json(RAW / ('bench-'+variant) / 'samples.jsonl', lines=True, precise_float=True)
    f=frames[variant]
    assert len(f)==1250 and not f.duplicated(['workload','phase','iteration']).any()
    assert (f.verified==1).all() and (f.elapsed_us>=0).all() and not f.isna().any().any()
    assert (f.groupby(['workload','phase']).size()==25).all()
    metadata=json.loads((RAW/('bench-'+variant)/'metadata.json').read_text())
    assert metadata['status']=='passed' and metadata['sample_count']==1250
rows=[]
for (workload,phase), before in frames['baseline'].groupby(['workload','phase']):
    after=frames['fixed'].query('workload == @workload and phase == @phase')
    bm=before.elapsed_us.median(); am=after.elapsed_us.median()
    # Independent direct-record/statistics check, not a second pandas groupby.
    for label, expected in [('baseline',bm),('fixed',am)]:
        values=[r['elapsed_us'] for r in map(json.loads,(RAW/('bench-'+label)/'samples.jsonl').read_text().splitlines()) if r['workload']==workload and r['phase']==phase]
        assert statistics.median(values)==expected
    row={'workload':workload,'phase':phase,'samples_per_version':25,'baseline_median_us':bm,'fixed_median_us':am,'median_change_percent':(am/bm-1)*100}
    for label,f in [('baseline',before),('fixed',after)]:
        row[label+'_p10_us']=f.elapsed_us.quantile(.1);row[label+'_p90_us']=f.elapsed_us.quantile(.9)
        row[label+'_peak_bytes']=int(f.requested_peak_bytes.max());row[label+'_retained_bytes']=int(f.requested_retained_bytes.max());row[label+'_image_bytes']=int(f.image_bytes.iloc[0])
    assert row['baseline_peak_bytes']==row['fixed_peak_bytes']
    assert row['baseline_retained_bytes']==row['fixed_retained_bytes']
    assert row['baseline_image_bytes']==row['fixed_image_bytes']
    rows.append(row)
comparison=pd.DataFrame(rows)
comparison.to_csv(OUT/'microcs-stack-comparison.csv',index=False)
evidence={'sample_count_each':1250,'workloads':5,'phases':10,'samples_per_workload_phase':25,'all_outputs_verified':True,'all_requested_peak_retained_and_image_sizes_equal':True,'numeric_validation':'Every median independently recomputed from raw records with statistics.median','findings':rows,'comparison_plan':{'grain':'workload x phase, matched flags on same host','unit':'wall microseconds, requested allocator bytes, image bytes','disposition':'table','reason':'Engineering checkpoint evidence, retain per-workload medians and p10/p90; do not pool heterogeneous phases into a speed headline'},'limitations':['Sequential baseline then fixed, not randomized; shared-host/thermal/scheduling effects and ordering can influence short samples','Not MCU timing/flash, no independent performance or security audit','No universal speedup or statistically established causal overhead claim','Successful requested-byte/image equality does not prove arbitrary-source peak/capacity safety']}
(OUT/'microcs-stack-evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
print('Validated 1250 samples/version, 50 matched workload-phase groups, independent medians; requested memory/image sizes unchanged')
print('Runtime timing deltas retained per workload/phase; no pooled causal performance claim')
