# Initial failures, retained rather than waived

- Exploratory `gen_stdlib_doc.py --help | head` was not a supported help request: the generator writes Markdown to stdout, and closing its pipe produced BrokenPipeError. It did not modify production sources. Correct full generator output was written to docs/STDLIB.md; only the new method and total member count changed.
- Initial exact-example regression failed all three source/copy/XIP executions with MissingMemberException for Hal. The headless harness deliberately had no HAL attached; the documented example correctly checks Hal.Has. Original log headless-with-example.log is retained. Correction attaches the actual empty HAL library (no peripheral callbacks) to each execution VM, not a fake Has implementation or skipped guard. The unchanged exact example and assertions must pass in all three modes.

- Final documentation invocation initially named nonexistent tools/check_wiki_links.py (exit2; raw final-wiki.log retained). Correct repository checker is tools/wiki_sync.py --check. No implementation change.
