#!/bin/sh
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# apk commit hook (Alpine / postmarketOS): rebuild KDE Infinit Desktop after
# KWin updates. Installed by scripts/install.sh as
#   /etc/apk/commit_hooks.d/kde-infinit-desktop.post-commit
HOOK=/usr/lib/kde-infinit-desktop/kwin-update-hook.sh
[ "${1:-}" = post-commit ] || exit 0
[ -x "$HOOK" ] || exit 0
"$HOOK" --background apk </dev/null >/dev/null 2>&1 || true
exit 0
