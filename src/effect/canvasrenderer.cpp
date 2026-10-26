/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "canvasrenderer.h"
#include "infinitedesktopeffect.h"

#include "core/colorspace.h"
#include "core/output.h"
#include "core/region.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "opengl/glvertexbuffer.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLinearGradient>
#include <QMatrix4x4>
#include <QPainter>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace InfiniteDesktop
{

namespace
{

constexpr qreal MinimumGridScreenSpacing = 18.0;
constexpr int MaximumGridDots = 40000;
constexpr int MaximumTileTextureWidth = 1024;

QRectF rectOf(const KWin::LogicalOutput *output)
{
    const KWin::RectF geometry = output->geometryF();
    return QRectF(geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

void renderClipped(KWin::GLVertexBuffer *vbo, const KWin::Region &deviceRegion)
{
    if (deviceRegion == KWin::Region::infinite()) {
        vbo->render(GL_TRIANGLES);
    } else {
        vbo->render(deviceRegion, GL_TRIANGLES, true);
    }
}

QImage renderTileImage(const QSize &size, const QIcon &icon, const QString &caption)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal w = size.width();
    const qreal h = size.height();
    const qreal shortSide = std::min(w, h);
    const qreal radius = std::max<qreal>(6.0, shortSide * 0.07);
    const QRectF card(0.5, 0.5, w - 1.0, h - 1.0);

    QLinearGradient background(card.topLeft(), card.bottomLeft());
    background.setColorAt(0.0, QColor(44, 47, 56, 238));
    background.setColorAt(1.0, QColor(26, 28, 34, 238));
    painter.setPen(QPen(QColor(255, 255, 255, 46), std::max<qreal>(1.0, shortSide / 220.0)));
    painter.setBrush(background);
    painter.drawRoundedRect(card, radius, radius);

    const qreal iconSize = shortSide * 0.42;
    const QRectF iconRect((w - iconSize) / 2.0, h * 0.44 - iconSize / 2.0, iconSize, iconSize);
    const QIcon effectiveIcon = icon.isNull() ? QIcon::fromTheme(QStringLiteral("application-x-executable")) : icon;
    effectiveIcon.paint(&painter, iconRect.toAlignedRect());

    QFont font = QGuiApplication::font();
    font.setPixelSize(std::max(10, int(shortSide * 0.075)));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 220));
    const QFontMetricsF metrics(font);
    const QString text = metrics.elidedText(caption, Qt::ElideRight, w * 0.86);
    const QRectF textRect(w * 0.07, iconRect.bottom() + h * 0.06, w * 0.86, metrics.height() * 1.4);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);

    // A small "play" badge: the tile restores the fullscreen window.
    const qreal badge = shortSide * 0.11;
    const QRectF badgeRect(w - badge * 1.6, badge * 0.6, badge, badge);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 40));
    painter.drawEllipse(badgeRect);
    QPolygonF triangle;
    triangle << QPointF(badgeRect.left() + badge * 0.38, badgeRect.top() + badge * 0.28) << QPointF(badgeRect.left() + badge * 0.38, badgeRect.bottom() - badge * 0.28)
             << QPointF(badgeRect.right() - badge * 0.26, badgeRect.center().y());
    painter.setBrush(QColor(255, 255, 255, 210));
    painter.drawPolygon(triangle);
    painter.end();
    return image;
}

QImage renderMarkerImage(int pixels, const QIcon &icon)
{
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal size = pixels;
    painter.setPen(QPen(QColor(255, 255, 255, 80), std::max<qreal>(1.0, size / 40.0)));
    painter.setBrush(QColor(32, 35, 42, 225));
    painter.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    const qreal iconSize = size * 0.58;
    const QIcon effectiveIcon = icon.isNull() ? QIcon::fromTheme(QStringLiteral("application-x-executable")) : icon;
    effectiveIcon.paint(&painter, QRectF((size - iconSize) / 2.0, (size - iconSize) / 2.0, iconSize, iconSize).toAlignedRect());
    painter.end();
    return image;
}

} // namespace

CanvasRenderer::CanvasRenderer() = default;
CanvasRenderer::~CanvasRenderer() = default;

void CanvasRenderer::forget(KWin::EffectWindow *window)
{
    m_tiles.erase(window);
    m_markers.erase(window);
}

