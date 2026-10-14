#!/bin/bash
set -u
cd /data/v2-parallel/review || exit 1
export NODE_PATH=/vercel/sandbox/node_modules
export CHROME=$(command -v chromium)
LOG=/data/v2-parallel/logs/review/round2
: > "$LOG/combined-results.tsv"
failed=0
run() {
 local name=$1; shift
 "$@" > "$LOG/combined-$name.log" 2>&1
 local rc=$?
 printf '%s\t%s\n' "$name" "$rc" | tee -a "$LOG/combined-results.tsv"
 if [ "$rc" -ne 0 ]; then failed=1; tail -15 "$LOG/combined-$name.log"; fi
}
git rev-parse HEAD HEAD^{tree} > "$LOG/combined-provenance.txt"
run build make -j2 mcs
run host sh tests/run_tests.sh ./mcs
run template-metadata python3 -B tests/test_template_metadata.py
run template-generated python3 -B tools/studio/gen_templates.py --check
run verifier python3 -B tests/test_verify_preview.py
run packager python3 -B tests/test_preview_package.py
run workflow python3 -B tests/test_preview_workflow.py
run shell python3 -B tests/test_shell.py ./mcs
run readline python3 -B tests/test_readline_echo.py ./mcs
run wiki python3 -B tools/wiki_sync.py --check
for name in source lexical scope highlight capabilities project project_file format; do
 run "$name" node "tests/studio/test_$name.js" ./mcs
done
for name in review_eol source_eol_history_browser completion_context editor_layout gallery_layout project_layout status_layout project_browser project_file_browser studio; do
 run "$name" node "tests/studio/test_$name.js" ./mcs
done
exit "$failed"
