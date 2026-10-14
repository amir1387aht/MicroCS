# Independent CAN FD / TFT combined review

Decision: APPROVE CAN FD owner865ce88522cb1a76c34053449f27e5ab4f4ff85f for local disposable integration testing. No physical CAN backend, full-v2/phase completion, protected merge, remote CI or release approval.

Frozen candidate8943b5e437eb6e4512819e2b8da86649fa9647c0 tree5d1cca026634831d8ed93d5eeb9b97b626e45591. Contains exact TFT b7c5650 and CAN865ce88 plus reviewer-only dedicated oracle tests. Only integration conflict was capability EOF: every b4 line preserved exactly, then TFT.SpriteScroll, CAN.FD. Existing IDs retained; both new entries present. git diff --check and clean status PASS.

Reviewed separate classic-compatible frame/API and append-only C callbacks, canonical DLC/padding and strict native metadata validation, complete-callback capability query and perbus support, simulator copies/queue/rates, exclusive ownership, callback roots/global+deferred generation cleanup, teardown/failure semantics, BLE identity8 vs BLE7 regression, documented ABI/profile/physical limits. No blocker found in this bounded slice.

Independently reproduced ASan/UBSan/leak representative matrix on local combined source: default512, GCstress512, INT64516, compilerOFF315, queueOFF319, minHAL301 =2475 PASS. MinHAL correctly uses genuine compiler NO_OPT images for documented superops0 profile. Owner full11-variant4160 evidence inspected, not all variants independently repeated.

Independent public-API oracle11858 checks PASS with ASan/UBSan+GCstress, then exact committed runner repeated PASS. All application lengths0..64 x standard/extended x BRS: actual transmitted bytes, independent canonical-length table/zero padding, source-input isolation after mutation/GC. Native eight-frame FIFO copy/order/fullness/empty, VM cleanup vs embedding-owned bus, unowned Close/free preservation and VM-owned free cleanup. No physical traffic or handcrafted images.

Combined genuine pinned TFT normal checks131558/example11 and reviewer immutable snapshot263079 checks PASS. Separate TFT-only sanitizer+GC evidence at preceding bbdf56d milestone also PASS. Combined TFT sanitizer not rerun after adding FD; this is explicit scope, not a hidden pass.

Classic host build/canonical tests85 PASS; Python shell/readline PASS; make capabilities-test PASS (three native configurations +Studio vocabulary/templates/metadata/package checks). Capability-off behavior remains tested. No port-binding suites used in this increment. Previously protected branches untouched.

Initial reviewer failures preserved, not owner defects: matrix missing LD_LIBRARY_PATH could not load libasan.so.6(exit127); corrected new output uses documented environment. Own oracle first compile supplied nonexistent fourth exec_source length argument; corrected public API arity, original compile log retained. Guessed tests/run.sh entrypoint missing(exit127), corrected to sh tests/run_tests.sh ./mcs; original log retained. No runtime/product failure waived.

Commands/provenance: matrix-env-corrected/provenance.json; logs/own-exact.log; tests/can_fd/reviewer_payload_oracle.py --build-dir <newdir>, with LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. Runtime fixture images genuine trusted compiler outputs. Original full mission remains partial. No old-binary HAL table ABI guarantee; rebuild against appended header. Simulator is serialized software-only loopback, not arbitration/timing/physical CAN. Best-effort teardown close-failure recovery remains embedding responsibility, as documented.