KWin::GLTexture *CanvasRenderer::markerTexture(KWin::EffectWindow *window, int pixelSize)
{
    CachedTile &cached = m_markers[window];
    if (!cached.texture || cached.size.width() != pixelSize) {
        cached.texture = KWin::GLTexture::upload(renderMarkerImage(pixelSize, window->icon()));
        if (cached.texture) {
            cached.texture->setFilter(GL_LINEAR);
            cached.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        }
        cached.size = QSize(pixelSize, pixelSize);
    }
    return cached.texture.get();
}

void CanvasRenderer::paintMarkers(const Canvas &canvas, const QList<OffscreenMarker> &markers, const KWin::RenderTarget &renderTarget,
                                  const KWin::RenderViewport &viewport, const KWin::Region &deviceRegion)
{
    if (!KWin::effects->openglContext()) {
        return;
    }
    const qreal scale = viewport.scale();
    const qreal alpha = std::clamp(canvas.markerAlpha, 0.0, 1.0);
    const qreal radius = OffscreenMarker::Size / 2.0;
    const int pixels = std::max(16, int(std::lround(OffscreenMarker::Size * scale)));

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Arrows towards the windows, just outside of each marker.
    QList<QVector2D> vertices;
    vertices.reserve(markers.size() * 3);
    for (const OffscreenMarker &marker : markers) {
        const QPointF normal(-marker.direction.y(), marker.direction.x());
        const QPointF tip = marker.position + marker.direction * (radius + 9.0);
        const QPointF base = marker.position + marker.direction * (radius + 1.0);
        vertices << QVector2D(tip * scale) << QVector2D((base + normal * 6.0) * scale) << QVector2D((base - normal * 6.0) * scale);
    }
    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(vertices);
    {
        KWin::ShaderBinder binder(KWin::ShaderTrait::UniformColor | KWin::ShaderTrait::TransformColorspace);
        binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
        binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
        const qreal a = 0.9 * alpha;
        binder.shader()->setUniform(KWin::GLShader::ColorUniform::Color, QColor::fromRgbF(a, a, a, a));
        renderClipped(vbo, deviceRegion);
    }

    KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::Modulate | KWin::ShaderTrait::TransformColorspace);
    binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
    binder.shader()->setUniform(KWin::GLShader::Vec4Uniform::ModulationConstant, QVector4D(alpha, alpha, alpha, alpha));
    for (const OffscreenMarker &marker : markers) {
        KWin::GLTexture *texture = markerTexture(marker.window->effectWindow, pixels);
        if (!texture) {
            continue;
        }
        QMatrix4x4 mvp = viewport.projectionMatrix();
        mvp.translate((marker.position.x() - radius) * scale, (marker.position.y() - radius) * scale);
        binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
        if (deviceRegion == KWin::Region::infinite()) {
            texture->render(QSizeF(pixels, pixels));
        } else {
            texture->render(deviceRegion, QSizeF(pixels, pixels), true);
        }
    }
    glDisable(GL_BLEND);
}

void CanvasRenderer::paintCanvasLayer(const InfiniteDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                                      const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion)
{
    if (!output || !KWin::effects->openglContext()) {
        return;
    }
    if (effect.settings().grid != EffectSettings::Grid::Never && canvas.gridAlpha > 0.0) {
        paintGrid(effect, canvas, renderTarget, viewport, output, deviceRegion);
    }
    if (!effect.settings().tileLiveContent) {
        paintTiles(effect, canvas, renderTarget, viewport, output, deviceRegion);
    }
}

