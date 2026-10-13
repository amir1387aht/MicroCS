#!/bin/sh
# Completed local gates; CHROME resolves actual installed Chromium. Web Serial
# is test-boundary glue to the real ./mcs shell, not simulated protocol results.
set -eu
export CHROME=$(command -v chromium)
node tests/studio/test_project.js ./mcs
node tests/studio/test_project_layout.js
node tests/studio/test_project_browser.js ./mcs
node tests/studio/test_studio.js ./mcs
node tests/studio/test_gallery_layout.js
node tests/studio/test_completion_context.js
node tests/studio/test_scope.js
node tests/studio/test_lexical.js
node tests/studio/test_highlight.js
node tests/studio/test_format.js ./mcs
node tests/studio/test_templates.js ./mcs
node tests/studio/test_capabilities.js
python3 tests/test_template_metadata.py
python3 tools/wiki_sync.py --check
