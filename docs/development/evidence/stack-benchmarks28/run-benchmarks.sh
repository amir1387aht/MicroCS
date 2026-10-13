#!/bin/sh
set -eu
cd /data/MicroCS-v2-next
# Measurement waits for all source/profile compilation to finish; no competing
# local compiler/test jobs are deliberately launched during these samples.
tries=0
while ! grep -q 'FULL_CHECK_EXIT=' /data/stack-fullcheck.log; do
  tries=$((tries+1)); [ "$tries" -lt 1200 ] || exit 2
  sleep 3
done
grep -q 'FULL_CHECK_EXIT=0' /data/stack-fullcheck.log || exit 3
python3 bench/v2/run.py --repo /data/MicroCS-v2-baseline --out /data/raw/microcs-stack26/bench-baseline --repetitions 25
python3 bench/v2/run.py --repo /data/MicroCS-v2-next --out /data/raw/microcs-stack26/bench-fixed --repetitions 25