void CanvasRenderer::paintGrid(const InfiniteDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                               const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion)
{
    const EffectSettings &settings = effect.settings();
    const QRectF screen = rectOf(output);
    const Camera &camera = canvas.camera;

    // Level of detail: when zooming out, only every 4th dot survives.
    qreal spacing = std::max<qreal>(settings.gridSpacing, 4.0);
    for (int i = 0; i < 12 && spacing * camera.zoom < MinimumGridScreenSpacing; ++i) {
        spacing *= 4.0;
    }
    const QRectF visible = camera.visibleCanvasRect(screen);
    qreal firstX = std::floor(visible.left() / spacing) * spacing;
    qreal firstY = std::floor(visible.top() / spacing) * spacing;
    int columns = int(std::ceil((visible.right() - firstX) / spacing)) + 1;
    int rows = int(std::ceil((visible.bottom() - firstY) / spacing)) + 1;
    while (columns * rows > MaximumGridDots) {
        spacing *= 2.0;
        firstX = std::floor(visible.left() / spacing) * spacing;
        firstY = std::floor(visible.top() / spacing) * spacing;
        columns = int(std::ceil((visible.right() - firstX) / spacing)) + 1;
        rows = int(std::ceil((visible.bottom() - firstY) / spacing)) + 1;
    }
    if (columns <= 0 || rows <= 0) {
        return;
    }

    const qreal scale = viewport.scale();
    const qreal half = std::max<qreal>(0.75, 1.1 * scale) * std::clamp(camera.zoom * 1.5, 0.7, 1.4);
    QList<QVector2D> vertices;
    vertices.reserve(columns * rows * 6);
    for (int column = 0; column < columns; ++column) {
        for (int row = 0; row < rows; ++row) {
            const QPointF point = camera.canvasToScreen(QPointF(firstX + column * spacing, firstY + row * spacing), screen) * scale;
            const float left = float(point.x() - half);
            const float right = float(point.x() + half);
            const float top = float(point.y() - half);
            const float bottom = float(point.y() + half);
            vertices << QVector2D(left, top) << QVector2D(right, top) << QVector2D(right, bottom)
                     << QVector2D(right, bottom) << QVector2D(left, bottom) << QVector2D(left, top);
        }
    }

    const qreal alpha = std::clamp(settings.gridOpacity * canvas.gridAlpha, 0.0, 1.0);
    const QColor base = settings.gridColor;
    const QColor premultiplied = QColor::fromRgbF(base.redF() * alpha, base.greenF() * alpha, base.blueF() * alpha, alpha);

    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(vertices);

    KWin::ShaderBinder binder(KWin::ShaderTrait::UniformColor | KWin::ShaderTrait::TransformColorspace);
    binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
    binder.shader()->setUniform(KWin::GLShader::ColorUniform::Color, premultiplied);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    renderClipped(vbo, deviceRegion);
    glDisable(GL_BLEND);
}

KWin::GLTexture *CanvasRenderer::tileTexture(KWin::EffectWindow *window, const QSize &pixelSize)
{
    CachedTile &cached = m_tiles[window];
    const QString caption = window->caption();
    // Re-render only when the size changes noticeably, so zooming stays cheap.
    const bool sizeChanged = !cached.texture || std::abs(cached.size.width() - pixelSize.width()) > std::max(24, cached.size.width() / 5);
    if (sizeChanged || cached.caption != caption) {
        cached.texture = KWin::GLTexture::upload(renderTileImage(pixelSize, window->icon(), caption));
        if (cached.texture) {
            cached.texture->setFilter(GL_LINEAR);
            cached.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        }
        cached.size = pixelSize;
        cached.caption = caption;
    }
    return cached.texture.get();
}

void CanvasRenderer::paintTiles(const InfiniteDesktopEffect &effect, const Canvas &canvas, const KWin::RenderTarget &renderTarget,
                                const KWin::RenderViewport &viewport, KWin::LogicalOutput *output, const KWin::Region &deviceRegion)
{
    const QRectF screen = rectOf(output);
    const qreal scale = viewport.scale();
    for (const auto &[w, managed] : effect.managedWindows()) {
        if (managed->canvasKey != canvas.key || !managed->tilePos || effect.modeOf(*managed) != WindowMode::Tile) {
            continue;
        }
        const QRectF tile = canvas.camera.canvasToScreen(effect.tileCanvasRect(*managed, canvas), screen);
        if (!tile.intersects(screen) || tile.width() < 4 || tile.height() < 4) {
            continue;
        }
        QSizeF pixels = tile.size() * scale;
        if (pixels.width() > MaximumTileTextureWidth) {
            pixels *= MaximumTileTextureWidth / pixels.width();
        }
        KWin::GLTexture *texture = tileTexture(w, pixels.toSize().expandedTo(QSize(16, 16)));
        if (!texture) {
            continue;
        }
        KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::TransformColorspace);
        QMatrix4x4 mvp = viewport.projectionMatrix();
        mvp.translate(tile.x() * scale, tile.y() * scale);
        binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
        binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (deviceRegion == KWin::Region::infinite()) {
            texture->render(tile.size() * scale);
        } else {
            // The scissor region is in framebuffer coordinates, independent of the MVP matrix.
            texture->render(deviceRegion, tile.size() * scale, true);
        }
        glDisable(GL_BLEND);
    }
}

} // namespace InfiniteDesktop
