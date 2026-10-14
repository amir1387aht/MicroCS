# Original v2 mission: implementation status and resumed work

The original implementation handoffs remain the full project goal. A scoped
preview and its packaging/CI improvements do not replace that mission. The
current software is not a finished stable v2. No merge, tag or release is
approved by this checkpoint; the latest user restriction requires separate
approval for main merges and publication.

## Eight-phase status

1. **Compatibility and evidence — partial.** Host baselines and regressions exist;
   exhaustive public/native/port coverage and comparable MCU evidence remain.
2. **Core/compiler/REPL — partial.** Keyword lookup, defensive capacity/cancellation
   and targeted image metadata/debug checks are implemented. The old checkpoint25
   handoff's immediate stack guards and research operand fixes are already in
   newer history; do not restart them. Broader measured optimization, ROM/profile,
   lifecycle and target/footprint work remain. Host measurements are not MCU wins.
3. **LVGL8/9 — partial real integration.** Real pinned versions, widget/style/text/
   font subsets and synchronous TFT bridge exist. Complete enabled public APIs,
   assets/files/custom fonts/drawing/groups/async/build routes/hardware remain.
4. **TFT_eSPI — partial real integration.** Native drawing, sprites, images/copies
   and synchronous bridge exist. Full operations/assets/fonts/readback/DMA/platform
   and physical-board evidence remain; native RAM tests do not establish hardware.
5. **Studio — partial.** Lexer/formatter, scoped assistance, explicit portable source
   projects/navigation, responsive layouts and bounded explicit raw-EOL history
   exist. Full assistance/imports/snippets/long-file measured performance and
   broader UI/serial/accessibility coverage remain.
6. **Optional security — incomplete.** Targeted checks are not complete bytecode
   verification, a sandbox, shell authentication, signing or a board trust chain.
   Use trusted source/compiler images only. No custom cryptography, secret/default
   keys or irreversible provisioning is authorized. Independent review and exact
   hardware evidence cannot be invented or waived.
7. **Remaining roadmap — incomplete.** Arduino Wi-Fi/MQTT, RP2 streaming/PIO,
   USB mass storage, CAN FD, event async/await and debugger/VS Code work require
   genuine implementation and platform/config/cleanup evidence, not symbols.
8. **Benchmarks/documentation — partial.** Fresh host evidence exists, but full
   measured target comparisons, support/API/setup/migration and examples remain.

## Resumed substantive increments

- Studio lane: real bounded numbered snippet fields, editable defaults, keyboard
  field navigation and safe editor/session behavior; actual browser/runtime tests.
- Backend lane: classic-compatible CAN FD payload/flag flow through common HAL,
  actual VM bindings and simulator, with truthful backend support. Software
  loopback is not physical bus validation.
- Coordinator lane: checked native RAM sprite scrolling using pinned TFT_eSPI,
  all-depth snapshot correctness and lifetime/boundary tests. Not full TFT support.
- Independent reviewer: verifies exact feature commits and tests; rejects scope
  inflation, stubs, unmeasured claims and unexplained failures.

These are active tasks, not completion claims. Source/configuration versions,
limits, results and blockers must accompany every tested checkpoint. Preserve
small commits, immutable local backups AND remote WIP checkpoints; fetch and
compare trees. Local-only backups cannot guarantee reset recovery. Full original
scope remains on the living checklist; missing hardware/review is a concrete
blocker, not permission to silently remove requirements.

## Preserved preceding checkpoint

Reviewed local candidate b4f670a2a778b92322bf868cba300f9b767df893, tree
cd227a96f8a8852e8eefe9ba3df8f2bb4ecbfb68, passed the independent 28-command
local matrix and trusted extracted host/Studio package smoke. This preserves
bounded raw-EOL history, offline integrity verifier, their tests and CI wiring.
Evidence copied alongside this note is for that exact local candidate only,
not remote CI or the subsequent active feature tasks. No prior preview artifact
was replaced. Integrity verification does not authenticate a publisher or prove
content-to-Git provenance. Native/arbitrarily-old undo and Mac shortcut limits
remain explicit.
