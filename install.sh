#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# KDE Infinit Desktop - one-command installer.
#
#   ./install.sh [OPTIONS]          from the source tree
#   curl -fsSL https://raw.githubusercontent.com/RandomNam3/kde-infinit-desktop/main/install.sh | bash
#   curl -fsSL .../install.sh | bash -s -- --yes      (options when piped)
#
# Thin wrapper around scripts/install.sh (see "./install.sh --help" and
# docs/INSTALL.md). When it is piped into bash there is no source tree next
# to it, so it downloads the sources first (branch main, or $KID_SOURCE_URL).
# Everything is inside main() so that "curl ... | bash" is safe.

set -eu

main() {
    local here='' url
    url=${KID_SOURCE_URL:-https://github.com/RandomNam3/kde-infinit-desktop/archive/refs/heads/main.tar.gz}

    if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
        here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
    fi
    if [ -n "$here" ] && [ -r "$here/scripts/install.sh" ]; then
        exec bash "$here/scripts/install.sh" "$@"
    fi

    # piped into bash: fetch the sources, run their installer (it keeps its own
    # copy in /usr/src for the automatic rebuild), then clean up
    local tmp rc=0
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/kde-infinit-desktop-src.XXXXXX")
    echo "==> Downloading KDE Infinit Desktop from $url"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" | tar -xz -C "$tmp" --strip-components=1 || rc=$?
    elif command -v wget >/dev/null 2>&1; then
        wget -qO- "$url" | tar -xz -C "$tmp" --strip-components=1 || rc=$?
    else
        echo "Error: neither curl nor wget found" >&2
        rc=1
    fi
    if [ "$rc" -eq 0 ] && [ -r "$tmp/scripts/install.sh" ]; then
        bash "$tmp/scripts/install.sh" --source "$tmp" "$@" || rc=$?
    elif [ "$rc" -eq 0 ]; then
        echo "Error: the download does not contain scripts/install.sh" >&2
        rc=1
    fi
    rm -rf -- "$tmp"
    return "$rc"
}

main "$@"; exit
