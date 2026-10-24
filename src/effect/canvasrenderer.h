/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QList>
#include <QSize>
#include <QString>

#include <memory>
#include <unordered_map>

namespace KWin
{
class EffectWindow;
class GLTexture;
class LogicalOutput;
class Region;
class RenderTarget;
class RenderViewport;
}

namespace InfinitDesktop
{

class InfinitDesktopEffect;
struct Canvas;
struct OffscreenMarker;

/**
 * Draws what belongs to the canvas itself and is not a window: the optional
 * dot grid and the "application icon" tiles of minimized fullscreen windows.
 * Both are painted right after the desktop window (the wallpaper), i.e. above
 * the wallpaper and below all windows. The wallpaper itself is never touched.
 *
 * Only KWin's own GL helpers are used (no extensions, no custom shaders), so
 * the result is the same on Mesa and on the NVIDIA driver.
 */
class CanvasRenderer
{
public:
    CanvasRenderer();
    ~CanvasRenderer();

    void paintCanvasLayer(const InfinitDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                          const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion);
    /// Markers at the screen edge for windows outside the view; painted on top of everything.
    void paintMarkers(const Canvas &canvas, const QList<OffscreenMarker> &markers, const KWin::RenderTarget &renderTarget,
                      const KWin::RenderViewport &viewport, const KWin::Region &deviceRegion);

    /// Drops cached textures of a window that is gone.
    void forget(KWin::EffectWindow *window);

private:
    void paintGrid(const InfinitDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                   const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion);
    void paintTiles(const InfinitDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                    const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion);
    KWin::GLTexture *tileTexture(KWin::EffectWindow *window, const QSize &pixelSize);
    KWin::GLTexture *markerTexture(KWin::EffectWindow *window, int pixelSize);

    struct CachedTile
    {
        std::unique_ptr<KWin::GLTexture> texture;
        QSize size;
        QString caption;
    };
    std::unordered_map<KWin::EffectWindow *, CachedTile> m_tiles;
    std::unordered_map<KWin::EffectWindow *, CachedTile> m_markers;
};

} // namespace InfinitDesktop
