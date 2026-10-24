#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libzypp commit plugin (openSUSE): rebuild KDE Infinit Desktop after KWin
# updates. Installed by scripts/install.sh as
#   /usr/lib/zypp/plugins/commit/kde-infinit-desktop
#
# Protocol: libzypp sends STOMP-like frames (COMMAND, headers, empty line,
# body, NUL) on stdin and expects an "ACK" frame for each. stdout belongs to
# the protocol, so nothing else may be printed there. After COMMITEND the
# rebuild is started detached; it waits until zypper has finished.

HOOK=/usr/lib/kde-infinit-desktop/kwin-update-hook.sh
commit_seen=0

ack() { printf 'ACK\n\n\0'; }

while IFS= read -r -d '' frame; do
    command=${frame%%$'\n'*}
    case $command in
        COMMITEND)
            # only rebuild if a kwin package was part of the transaction
            if [[ $frame =~ \"n\":[[:space:]]*\"(lib)?kwin[0-9]*(-[a-z]+)?\" ]]; then
                commit_seen=1
            fi
            ack
            ;;
        PLUGINEND | _DISCONNECT)
            ack
            break
            ;;
        *) ack ;;
    esac
done

if [ "$commit_seen" -eq 1 ] && [ -x "$HOOK" ]; then
    "$HOOK" --background zypper </dev/null >/dev/null 2>&1 || true
fi
exit 0
