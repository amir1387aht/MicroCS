# Tests-only EXPECTED-FAIL baseline checkpoint

Frozen original test source: 35e758f733e5de0219fef7d668538cedbac89058, tree 838897f95202348cea554597b4dea8fbbd2eb8b7. This checkpoint intentionally preserves failures on unchanged baseline runtime, not a fix or approval.

Sustained allocation denial begins at a selected reached allocation until C restores the allocator; this is not single-shot injection. Actual physical-pointer ownership is measured before test cleanup. Four no-fault/unhit controls remain required. Default/GC and eager-registration configurations have expected failures; eager-class and both-eager passes are semantic controls only. Mixed eager-registration module scenario is not equivalent fault coverage; see the frozen notes. No hostile input, authentication, image safety or general OOM transaction guarantee is claimed.

Immutable restore-verified baseline backup: /data/backups/v2-mission-lazy-module-tests/35e758f733e5de0219fef7d668538cedbac89058/. Exact fixed-runtime reruns and independent approval are still pending. No main/preview merge, release or full-v2 completion.
