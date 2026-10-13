# Compiler CPU profile before keyword lookup change

Runtime source is unchanged main d3def1ff534c59a6726037ccf31f61535647e857.
GCC 11.5.0, Linux host. Build/run from repo:

```sh
cc -pg -O2 -std=gnu99 -Iinclude src/*.c modules/*/*.c docs/development/evidence/compiler-profile/compile_profile.c -lm -o /data/profiling/compile_profile
cd /data/profiling
./compile_profile > compile-profile.log
gprof ./compile_profile gmon.out > compile-profile-gprof.txt
```

1000 successful compilation-only iterations of 1200 assignments. This is an
instrumented short profile (0.30 sampled CPU seconds, coarse 0.01-second ticks),
not release-performance evidence. Concurrent make-check compilation occurred;
no wall-time comparison is claimed. mcs_lex was the largest individually sampled
function in this run. Source inspection shows every eligible lowercase identifier
walking the whole keyword table. Candidate optimization: group that immutable
table by first byte and index 27 byte-sized boundaries; no allocator, token enum,
public API, syntax, diagnostics or format change. Exhaustive keyword enum and
escaped/case/contextual/prefix/suffix/Unicode identifier regressions cover it.

Accept/reject the candidate with isolated identical harness/config runs plus
flash/RAM checks; don't infer a broad compiler/VM speedup from this profile.
Cortex-M/link-size and full sanitizers/CI gates still required.
