#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Put core/shadPS4 on a local branch at the pinned upstream commit and apply
# core/patches/*.patch on top. Safe to re-run: the branch is rebuilt each time.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
pinned=$(git -C "$root" ls-tree HEAD core/shadPS4 | awk '{print $3}')
cd "$root/core/shadPS4"
if [ -n "$(git status --porcelain --untracked-files=no --ignore-submodules=all)" ]; then
    echo "core/shadPS4 has uncommitted changes; commit or stash them first" >&2
    exit 1
fi
git checkout -q -B ps4droid/android "$pinned"
git -c user.name="ps4droid" -c user.email="ps4droid@localhost" \
    am -q --committer-date-is-author-date "$root"/core/patches/*.patch
echo "applied $(ls "$root"/core/patches/*.patch | wc -l) patches on $pinned"
