# Security policy

## Reporting a vulnerability

Please do **not** open a public issue for security problems (a script that escapes the VM,
corrupts memory, crashes the firmware, breaks the VFS confinement, ...). Report it privately
through **[GitHub security advisories](https://github.com/amir1387aht/MicroCS/security/advisories/new)**
with a script or steps that reproduce it. You will get an answer within a few days, and a fix
is released as soon as it is ready, with credit if you want it.

## Supported versions

Fixes go into the latest release (see [CHANGELOG.md](CHANGELOG.md)).

## Security model

What MicroCS does and does not protect against (memory safety, heap and time limits, module
whitelisting, VFS confinement) is described in [docs/SECURITY.md](docs/SECURITY.md).
