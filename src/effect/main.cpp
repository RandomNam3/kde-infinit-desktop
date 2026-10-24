/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "infinitdesktopeffect.h"

namespace InfinitDesktop
{

KWIN_EFFECT_FACTORY_SUPPORTED(InfinitDesktopEffect, "metadata.json", return InfinitDesktopEffect::supported();)

} // namespace InfinitDesktop

#include "main.moc"
