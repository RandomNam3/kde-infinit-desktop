#!/bin/bash
# Creates the test video of the demo and of the documentation recordings: moving colour
# gradients with a title and a running time code, so that it reads as a video.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: make-testvideo.sh OUTPUT.mp4 [SECONDS]
set -eu
out=$1
seconds=${2:-120}
font=/usr/share/fonts/TTF/DejaVuSans-Bold.ttf
mkdir -p "$(dirname "$out")"
ffmpeg -loglevel error -y \
    -f lavfi -i "gradients=size=1280x720:rate=30:duration=${seconds}:speed=0.01:type=spiral:nb_colors=4:c0=0x1d99f3:c1=0x8e44ad:c2=0xf67400:c3=0x16a085" \
    -f lavfi -i "sine=frequency=330:duration=${seconds}" \
    -vf "drawtext=fontfile=${font}:text='Testvideo':x=64:y=56:fontsize=84:fontcolor=white:shadowx=3:shadowy=3:shadowcolor=black@0.5,drawtext=fontfile=${font}:text='KDE Infinit Desktop':x=68:y=158:fontsize=34:fontcolor=white@0.85,drawtext=fontfile=${font}:text='%{pts\:hms}':x=64:y=h-112:fontsize=46:fontcolor=white:box=1:boxcolor=black@0.45:boxborderw=14" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 64k -shortest "$out"
