# Evidence-only controlled queued-focus characterization

Frozen Studio commit32cba4bd496f971d9885d6c5b16eb1bcee7fdd20, tree73222c07330255f4ad40940b081c6b817e2a6259. NO production/test-assertion/source changes after freeze. Original unexplained full-harness failure remains unexplained; original diagnostics cannot be retroactively recovered. This does not demonstrate naturally occurring baseline failure.

## Method

Real offline Chromium153.0.8010.12, Playwright1.63.0, Node24.14.1, shared network-tests.lock. Baseline injects exactb4 studio.js AND snippets.js, candidate exact32 current modules. Retained the ACTUAL Editor.open requestAnimationFrame callback object without changing its function; restored normal requestAnimationFrame immediately after open.

Both orderings explicitly reproduce installed Playwright _fill's two phases (target focus/select, then separate native keyboard.insertText); NOT a claim that unmodified page.fill was naturally caught racing. Relevant installed implementation excerpt retained.

Paired control: run retained tab-focus callback BEFORE find-field focus/select, then native insertText("var"), ordinary fill replacement and real click ReplaceAll. Expected raw CRLF/CR/LF replacement succeeds.

Injected timing window: select find field, THEN release that same pending tab-focus callback, THEN native insertText("var"). Actual callback restores caret0 and focus to textarea; native input goes into source, leaving find empty. ReplaceAll then does not replace. Actual raw becomes "var"+original mixed-EOL source rather than expected replacement. Event trace records field-selected -> focusout fFind -> focusin ta -> actual-open-frame-released -> beforeinput/input targetta.

## Results and limits

Forty samples: baseline10control+10forcedtiming, candidate10control+10forcedtiming. All expected control and characterized source/focus outcomes reproduced. Every sample includes raw bytes as hex, selection/focus/find, actual callback text/hash and event timestamps. The callback hashes across both source versions were:

bfde8a37d382592e91a2c596db16443ae2e46340ee59f9aba0d2c856b7c9a88d

This establishes an existing baseline focus-steal timing window under explicit controlled scheduling, not the root cause of the original uninstrumented full-harness failure. Original status remains UNEXPLAINED; no unqualified Studio/combined approval requested. Repeated natural focused320 and full baseline2/candidate2 passes remain separate evidence. Do not count injected timing outcomes as unexplained functional test regressions or silently substitute them for original logs.

Raw file: controlled-focus-race.log; harness: controlled-focus-race.js. No hostile input, device, network, implicit source execution or source edits. Parent/reviewer must independently judge this narrow evidence. Existing immutable source/history backup stays untouched at /data/backups/v2-parallel-studio/32cba4bd496f971d9885d6c5b16eb1bcee7fdd20/.
