#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Initialise core/shadPS4 and its nested dependencies at the pinned revisions.
# One failing dependency must not leave the others unchecked-out, so each is
# updated on its own.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
git submodule update --init --depth 1 core/shadPS4 || exit 1
cd core/shadPS4
failed=""
for path in $(git config -f .gitmodules --get-regexp 'submodule\..*\.path' | awk '{print $2}'); do
    if ! git submodule update --init --recursive --depth 1 "$path" >/dev/null 2>&1; then
        failed="$failed $path"
    fi
done
# fdk-aac is hosted on android.googlesource.com upstream. Where that host is not
# reachable, the identical commit (same hash) is available from the GitHub mirror.
case "$failed" in
*externals/aacdec/fdk-aac*)
    if git -c submodule.externals/aacdec/fdk-aac.url=https://github.com/mstorsjo/fdk-aac \
        submodule update --init --depth 1 externals/aacdec/fdk-aac; then
        failed=$(echo "$failed" | sed 's| externals/aacdec/fdk-aac||')
    fi
    ;;
esac
if [ -n "$failed" ]; then
    echo "failed to fetch:$failed" >&2
    exit 1
fi
echo "core and dependencies ready"
