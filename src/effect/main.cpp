/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "infinitedesktopeffect.h"

namespace InfiniteDesktop
{

KWIN_EFFECT_FACTORY_SUPPORTED(InfiniteDesktopEffect, "metadata.json", return InfiniteDesktopEffect::supported();)

} // namespace InfiniteDesktop

#include "main.moc"
