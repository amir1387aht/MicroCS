# Independent 416 core recovery milestone (not final approval)

Exact owner416f6ced54912628041ed76c74bebd8b369f7d09; B35 .c SHA25606bb90d9b57fcddaf098b20b734268a065ea5ba3d728f5941aa2c785db921b99 confirmed unchanged. Four-runtime-file diff reviewed; flag reader/writer search audited. Native complete-before-publication, original-position queue/applying ownership, partial-module pendingflag and external lookup root/pause restoration inspected.

BLOCKING portability finding: NEW `int code = setjmp(guard);` initializer violates ISO C99/C11 setjmp invocation context restrictions7.13.1.1p4. Owner accepted and is preparing narrow switch controlling-expression followup with exact valid error-code forwarding. Original416 stays immutable. Do not infer portability from GCC sanitizer passes; no unrelated setjmp-site rewrite requested.

Independent ASan/UBSan/leak: default owner36+B2309 PASS; GCstress owner36+B2309 PASS. Own reviewer_lazy_flags.c18/18 PASS under ASan/UBSan+GCstress, actual physical allocation tracking: SHARED_LAYOUT|PENDING bit borrowed destruction, own-layout COW clears only shared bit, pendingbit retained, parent fields/defaults remain valid, final zero physical allocations. Trusted internal storage invariants only, not a fault sweep/public API claim. No suppression or failures in these runs.

Commands: cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -DMCS_USER_CONFIG=0 [GC_STRESS=1] -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie src/*.c modules/*/*.c <exact-test.c> -lm -o <unique-binary>. LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs; ASAN_OPTIONS=detect_leaks=1:halt_on_error=1; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.

Disposable candidate also contains separately approved TFT/CAN and UNAPPROVED Studio32; not final combined approval. Original mixed-EOL full-harness Studio failure remains unexplained. Full-v2/phases incomplete; no physical device/protected-ref/release claim. Next: review exact narrow core followup then rerun relevant tests, and separately exact LVGL group feature/lifetimes. No source coauthoring or owner checkout edits.
