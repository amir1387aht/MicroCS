#!/bin/sh
# CI-only Ubuntu dependency installation. Never wait six hours for a bad mirror.
set -eu
if [ "$#" -eq 0 ]; then
    echo "usage: sh tools/ci_apt.sh package [...]" >&2
    exit 2
fi
# Hosted runners use a mirror list which can select an Azure HTTP endpoint that
# stalls on large Arm toolchain packages. Use official HTTPS repositories for
# this invocation only; do not rewrite the runner's system sources or disable
# signature verification. Derive the suite/architecture from the actual runner.
suite=$(lsb_release -sc)
case "$suite" in ''|*[!a-z]*) echo "Invalid Ubuntu suite: $suite" >&2; exit 2;; esac
case "$(dpkg --print-architecture)" in
    amd64|i386) archive=https://archive.ubuntu.com/ubuntu; security=https://security.ubuntu.com/ubuntu;;
    *) archive=https://ports.ubuntu.com/ubuntu-ports; security=$archive;;
esac
sources=$(mktemp -d)
trap 'rm -rf "$sources"' EXIT
trap 'exit 130' HUP INT TERM
chmod 755 "$sources"
cat > "$sources/ubuntu.list" <<SOURCES
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] $archive $suite main restricted universe multiverse
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] $archive $suite-updates main restricted universe multiverse
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] $security $suite-security main restricted universe multiverse
SOURCES
chmod 644 "$sources/ubuntu.list"
# Keep both per-request waits and the complete operation bounded. APT update must
# fail on an unreachable index instead of silently using incomplete stale lists.
apt_run() {
    budget=$1
    shift
    timeout --kill-after=15s "$budget" sudo -n env DEBIAN_FRONTEND=noninteractive \
        apt-get -o "Dir::Etc::sourcelist=$sources/ubuntu.list" \
        -o Dir::Etc::sourceparts=- -o APT::Get::List-Cleanup=0 \
        -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 \
        -o Acquire::Retries=2 -o DPkg::Lock::Timeout=60 "$@"
}
apt_run 5m update -q -o APT::Update::Error-Mode=any
apt_run 10m install -y -q --no-install-recommends "$@"
