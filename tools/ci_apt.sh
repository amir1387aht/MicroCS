#!/bin/sh
# CI-only Ubuntu dependency installation. Never wait six hours for a bad mirror.
set -eu
if [ "$#" -eq 0 ]; then
    echo "usage: sh tools/ci_apt.sh package [...]" >&2
    exit 2
fi
# Keep both per-request waits and the complete operation bounded. APT update must
# fail on an unreachable index instead of silently using incomplete stale lists.
apt_run() {
    budget=$1
    shift
    timeout --kill-after=15s "$budget" sudo -n env DEBIAN_FRONTEND=noninteractive \
        apt-get -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 \
        -o Acquire::Retries=2 -o DPkg::Lock::Timeout=60 "$@"
}
apt_run 5m update -q -o APT::Update::Error-Mode=any
apt_run 10m install -y -q --no-install-recommends "$@"
