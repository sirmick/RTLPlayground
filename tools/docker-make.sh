#!/bin/sh
# docker-make.sh [make args...] - build in the dev container (see the README's
# Docker section), in parallel, from this checkout or any git worktree of it.
#
#   tools/docker-make.sh MACHINE=SWGT024_V2_0_UNMANAGED LAYOUT=compact
#
# The repository's shared .git is mounted too, so `git describe` (the version
# string) works from a worktree. IMAGE overrides the image name (default
# rtlplayground-dev:main, built from the Dockerfile).
set -e
top=$(git rev-parse --show-toplevel)
common=$(cd "$top" && cd "$(git rev-parse --git-common-dir)" && pwd)
image=${IMAGE:-rtlplayground-dev:main}
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
	-v "$top:$top" -v "$common:$common" -w "$top" "$image" \
	sh -c 'git config --global --add safe.directory "*"; exec make -j"$(nproc)" "$@"' make "$@"
