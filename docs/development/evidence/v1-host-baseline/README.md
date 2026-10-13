# Clean current-main host baseline (not v2 performance)

Runtime source: d3def1ff534c59a6726037ccf31f61535647e857, clean detached checkout.
Harness: checkpoint 997db043ea62c5a8b2c444cb6e5dfb56f9175173 on feat/microcs-v2.
The metadata's harness commit is a local content-equivalent commit (GitHub MCP
and local commit identities differ); the checkpoint trees were verified equal.
Harness/source/workload hashes and exact build argv are in metadata.json.

Reproduce with the same GCC 11.5.0/Linux x86-64 environment and no parallel load
if seeking comparable timings:

```sh
git worktree add --detach /data/MicroCS-v1-baseline d3def1ff534c59a6726037ccf31f61535647e857
python3 bench/v2/run.py --repo /data/MicroCS-v1-baseline --out /data/raw/microcs-v1-host-baseline --repetitions 25
```

This capture ran while other host gate builds were active: it is a functional
baseline and preliminary timing distribution, **not an isolated performance
budget or valid speedup comparison**. Re-run without concurrent builds before
setting budgets or comparing versions. No v2 core optimization has occurred.

1000 raw checked samples: 4 workloads × 10 phases × 25 repetitions. Unique grain
is (workload, phase, iteration), not any single field. Data profiler reports no
duplicates/nulls; constant verified=true is deliberate because failed runs are
rejected. Summary medians independently recomputed through pandas groupby and
cross-checked against runner output for all 40 groups. Phase/group sample counts
all equal 25. Mathematical expected stdout checked on every source/image/XIP
execution; every tracked VM allocator returns to zero after teardown.

Host wall microseconds only; requested allocator memory excludes libc overhead
and external input/image buffers. Cold/warm REPL use different representative
expressions. Memory and timing definitions are in bench/v2/README.md. No MCU,
hardware, .NET, graphics or security-overhead results are claimed. The host
link map and executable remain under /data/raw and local backups; small raw
samples/provenance/summary/build logs are committed remotely here.
