# Host-only image stack-flow research

**Not a production verifier or permission to execute handcrafted images.**
`tests/c/experimental/test_image_stack_probe.c` is an isolated research tool.
It never calls image execution. The production library/runtime does not link or
invoke it, and `docs/SECURITY.md`'s trusted-compiler image requirement remains.

## Implemented experiment

After the actual loader accepts an image, the probe visits nested functions and
propagates frame-relative stack heights and installed-handler contexts through
reachable instruction boundaries. It models current base/fused opcode stack
effects, local accesses, closure capture bounds, both conditional successors,
`FOR_ITER`'s unequal success/failure heights, calls/returns, and `TRY` normal/catch
successors (`saved height + 1`, previous handler context). Merges must agree on
height/context; a loop cannot grow its stack on a backedge. Normal paths must
retain the callee slot and active handlers' saved stack heights. It rejects
reachable fallthrough and orphan `END_TRY`.

The self-test checks eleven expectations, including controlled underflow,
uninitialized-height local access, mismatched joins, growing loops, orphan
handlers and loss of saved handler stack height. Positive return/local/try paths
remain accepted. Every fixture is **load-only**, even when flow-rejected.

Crucially, the self-test deliberately demonstrates a **known type gap**:
`TRUE; TRUE; INHERIT; RETURN_NULL` has compatible heights and is accepted by the
loader/probe, but its class operand is invalid. It is never executed. Passing
these tests therefore cannot be called malicious-artifact validation, VM memory
safety, or a sandbox. No expected result was changed to pretend type verification
exists.

## Reproduction and measured compatibility

Configure the isolated test project, optionally with ASan/UBSan flags:

```sh
cmake -S tests/c/image_metadata -B build/stack-flow-research-cmake \
  -DCMAKE_C_FLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -Wall -Wextra -Werror" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
cmake --build build/stack-flow-research-cmake --target test_image_metadata test_image_stack_probe -j2
ctest --test-dir build/stack-flow-research-cmake --output-on-failure
make mcs
python3 tools/test_image_stack_probe.py --probe build/stack-flow-research-cmake/test_image_stack_probe
```

The corpus runner uses only repository scripts/templates, invokes the real
compiler in optimized and `-O0` modes, records source/image hashes and compiler
results, then loads/analyzes without execution. Current corpus: 27 test scripts
plus 217 C# templates = 244 sources, **488 images and 1346 function visits**.
All are accepted by this experimental analysis under ASan/UBSan/leak detection.
Three bounded empty/bad-magic/truncated file paths also reject without sanitizer
reports; this does not establish arbitrary-image/OOM safety.
This is compatibility evidence for the experiment, not execution or hardware
proof. CI includes this explicitly named research step, without changing the
production trust model.

## Preconditions before production integration

The dense PC-indexed host tables use standard `calloc` and are **not** quota
accounted or acceptable as an embedded/low-RAM design. The command limits files
to 1 MB, but that is not a security-mode resource policy. No low-RAM/default
footprint or runtime speed improvement is claimed.

Remaining design/implementation/test work includes:

- compact bounded basic-block/worklist storage with explicit allocation/OOM,
  arithmetic, time/step, and GC/rooting behavior on actual 32-bit builds;
- operand-type analysis and runtime guards for dynamic types before unchecked
  class/string/instance casts; signatures do not replace those checks;
- captured-slot/open-upvalue lifetime, initialized values/types and exceptional
  edges beyond stack heights; handler/type/resource joins need further testing;
- derive/validate actual evaluation peaks and safe call reservations without
  rejecting valid compiler outputs or overflowing pointer arithmetic;
- every feature/profile, old images/copy/XIP/disassembly, larger programs, nested
  exception/capture/callback/GC cases and hostile mutation/OOM corpora;
- independent security review and integration with the separate optional auth,
  signing/policy/atomic-update/board trust-chain work, still unimplemented.

Do not wire this prototype into production or enable untrusted execution merely
because the compatibility corpus is green. Whole-scope v2 release remains blocked.
