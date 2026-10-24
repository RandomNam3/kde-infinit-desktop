#!/bin/bash
# Wrapper for scenario 5: optionally builds Better Blur DX (mounted at /betterblur)
# and enables a set of popular effects before the KWin session starts.
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
BLUR_PLUGIN=blur
if [ -d /betterblur ]; then
    if cmake -S /betterblur -B /bbbuild -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
             -DKDE_INSTALL_USE_QT_SYS_PATHS=ON > /tmp/betterblur.log 2>&1 \
       && cmake --build /bbbuild >> /tmp/betterblur.log 2>&1 && cmake --install /bbbuild >> /tmp/betterblur.log 2>&1; then
        echo "INFO: Better Blur DX built and installed"
        BLUR_PLUGIN=better_blur_dx
    else
        echo "INFO: Better Blur DX does not build against this KWin, using the built-in blur:"
        grep -E "error" /tmp/betterblur.log | head -8
    fi
fi
EXTRA_CONFIG="kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled $([ "$BLUR_PLUGIN" = blur ] && echo true || echo false);"
EXTRA_CONFIG+="kwriteconfig6 --file kwinrc --group Plugins --key better_blur_dxEnabled $([ "$BLUR_PLUGIN" = better_blur_dx ] && echo true || echo false);"
EXTRA_CONFIG+="kwriteconfig6 --file kwinrc --group Effect-better-blur-dx --key BlurNonMatching true;"
for effect in translucency wobblywindows magiclamp; do
    EXTRA_CONFIG+="kwriteconfig6 --file kwinrc --group Plugins --key ${effect}Enabled true;"
done
export EXTRA_CONFIG
# llvmpipe counts as software rendering, where KWin disables animated effects
# (wobbly windows, magic lamp, scripted effects) unless forced.
export KWIN_EFFECTS_FORCE_ANIMATIONS=1
exec bash /th/session.sh "$@"
