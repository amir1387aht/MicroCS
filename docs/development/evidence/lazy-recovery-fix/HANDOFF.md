# Bounded registration recovery handoff

Frozen commit `416f6ced54912628041ed76c74bebd8b369f7d09`, tree `d1641af035ec9ef8f8ab42f5474364a1867e9a0d`. Clean checkout. Four runtime files plus owner regression/notes and byte-identical independent frozen B regression/notes. Independent review PENDING.

All five ASan/UBSan/leak configurations pass (6,444 checks total): default 36+2309; GC stress 36+2309; eager registrations 17+1183; eager classes and both eager each 17+260. Canonical host source/copied/XIP diagnostics 85 passed, shell/readline and retained capability gates passed. Commands and all initial failures retained in this directory; inventory proves tested runtime/test bytes equal committed files.

Runtime reader/writer, linked ownership, nested application and class pending-bit audit: tests/c/test_lazy_recovery.md. Independent test sources exactly match 35e758f733e5de0219fef7d668538cedbac89058. Sustained-denial semantics, mixed eager-registration coverage limitation and semantic-only modes remain explicit. Counts differ from failing baseline because successful recovery follows different assertions.

First owner stage assertion incorrectly treated third allocation as unpublished metadata; source audit showed third was instance allocation after complete publication. That test remains and checks the complete class; separate second-allocation metadata failure checks unpublished class and same-VM retry. Original failures remain; no B regression altered.

No generic builtin OOM transaction, hostile input/image/authentication, physical board or full-v2 claim. LvGroup first-use failure must be rerun without prewarming; groups and combined candidate still unapproved. No protected merge, tag or release.
