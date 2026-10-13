# Image loader metadata contract (v2 WIP)

This is a bounded hardening step, **not complete verification of untrusted
bytecode**. Execution still requires trusted compiler output, as described in
[SECURITY.md](../SECURITY.md). No security-on mode, authenticated shell, signing
protocol, board trust chain, sandbox, or independent audit is established here.

## Compatibility

Public C APIs, opcode numbering and image versions 1–4 remain unchanged. Fixed
width v1/v2 and compact v3/v4 images use the same loader in copy and XIP modes;
public disassembly and execution go through that loader. `FN_XIP` is still
accepted on disk and masked: code-buffer ownership is determined by the actual
load mode, not by artifact metadata. XIP still requires the caller to keep its
image buffer valid and immutable for every use.

The loader now rejects metadata that the previous loader tolerated:

- noncanonical unsigned varints with redundant final zero bytes, or a tenth
  byte with more than one payload bit (including continuation). Canonical
  ten-byte `UINT64_MAX` remains valid, including zigzag `INT64_MIN` constants;
- slot counts outside 1–65535, checked **before** narrowing, or fewer than
  `arity + 1` slots (callee/receiver slot followed by parameter slots);
- minimum arity greater than declared arity, unknown function flag bits,
  zero-arity `params` functions, nonboolean parameter-type-present flags,
  zero-arity parameter type arrays, or tags outside the current `PT_*` enum;
- an entry function with arguments, minimum arguments, captures, or `params`;
- empty code bodies, and closure-capture kinds other than exactly 0 or 1.

There is no migration or format bump for compiler-produced images. Custom image
writers must emit canonical varints and valid metadata. Rejection uses the
existing `MCS_ERR_BYTECODE` API result. The instruction-boundary bitmap's byte
count also avoids an overflowing `clen + 8` intermediate.

## Evidence and limitations

`tests/c/test_image_metadata.c` builds minimal, deterministic v1–v4 fixtures.
Positive fixtures exercise nested functions, local captures, static/constructor
flags, all parameter type tags, unsigned/zigzag integer boundaries, serialized
XIP masking and the maximum slot metadata of an **uncalled** child. Negative
fixtures exercise the above metadata rules plus every truncated prefix of a
valid minimal image. Copy/XIP load, public disassembly and public execution
rejection are checked with fresh VMs and tracked teardown allocations.

Negative fixtures reach public execution only after the independent load has
rejected them. Baseline mode **never executes malformed images**. Its frozen
pre-change run accepted 104 malformed loads and recorded eight incorrect old-size
hints from the legacy zero-arity type-array path (one allocated byte freed with
an old-size hint of zero). The original sum-of-hints tracker incorrectly carried
that discrepancy into later trials; an independent aligned allocation-header
tracker distinguished it from a leak. The baseline then passed all 409 fixture
checks with all actual allocations freed. Hardening rejects that metadata before
allocating the array; current fixtures require zero size-hint mismatches.

Run the targeted default fixture with `make image-metadata-test`, the nine
configuration matrix with `python3 tools/test_image_metadata.py --sanitize`, or
CMake/CTest with `cmake -S tests/c/image_metadata -B build/image-metadata-cmake`.
The matrix covers default, GC stress, compiler OFF, XIP OFF, compact values,
INT64, float32, float OFF and the min profile. CI also invokes the sanitizer
matrix. Sanitizer runs are not replaced by synthetic headers or mocked loaders.

Full stack balance, initialized-local tracking, fallthrough, operand types and
all exceptional/control-flow paths are **still unverified**. Native execution
of arbitrary mutated bytecode remains unsafe; `tools/fuzz.py --image` stays
load-and-disassemble-only. Existing index/jump checks plus this step cannot be
called malicious-artifact verification. Missing full verification and the other
security/fullscope requirements still block release.

A same-flags GCC11.5 x86-64 `-Os` bytecode object measurement is recorded with
this checkpoint. It is not MCU flash, full firmware size, runtime speed, stack
usage, or hardware evidence. Loader timing and wider performance trade-offs
must be measured separately; there is no new speed claim from this step.
