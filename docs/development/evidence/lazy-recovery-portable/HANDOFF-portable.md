# Final portable guard follow-up

Frozen source `2daf6239846d41f6b677c8a264e04e78242edf64`, tree `1ee960fc22d10059b4e666de10e2392ed3035294`, parent 416f6ced54912628041ed76c74bebd8b369f7d09. Exactly three follow-up files: mcs_lib.c and owner regression/notes. All prior evidence/checkpoints remain immutable; independent core/group/combined approval PENDING.

Uses switch(setjmp(guard)) as the entire permitted controlling expression. Every supported mcs_result_t error is forwarded exactly with original message/previous target and retryable marker. Invalid internal codes reject as runtime error. No existing compiler/VM setjmp sites changed; no extra VM/class storage. Five new native forwarding cases verify original target, exact result/message, queue marker, same-VM retry and zero physical leftovers.

Final portable-guard-02 matrix all PASS6524: default/GC owner76+Bunchanged2309 each; eager registrations17+1183; eager classes and both eager17+260 each. Host85 source/image/XIP, shell/readline and capabilities PASS. Final source inventory matches committed bytes; no final compiler warnings. B35 tests remain byte-identical.

portable-guard-01 was development-only: conservative GCC test-loop-index clobber warning; corrected index volatile. Tool cancellation initially left nested script running; exact old process tree terminated, logs retained. Only final02 outputs count. Runtime source stayed frozen during final02; no test assertion was waived.

Earlier416 candidate independently passed B6321 and reviewer default/GC+18 shared-layout checks, but remains rejected for its new setjmp initializer context. This follow-up needs exact independent reproduction, not inherited approval. Real LVGL8/9 original first-use regression passed949 each on416, yet must rerun on this exact final core with group code. Not generic all-OOM, builtin initialization, hostile-image/security, hardware or full-v2 completion. No main/preview integration, tag or release.
