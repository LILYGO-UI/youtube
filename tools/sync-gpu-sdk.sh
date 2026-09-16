#!/bin/sh
set -eu

target=${1:?Usage: sh tools/sync-gpu-sdk.sh user@device}
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT HUP INT TERM

ssh "$target" 'set -eu
    staging=$(mktemp -d)
    trap '\''rm -rf "$staging"'\'' EXIT HUP INT TERM
    cd "$staging"
    apt-get download libegl-dev libgles-dev libgbm-dev libgl-dev libegl1 libgles2 libgbm1 libglvnd0 >&2
    mkdir root
    for package in ./*.deb; do dpkg-deb -x "$package" root; done
    tar -czf - -C root usr/include usr/lib/aarch64-linux-gnu' > "$archive"
mkdir -p "$project_dir/.cache/gpu-sdk"
tar -xzf "$archive" -C "$project_dir/.cache/gpu-sdk"
