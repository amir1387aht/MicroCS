# Isolated host comparison: current main vs keyword-lookup checkpoint

This is the first **scoped optimization checkpoint**, not the final v2 release
benchmark suite. Raw captures finished before frozen-checkout gate builds began.
Source checkouts were clean, and compiler/OS/CPU/harness/workload/expected hashes
match; only src/mcs_lexer.c differs among runtime sources/headers.

- Baseline main: d3def1ff534c59a6726037ccf31f61535647e857.
- Candidate/harness: 8936b6d8a3a81d048733d7d750eb411521728273.
- Five workloads × ten phases × 25 repetitions per version = 2500 checked samples.
- CPU/toolchain/settings: exact values and build argv in each metadata.json.
- Every output/status verified; tracked allocator usage zero after each VM teardown.
- Requested peak/retained bytes and image sizes match in **all** groups.
- Host ELF text grows 56 B; data/BSS unchanged. This is NOT MCU flash evidence.

Compiler-heavy workload, medians in host wall microseconds:

| Mode | Main | Candidate | Median reduction |
|---|---:|---:|---:|
| Compile + optimize | 573.140 | 541.296 | 5.6% |
| Compile, no optimizer | 420.286 | 370.829 | 11.8% |
| Source compile + run | 348.093 | 309.732 | 11.0% |

All measured phases, p10/p90, memory and opposing results remain in comparison.csv.
Some runtime medians are slightly worse: collections image load+run +1.36%,
strings image load+run +0.70%, calls image load+run +0.67% in this capture.
The lexer change does not optimize image execution; neither improvements nor
small regressions there establish a universal runtime speedup/slowdown.
These are short host timings, one sequential-version-order capture, not a
statistical causal guarantee or board performance budget. Repeat in reversed
order and on target hardware before treating results as stable release budgets.

## Reproduce

Use the same frozen harness/compiler/config and new evidence directories:

```sh
python3 /data/MicroCS-v2-gates/bench/v2/run.py --repo /data/MicroCS-v1-baseline --out /data/raw/microcs-idle-v1 --repetitions 25
python3 /data/MicroCS-v2-gates/bench/v2/run.py --repo /data/MicroCS-v2-gates --out /data/raw/microcs-idle-v2 --repetitions 25
python3 docs/development/evidence/isolated-host-comparison/compare_microcs.py
```

The final step uses pandas (the sandbox's installed version), recomputes every
median through groupby and independently by filtered sorted midpoint, verifies
sample grain/counts/matching configuration/source hashes, and exports the one
evidence object, full CSV and optional canonical chart spec under /data/analysis.
The chart is a scoped grouped-bar view of the three compiler-heavy modes in
native µs, not an aggregation of unlike workloads. Its canonical inline builder
card is 0d2f0f1261eee4fc575ee016d2c123534da072a0eb2045d789ef7ffd178ad652.
Small raw/provenance/summary evidence is committed here; executables and host
link maps remain in /data/raw and recovery backups, not third-party binaries.
Full cross-target/flash gates, compiler stack high-water, hardware, security/
graphics overhead and remaining v2 scope remain release-blocking.
