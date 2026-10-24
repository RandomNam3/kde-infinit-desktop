/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "organizer.h"
#include "infinitdesktopeffect.h"
#include "inputfilter.h"
#include "logging.h"
#include "navigation.h"
#include "statestore.h"

#include "core/colorspace.h"
#include "core/output.h"
#include "core/region.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "input_event.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "opengl/glvertexbuffer.h"
#include "window.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPalette>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;

namespace InfinitDesktop
{

namespace
{

using SteadyClock = std::chrono::steady_clock;

constexpr auto LaunchTimeout = 30s;
constexpr auto ExpectedNoteTimeout = 10s;
constexpr auto NewNoteTimeout = 15s;
constexpr qreal GroupMargin = 32.0;
constexpr qreal MenuWidth = 280.0;
constexpr qreal MenuItemHeight = 32.0;
constexpr qreal MenuSeparatorHeight = 9.0;
constexpr qreal MenuPadding = 6.0;
constexpr qreal MinimumSelection = 12.0;
constexpr int MaxLabels = 256;
constexpr qreal MapWheelStep = 1.15;

const QString CompanionPath = QStringLiteral("/Companion");
const QString CompanionInterface = QStringLiteral("org.kde.InfinitDesktop.Companion");

// Pastel colors for areas, readable on light and dark wallpapers alike.
const QColor AreaColors[AreaStore::Colors] = {
    QColor(0x3d, 0xae, 0xe9), // blue
    QColor(0x27, 0xae, 0x60), // green
    QColor(0xf6, 0x74, 0x00), // orange
    QColor(0x9b, 0x59, 0xb6), // purple
    QColor(0xda, 0x44, 0x53), // red
    QColor(0x16, 0xa0, 0x85), // teal
    QColor(0xc9, 0xce, 0x3b), // olive
    QColor(0xe9, 0x3d, 0x8f), // pink
};
const QColor AccentColor(0x3d, 0xae, 0xe9);
const QColor NoteColor(0xf7, 0xd5, 0x4e);

QRectF rectOf(const KWin::LogicalOutput *output)
{
    const KWin::RectF geometry = output->geometryF();
    return QRectF(geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

QRectF toQRectF(const KWin::RectF &rect)
{
    return QRectF(rect.x(), rect.y(), rect.width(), rect.height());
}

QColor premultiplied(const QColor &color, qreal alpha)
{
    const qreal a = std::clamp(color.alphaF() * alpha, 0.0, 1.0);
    return QColor::fromRgbF(color.redF() * a, color.greenF() * a, color.blueF() * a, a);
}

void render(KWin::GLVertexBuffer *vbo, const KWin::Region &deviceRegion)
{
    if (deviceRegion == KWin::Region::infinite()) {
        vbo->render(GL_TRIANGLES);
    } else {
        vbo->render(deviceRegion, GL_TRIANGLES, true);
    }
}

/// Fills `rects` (logical, global coordinates) with one color.
void fillRects(const QList<QRectF> &rects, const QColor &color, qreal alpha, const KWin::RenderTarget &renderTarget,
               const KWin::RenderViewport &viewport, const KWin::Region &deviceRegion)
{
    if (rects.isEmpty() || alpha <= 0.0) {
        return;
    }
    const qreal scale = viewport.scale();
    QList<QVector2D> vertices;
    vertices.reserve(rects.size() * 6);
    for (const QRectF &rect : rects) {
        const QRectF r(rect.topLeft() * scale, rect.size() * scale);
        vertices << QVector2D(r.topLeft()) << QVector2D(r.topRight()) << QVector2D(r.bottomRight());
        vertices << QVector2D(r.topLeft()) << QVector2D(r.bottomRight()) << QVector2D(r.bottomLeft());
    }
    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(vertices);
    KWin::ShaderBinder binder(KWin::ShaderTrait::UniformColor | KWin::ShaderTrait::TransformColorspace);
    binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
    binder.shader()->setUniform(KWin::GLShader::ColorUniform::Color, premultiplied(color, alpha));
    render(vbo, deviceRegion);
}

/// The four edges of every rect as `width` wide bars, inside the rect.
QList<QRectF> outlines(const QList<QRectF> &rects, qreal width)
{
    QList<QRectF> bars;
    bars.reserve(rects.size() * 4);
    for (const QRectF &r : rects) {
        const qreal w = std::min({width, r.width() / 2, r.height() / 2});
        if (w <= 0) {
            continue;
        }
        bars << QRectF(r.left(), r.top(), r.width(), w) << QRectF(r.left(), r.bottom() - w, r.width(), w);
        bars << QRectF(r.left(), r.top() + w, w, r.height() - 2 * w) << QRectF(r.right() - w, r.top() + w, w, r.height() - 2 * w);
    }
    return bars;
}

void drawTexture(KWin::GLTexture *texture, const QPointF &topLeft, qreal alpha, const KWin::RenderTarget &renderTarget,
                 const KWin::RenderViewport &viewport, const KWin::Region &deviceRegion)
{
    if (!texture || alpha <= 0.0) {
        return;
    }
    const qreal scale = viewport.scale();
    KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::Modulate | KWin::ShaderTrait::TransformColorspace);
    binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(), KWin::RenderingIntent::Perceptual);
    binder.shader()->setUniform(KWin::GLShader::Vec4Uniform::ModulationConstant, QVector4D(alpha, alpha, alpha, alpha));
    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(std::round(topLeft.x() * scale), std::round(topLeft.y() * scale));
    binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
    const QSizeF size = texture->size();
    if (deviceRegion == KWin::Region::infinite()) {
        texture->render(size);
    } else {
        texture->render(deviceRegion, size, true);
    }
}

std::unique_ptr<KWin::GLTexture> uploadImage(const QImage &image)
{
    std::unique_ptr<KWin::GLTexture> texture = KWin::GLTexture::upload(image);
    if (texture) {
        texture->setFilter(GL_LINEAR);
        texture->setWrapMode(GL_CLAMP_TO_EDGE);
    }
    return texture;
}

std::optional<QJsonObject> parseObject(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

} // namespace

Organizer::Organizer(InfinitDesktopEffect *effect, StateStore *store)
    : m_effect(effect)
    , m_store(store)
{
    m_groups.fromJson(m_store->loadFile(QStringLiteral("groups.json")));
    m_areas.fromJson(m_store->loadFile(QStringLiteral("areas.json")));
    m_notes.fromJson(m_store->loadFile(QStringLiteral("notes.json")));

    // Notes that are already open belong to a companion that was running before the plugin
    // (re)started: ask it to announce them again. Never start it for that.
    QTimer::singleShot(0, this, [this]() {
        QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(CompanionService), CompanionPath, CompanionInterface,
                                                              QStringLiteral("reannounceNotes"));
        message.setAutoStartService(false);
        QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
    });
}

Organizer::~Organizer()
{
    persist();
}

// ============================================================================
// Persistence
// ============================================================================

void Organizer::markDirty()
{
    m_dirty = true;
    m_effect->persistSoon();
}

void Organizer::persist()
{
    if (m_groupsDirty) {
        m_store->saveFile(QStringLiteral("groups.json"), m_groups.toJson());
        m_groupsDirty = false;
    }
    if (m_areasDirty) {
        m_store->saveFile(QStringLiteral("areas.json"), m_areas.toJson());
        m_areasDirty = false;
    }
    if (m_notesDirty) {
        m_store->saveFile(QStringLiteral("notes.json"), m_notes.toJson());
        m_notesDirty = false;
    }
    m_dirty = false;
}

void Organizer::prunePending()
{
    const auto now = SteadyClock::now();
    m_launches.removeIf([now](const PendingLaunch &launch) {
        return launch.deadline < now;
    });
    m_expectedNotes.removeIf([now](const ExpectedNote &note) {
        return note.deadline < now;
    });
}

// ============================================================================
// Windows
// ============================================================================

bool Organizer::windowAdopted(ManagedWindow &window, bool newlyMapped)
{
    KWin::Window *client = window.window;
    if (!client) {
        return false;
    }
    if (newlyMapped && placeNewWindow(window)) {
        return true;
    }
    // Area membership of windows the canvas did not place itself.
    if (Canvas *canvas = m_effect->canvasByKey(window.canvasKey); canvas && m_effect->modeOf(window) == WindowMode::Canvas) {
        const QList<Area> areas = areasOf(*canvas);
        const int index = areaForWindow(areas, m_effect->canvasRect(window));
        if (index >= 0) {
            window.areaId = areas[index].id;
            updateAreas(*canvas);
        }
    }
    return false;
}

bool Organizer::placeNewWindow(ManagedWindow &window)
{
    KWin::Window *client = window.window;
    prunePending();
    KWin::LogicalOutput *output = client->output();
    if (!output) {
        return false;
    }

    // A note of the companion app.
    if (window.appKey == QLatin1String(CompanionAppKey) && !m_expectedNotes.isEmpty()) {
        window.noteId = m_expectedNotes.takeFirst().id;
        window.placementEligible = false;
        client->setSkipTaskbar(true);
        client->setSkipSwitcher(true);
        client->setSkipPager(true);
        const QSizeF size = toQRectF(client->frameGeometry()).size();

        if (const std::optional<NotePlacement> placement = m_notes.find(window.noteId)) {
            KWin::LogicalOutput *target = m_effect->findOutput(placement->output);
            if (!target) {
                target = output;
            }
            const QRectF viewport = rectOf(target);
            const QRectF area = m_effect->workArea(target);
            if (placement->pinned) {
                QPointF screenPos = viewport.topLeft() + placement->screenOffset;
                screenPos.setX(std::clamp(screenPos.x(), area.left(), std::max(area.left(), area.right() - size.width())));
                screenPos.setY(std::clamp(screenPos.y(), area.top(), std::max(area.top(), area.bottom() - size.height())));
                m_effect->setUserPinned(window, true, placement->keepAbove);
                m_effect->moveWindowPhysically(client, screenPos);
                return true;
            }
            if (Canvas *canvas = m_effect->currentCanvas(target)) {
                window.canvasKey = canvas->key;
                window.canvasPos = placement->canvasPos;
                m_effect->placeWindow(window, *canvas, target);
            }
            return true;
        }

        // A new note: at the pointer (or where the canvas menu was opened).
        KWin::LogicalOutput *target = output;
        QPointF screenPos = KWin::effects->cursorPos();
        if (!m_newNote.outputName.isEmpty() && m_newNote.deadline > SteadyClock::now()) {
            if (KWin::LogicalOutput *named = KWin::effects->findScreen(m_newNote.outputName)) {
                target = named;
                screenPos = m_newNote.screenPos;
            }
        }
        m_newNote = {};
        Canvas *canvas = m_effect->currentCanvas(target);
        if (!canvas) {
            return true;
        }
        const QRectF viewport = rectOf(target);
        const QRectF area = m_effect->workArea(target);
        const qreal zoom = canvas->camera.zoom;
        // The note's top left a little above and left of the pointer, fully on screen.
        QPointF topLeft = screenPos - QPointF(24, 16);
        topLeft.setX(std::clamp(topLeft.x(), area.left(), std::max(area.left(), area.right() - size.width() * zoom)));
        topLeft.setY(std::clamp(topLeft.y(), area.top(), std::max(area.top(), area.bottom() - size.height() * zoom)));
        window.canvasKey = canvas->key;
        window.canvasPos = canvas->camera.screenToCanvas(topLeft, viewport);
        m_effect->placeWindow(window, *canvas, target);
        KWin::effects->activateWindow(window.effectWindow);
        return true;
    }

    // A window started for a group.
    for (int i = 0; i < m_launches.size(); ++i) {
        const PendingLaunch launch = m_launches[i];
        if (launch.appKey != window.appKey) {
            continue;
        }
        Canvas *canvas = m_effect->canvasByKey(launch.canvasKey);
        KWin::LogicalOutput *target = canvas ? m_effect->outputOf(*canvas) : nullptr;
        if (!canvas || !target || m_effect->modeOf(window) != WindowMode::Canvas) {
            return false;
        }
        m_launches.removeAt(i);
        window.groupId = launch.groupId;
        window.groupMember = launch.member;
        if (launch.target.width() >= 50 && launch.target.height() >= 50 && client->maximizeMode() == KWin::MaximizeRestore) {
            client->resize(launch.target.size().boundedTo(m_effect->workArea(target).size() / std::max(canvas->camera.zoom, 0.1)));
        }
        if (window.canvasKey != canvas->key) {
            m_effect->transferWindow(window, *canvas, launch.target.topLeft());
        } else {
            window.canvasPos = launch.target.topLeft();
            m_effect->placeWindow(window, *canvas, target);
        }
        qCDebug(KWIN_INFINITDESKTOP) << "placed" << window.appKey << "in group" << launch.groupId << "at" << launch.target;
        return true;
    }
    return false;
}

void Organizer::windowClosed(ManagedWindow &window)
{
    if (!window.noteId.isEmpty() && window.window) {
        Canvas *canvas = m_effect->canvasByKey(window.canvasKey);
        KWin::LogicalOutput *output = canvas ? m_effect->outputOf(*canvas) : window.window->output();
        if (output) {
            const QRectF frame = toQRectF(window.effectWindow->frameGeometry());
            NotePlacement placement;
            placement.id = window.noteId;
            placement.output = InfinitDesktopEffect::identityOf(output);
            placement.desktopId = canvas ? canvas->desktopId : QString();
            placement.pinned = window.userPinned;
            placement.keepAbove = window.effectWindow->keepAbove();
            placement.screenOffset = frame.topLeft() - rectOf(output).topLeft();
            placement.canvasPos = (m_effect->isPinned(window) && canvas) ? canvas->committed.screenToCanvas(frame.topLeft(), rectOf(output))
                                                                         : window.canvasPos;
            if (m_notes.store(placement)) {
                m_notesDirty = true;
                markDirty();
            }
        }
    }
    if (!window.areaId.isEmpty()) {
        const QString canvasKey = window.canvasKey;
        window.areaId.clear();
        // Recompute once the window is out of the list of open windows.
        QTimer::singleShot(0, this, [this, canvasKey]() {
            if (Canvas *canvas = m_effect->canvasByKey(canvasKey)) {
                updateAreas(*canvas);
            }
        });
    }
}

void Organizer::windowDeleted(KWin::EffectWindow *w)
{
    m_icons.erase(w);
}

void Organizer::windowArranged(ManagedWindow &window)
{
    if (window.closed || !window.window) {
        return;
    }
    Canvas *canvas = m_effect->canvasByKey(window.canvasKey);
    if (!canvas || m_effect->modeOf(window) != WindowMode::Canvas) {
        return;
    }
    // Areas: the window belongs to the area its centre was dropped in.
    const QList<Area> areas = areasOf(*canvas);
    const int index = areaForWindow(areas, m_effect->canvasRect(window));
    const QString areaId = index >= 0 ? areas[index].id : QString();
    if (areaId != window.areaId || !areaId.isEmpty()) {
        window.areaId = areaId;
        updateAreas(*canvas);
    }

    // Groups: the place follows the windows, the arrangement as the group says.
    if (!window.groupId.isEmpty()) {
        if (const Group *stored = m_groups.find(window.groupId)) {
            Group group = *stored;
            const QList<QRectF> rects = memberRects(group);
            if (group.layoutMode == GroupLayoutMode::Automatic) {
                captureLayout(group, rects);
            } else {
                followWindows(group, rects);
            }
            if (KWin::LogicalOutput *output = m_effect->outputOf(*canvas)) {
                group.output = InfinitDesktopEffect::identityOf(output);
            }
            group.desktopId = canvas->desktopId;
            m_groups.update(group);
            m_groupsDirty = true;
            markDirty();
            Q_EMIT groupsChanged();
        } else {
            window.groupId.clear();
            window.groupMember = -1;
        }
    }
}

void Organizer::windowResized(ManagedWindow &window)
{
    if (window.areaId.isEmpty() || m_areaUpdates.contains(window.canvasKey)) {
        return;
    }
    // Applications resize themselves in bursts: one update per event loop round.
    m_areaUpdates.insert(window.canvasKey);
    QTimer::singleShot(0, this, [this]() {
        const QSet<QString> keys = std::exchange(m_areaUpdates, {});
        for (const QString &key : keys) {
            if (Canvas *canvas = m_effect->canvasByKey(key)) {
                updateAreas(*canvas);
            }
        }
    });
}

void Organizer::pinChanged(ManagedWindow &window)
{
    if (window.noteId.isEmpty() || !window.effectWindow) {
        return;
    }
    Q_EMIT notePinnedChanged(window.noteId, window.userPinned, window.effectWindow->keepAbove());
}

QList<QRectF> Organizer::memberRects(const Group &group) const
{
    QList<QRectF> rects(group.members.size());
    for (const auto &[w, managed] : m_effect->m_windows) {
        if (managed->closed || managed->groupId != group.id || managed->groupMember < 0 || managed->groupMember >= rects.size()) {
            continue;
        }
        if (m_effect->modeOf(*managed) != WindowMode::Canvas) {
            continue;
        }
        rects[managed->groupMember] = m_effect->canvasRect(*managed);
    }
    return rects;
}

// ============================================================================
// Areas
// ============================================================================

bool Organizer::areaBelongsTo(const Area &area, const Canvas &canvas) const
{
    if (area.desktopId != canvas.desktopId) {
        return false;
    }
    KWin::LogicalOutput *output = m_effect->outputOf(canvas);
    return output && outputMatchScore(area.output, InfinitDesktopEffect::identityOf(output)) > 0;
}

QList<Area> Organizer::areasOf(const Canvas &canvas) const
{
    QList<Area> result;
    for (const Area &area : m_areas.areas()) {
        if (areaBelongsTo(area, canvas)) {
            result.append(area);
        }
    }
    return result;
}

bool Organizer::hasAreas(const Canvas &canvas) const
{
    return std::any_of(m_areas.areas().cbegin(), m_areas.areas().cend(), [&](const Area &area) {
        return areaBelongsTo(area, canvas);
    });
}

void Organizer::updateAreas(const Canvas &canvas)
{
    const AreaMetrics metrics{m_effect->settings().areaPadding, QSizeF(320, 200)};
    bool changed = false;
    for (Area &area : m_areas.areas()) {
        if (!areaBelongsTo(area, canvas)) {
            continue;
        }
        QList<QRectF> members;
        for (const auto &[w, managed] : m_effect->m_windows) {
            if (!managed->closed && managed->areaId == area.id && managed->canvasKey == canvas.key && m_effect->modeOf(*managed) == WindowMode::Canvas) {
                members.append(m_effect->canvasRect(*managed));
            }
        }
        const QRectF rect = dynamicAreaRect(area, members, metrics);
        if (rect != area.rect) {
            area.rect = rect;
            changed = true;
        }
    }
    if (changed) {
        m_areasDirty = true;
        markDirty();
        m_effect->requestRepaint(canvas);
        Q_EMIT areasChanged();
    }
}

void Organizer::createAreaFromRect(const QRectF &canvasRect, Canvas &canvas)
{
    KWin::LogicalOutput *output = m_effect->outputOf(canvas);
    if (!output) {
        return;
    }
    Area area;
    area.name = m_areas.nextName(i18nc("@item name of a new area, %1 is a number", "Area %1", QStringLiteral("%1")));
    area.output = InfinitDesktopEffect::identityOf(output);
    area.desktopId = canvas.desktopId;
    area.drawnRect = canvasRect;
    area.dynamic = true;
    Area *added = m_areas.add(area);
    if (!added) {
        m_effect->flashMessage(i18n("No more areas can be created."), QStringLiteral("dialog-warning"));
        return;
    }
    const QString id = added->id;
    const QString name = added->name;
    // Windows already inside belong to it.
    for (auto &[w, managed] : m_effect->m_windows) {
        if (!managed->closed && managed->canvasKey == canvas.key && m_effect->modeOf(*managed) == WindowMode::Canvas
            && canvasRect.contains(m_effect->canvasRect(*managed).center())) {
            managed->areaId = id;
        }
    }
    m_areasDirty = true;
    markDirty();
    updateAreas(canvas);
    m_effect->requestRepaint(canvas);
    m_effect->flashMessage(i18n("Area “%1” created", name), QStringLiteral("draw-rectangle"));
    Q_EMIT areasChanged();
}

QString Organizer::areasJson() const
{
    return QString::fromUtf8(QJsonDocument(m_areas.toJson()).toJson(QJsonDocument::Compact));
}

bool Organizer::setAreasJson(const QString &json)
{
    const std::optional<QJsonObject> object = parseObject(json);
    if (!object) {
        return false;
    }
    m_areas.fromJson(*object);
    // Windows of areas that are gone belong to none.
    for (auto &[w, managed] : m_effect->m_windows) {
        if (!managed->areaId.isEmpty() && m_areas.indexOf(managed->areaId) < 0) {
            managed->areaId.clear();
        }
    }
    m_areasDirty = true;
    markDirty();
    for (const Canvas &canvas : m_effect->m_canvases) {
        updateAreas(canvas);
        m_effect->requestRepaint(canvas);
    }
    m_effect->updateActivity();
    Q_EMIT areasChanged();
    return true;
}

// ============================================================================
// Groups
// ============================================================================

QString Organizer::groupsJson() const
{
    QJsonObject root = m_groups.toJson();
    // How many windows of each group are open, for the settings app.
    QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    for (int i = 0; i < groups.size(); ++i) {
        QJsonObject group = groups[i].toObject();
        const QString id = group.value(QStringLiteral("id")).toString();
        int open = 0;
        for (const auto &[w, managed] : m_effect->m_windows) {
            if (!managed->closed && managed->groupId == id) {
                ++open;
            }
        }
        group.insert(QStringLiteral("openWindows"), open);
        groups[i] = group;
    }
    root.insert(QStringLiteral("groups"), groups);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool Organizer::setGroupsJson(const QString &json)
{
    const std::optional<QJsonObject> object = parseObject(json);
    if (!object) {
        return false;
    }
    m_groups.fromJson(*object);
    for (auto &[w, managed] : m_effect->m_windows) {
        const Group *group = managed->groupId.isEmpty() ? nullptr : m_groups.find(managed->groupId);
        if (!group || managed->groupMember >= group->members.size()) {
            managed->groupId.clear();
            managed->groupMember = -1;
        }
    }
    m_groupsDirty = true;
    markDirty();
    Q_EMIT groupsChanged();
    return true;
}

Canvas *Organizer::canvasForGroup(const Group &group) const
{
    KWin::LogicalOutput *output = group.output.name.isEmpty() && !group.output.hasEdid() ? nullptr : m_effect->findOutput(group.output);
    if (!output) {
        output = KWin::effects->activeScreen();
    }
    return output ? m_effect->currentCanvas(output) : nullptr;
}

void Organizer::fillMissingGeometry(Group &group, Canvas &canvas)
{
    QList<int> missing;
    for (int i = 0; i < group.members.size(); ++i) {
        if (group.members[i].geometry.isEmpty()) {
            missing.append(i);
        }
    }
    if (missing.isEmpty()) {
        return;
    }
    KWin::LogicalOutput *output = m_effect->outputOf(canvas);
    if (!output) {
        return;
    }
    // Sizes of open windows of these applications, else a common default.
    QList<QSizeF> sizes;
    for (int i : std::as_const(missing)) {
        QSizeF size(1000, 700);
        for (const auto &[w, managed] : m_effect->m_windows) {
            if (!managed->closed && managed->appKey == group.members[i].appKey) {
                size = toQRectF(w->frameGeometry()).size();
                break;
            }
        }
        sizes.append(size);
    }
    const QList<QRectF> arranged = autoArrange(sizes, 3, 24);
    const QRectF existing = group.layoutBounds();
    const QPointF offset = existing.isNull() ? QPointF() : QPointF(existing.right() + 24, existing.top());
    for (int k = 0; k < missing.size(); ++k) {
        group.members[missing[k]].geometry = arranged[k].translated(offset);
    }
    if (existing.isNull()) {
        // A new group without a place yet: in the middle of the view.
        const QRectF visible = m_effect->targetCamera(canvas).visibleCanvasRect(rectOf(output));
        group.origin = visible.center() - group.layoutBounds().center();
        group.output = InfinitDesktopEffect::identityOf(output);
        group.desktopId = canvas.desktopId;
    }
    m_groups.update(group);
    m_groupsDirty = true;
    markDirty();
}

void Organizer::openGroup(int index)
{
    if (index < 0 || index >= m_groups.groups().size()) {
        m_effect->flashMessage(i18n("There is no group %1. Create one with Meta+Alt+G or in the settings app.", index + 1),
                              QStringLiteral("dialog-information"));
        return;
    }
    openGroup(m_groups.groups()[index].id);
}

void Organizer::openGroup(const QString &id)
{
    const Group *stored = m_groups.find(id);
    if (!stored || stored->members.isEmpty()) {
        return;
    }
    Group group = *stored;
    Canvas *canvas = canvasForGroup(group);
    KWin::LogicalOutput *output = canvas ? m_effect->outputOf(*canvas) : nullptr;
    if (!canvas || !output || m_effect->isLocked(output)) {
        return;
    }
    fillMissingGeometry(group, *canvas);

    QList<GroupCandidate> candidates;
    for (const auto &[w, managed] : m_effect->m_windows) {
        if (managed->closed || !managed->window || !managed->noteId.isEmpty()) {
            continue;
        }
        const WindowMode mode = m_effect->modeOf(*managed);
        if (mode == WindowMode::Hidden || mode == WindowMode::Moving) {
            continue;
        }
        GroupCandidate candidate;
        candidate.handle = quint64(reinterpret_cast<quintptr>(w));
        candidate.appKey = managed->appKey;
        candidate.canvasRect = m_effect->canvasRect(*managed);
        candidate.groupId = managed->groupId;
        candidate.movable = mode == WindowMode::Canvas || mode == WindowMode::Minimized;
        candidates.append(candidate);
    }
    const GroupPlan plan = planGroup(group, candidates);

    ManagedWindow *first = nullptr;
    for (const GroupPlan::Move &move : plan.moves) {
        auto it = m_effect->m_windows.find(reinterpret_cast<KWin::EffectWindow *>(quintptr(move.handle)));
        if (it == m_effect->m_windows.end() || !it->second->window || it->second->closed) {
            continue;
        }
        ManagedWindow &window = *it->second;
        KWin::Window *client = window.window;
        if (window.effectWindow->isMinimized()) {
            window.effectWindow->unminimize();
        }
        const QSizeF current = toQRectF(client->frameGeometry()).size();
        if (client->maximizeMode() == KWin::MaximizeRestore && move.target.width() >= 50 && move.target.height() >= 50
            && (std::abs(current.width() - move.target.width()) > 2 || std::abs(current.height() - move.target.height()) > 2)) {
            client->resize(move.target.size());
        }
        window.groupId = group.id;
        window.groupMember = move.member;
        if (window.canvasKey != canvas->key) {
            m_effect->transferWindow(window, *canvas, move.target.topLeft());
        } else {
            window.canvasPos = move.target.topLeft();
            // Visible now: moved there; else parked and drawn by the plugin until the camera
            // arrives and commits.
            m_effect->placeWindow(window, *canvas, output);
        }
        if (!first || move.member == 0) {
            first = &window;
        }
    }

    int started = 0;
    if (m_effect->settings().groupStartsApplications) {
        prunePending();
        for (int member : plan.launches) {
            const GroupMember &m = group.members[member];
            if (m.launch.isEmpty() || !launch(m.launch)) {
                continue;
            }
            m_launches.append(PendingLaunch{m.appKey, group.id, member, canvas->key, plan.targetOf(member, group), SteadyClock::now() + LaunchTimeout});
            ++started;
        }
    }

    m_lastGroup = group.id;
    revealGroup(group, *canvas);
    if (first) {
        KWin::effects->activateWindow(first->effectWindow);
    }
    m_effect->updateActivity();
    m_effect->requestRepaint(*canvas);
    if (started > 0) {
        m_effect->flashMessage(i18np("Group “%2”: starting one application", "Group “%2”: starting %1 applications", started, group.name),
                              QStringLiteral("system-run"));
    } else {
        m_effect->flashMessage(i18n("Group “%1”", group.name), QStringLiteral("window-duplicate"));
    }
    qCDebug(KWIN_INFINITDESKTOP) << "opened group" << group.name << "moved" << plan.moves.size() << "started" << started;
}

void Organizer::revealGroup(const Group &group, Canvas &canvas)
{
    KWin::LogicalOutput *output = m_effect->outputOf(canvas);
    if (!output) {
        return;
    }
    const QRectF bounds = group.canvasBounds();
    if (bounds.isEmpty()) {
        return;
    }
    const QRectF viewport = rectOf(output);
    const QRectF area = m_effect->workArea(output);
    const Camera base = m_effect->targetCamera(canvas);
    const EffectSettings &settings = m_effect->settings();
    const bool fits = bounds.width() <= area.width() - 2 * GroupMargin && bounds.height() <= area.height() - 2 * GroupMargin;
    Camera target;
    if (fits || !settings.groupZoomToFit) {
        target = revealCamera(base, bounds, viewport, area, RevealMode::Center, GroupMargin, 1.0);
    } else {
        target = fitCamera(bounds, viewport, area, GroupMargin, settings.minZoom, 1.0);
    }
    m_effect->animateTo(canvas, target);
}

bool Organizer::launch(const QString &desktopFile)
{
    QString name = desktopFile;
    if (name.endsWith(QLatin1String(".desktop"))) {
        name.chop(8);
    }
    // kstart (Plasma) starts it like the launcher would; gtk-launch as a fallback.
    if (!QStandardPaths::findExecutable(QStringLiteral("kstart")).isEmpty()
        && QProcess::startDetached(QStringLiteral("kstart"), {QStringLiteral("--application"), name})) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("gtk-launch")).isEmpty()) {
        return QProcess::startDetached(QStringLiteral("gtk-launch"), {name});
    }
    qCWarning(KWIN_INFINITDESKTOP) << "cannot start" << name << "(neither kstart nor gtk-launch found)";
    return false;
}

void Organizer::createGroupFromRect(const QRectF &canvasRect, Canvas &canvas)
{
    KWin::LogicalOutput *output = m_effect->outputOf(canvas);
    if (!output) {
        return;
    }
    QList<ManagedWindow *> windows;
    QList<QRectF> rects;
    for (auto &[w, managed] : m_effect->m_windows) {
        if (!managed->closed && managed->window && managed->noteId.isEmpty() && managed->canvasKey == canvas.key
            && m_effect->modeOf(*managed) == WindowMode::Canvas && !managed->appKey.isEmpty()) {
            windows.append(managed.get());
            rects.append(m_effect->canvasRect(*managed));
        }
    }
    const QList<int> inside = windowsInside(canvasRect, rects);
    if (inside.isEmpty()) {
        m_effect->flashMessage(i18n("No windows in the selection"), QStringLiteral("dialog-information"));
        return;
    }
    Group group;
    group.name = m_groups.nextName(i18nc("@item name of a new group, %1 is a number", "Group %1", QStringLiteral("%1")));
    group.output = InfinitDesktopEffect::identityOf(output);
    group.desktopId = canvas.desktopId;
    group.layoutMode = m_effect->settings().groupLayoutAutomatic ? GroupLayoutMode::Automatic : GroupLayoutMode::Manual;
    QList<QRectF> memberRects;
    for (int index : inside) {
        if (group.members.size() >= GroupStore::MaxMembers) {
            break;
        }
        const ManagedWindow *window = windows[index];
        group.members.append(GroupMember{window->appKey, window->window->desktopFileName(), QRectF()});
        memberRects.append(rects[index]);
    }
    captureLayout(group, memberRects);
    Group *added = m_groups.add(group);
    if (!added) {
        m_effect->flashMessage(i18n("No more groups can be created."), QStringLiteral("dialog-warning"));
        return;
    }
    for (int k = 0; k < inside.size() && k < added->members.size(); ++k) {
        windows[inside[k]]->groupId = added->id;
        windows[inside[k]]->groupMember = k;
    }
    const int slot = m_groups.indexOf(added->id) + 1;
    m_lastGroup = added->id;
    m_groupsDirty = true;
    markDirty();
    if (slot <= GroupShortcutCount) {
        m_effect->flashMessage(i18n("Group “%1” created: open it with Meta+Alt+F%2", added->name, slot), QStringLiteral("window-duplicate"));
    } else {
        m_effect->flashMessage(i18n("Group “%1” created", added->name), QStringLiteral("window-duplicate"));
    }
    Q_EMIT groupsChanged();
}

void Organizer::saveGroupLayout(const QString &id)
{
    QString groupId = id;
    if (groupId.isEmpty()) {
        if (ManagedWindow *active = m_effect->record(KWin::effects->activeWindow()); active && !active->groupId.isEmpty()) {
            groupId = active->groupId;
        } else {
            groupId = m_lastGroup;
        }
    }
    const Group *stored = m_groups.find(groupId);
    if (!stored) {
        m_effect->flashMessage(i18n("The active window is in no group"), QStringLiteral("dialog-information"));
        return;
    }
    Group group = *stored;
    captureLayout(group, memberRects(group));
    m_groups.update(group);
    m_groupsDirty = true;
    markDirty();
    m_effect->flashMessage(i18n("Arrangement of group “%1” saved", group.name), QStringLiteral("document-save"));
    Q_EMIT groupsChanged();
}

// ============================================================================
// Notes
// ============================================================================

void Organizer::callCompanion(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(CompanionService), CompanionPath, CompanionInterface, method);
    message.setArguments(arguments);
    // Fire and forget: the compositor never waits for another process. D-Bus starts the
    // companion if it is not running.
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}

void Organizer::newNote(KWin::LogicalOutput *output, const QPointF &screenPos)
{
    if (!m_effect->settings().notesEnabled) {
        m_effect->flashMessage(i18n("Notes are switched off in the settings"), QStringLiteral("dialog-information"));
        return;
    }
    m_newNote.outputName = output ? output->name() : QString();
    m_newNote.screenPos = screenPos;
    m_newNote.deadline = SteadyClock::now() + NewNoteTimeout;
    callCompanion(QStringLiteral("newNote"));
}

void Organizer::toggleNotes()
{
    callCompanion(QStringLiteral("toggleNotes"));
}

void Organizer::openSettings()
{
    QProcess::startDetached(QStringLiteral("kde-infinit-desktop-settings"), {QStringLiteral("--page"), QStringLiteral("organize")});
}

void Organizer::expectNote(const QString &id)
{
    if (id.isEmpty() || id.size() > 64) {
        return;
    }
    prunePending();
    if (m_expectedNotes.size() < 64) {
        m_expectedNotes.append(ExpectedNote{id, SteadyClock::now() + ExpectedNoteTimeout});
    }
}

QString Organizer::noteState(const QString &id) const
{
    for (const auto &[w, managed] : m_effect->m_windows) {
        if (!managed->closed && managed->noteId == id) {
            return QString::fromUtf8(QJsonDocument(QJsonObject{
                                                       {QStringLiteral("id"), id},
                                                       {QStringLiteral("open"), true},
                                                       {QStringLiteral("pinned"), managed->userPinned},
                                                       {QStringLiteral("keepAbove"), w->keepAbove()},
                                                   })
                                         .toJson(QJsonDocument::Compact));
        }
    }
    const std::optional<NotePlacement> placement = m_notes.find(id);
    return QString::fromUtf8(QJsonDocument(QJsonObject{
                                               {QStringLiteral("id"), id},
                                               {QStringLiteral("open"), false},
                                               {QStringLiteral("pinned"), placement && placement->pinned},
                                               {QStringLiteral("keepAbove"), placement && placement->keepAbove},
                                           })
                                 .toJson(QJsonDocument::Compact));
}

void Organizer::setNotePinned(const QString &id, bool pinned, bool keepAbove)
{
    for (auto &[w, managed] : m_effect->m_windows) {
        if (!managed->closed && managed->noteId == id) {
            m_effect->setUserPinned(*managed, pinned, keepAbove);
            return;
        }
    }
    if (std::optional<NotePlacement> placement = m_notes.find(id)) {
        placement->pinned = pinned;
        placement->keepAbove = pinned && keepAbove;
        m_notes.store(*placement);
        m_notesDirty = true;
        markDirty();
        Q_EMIT notePinnedChanged(id, placement->pinned, placement->keepAbove);
    }
}

void Organizer::forgetNote(const QString &id)
{
    if (m_notes.remove(id)) {
        m_notesDirty = true;
        markDirty();
    }
}

void Organizer::retainNotes(const QStringList &existing)
{
    if (m_notes.retain(existing) > 0) {
        m_notesDirty = true;
        markDirty();
    }
}

// ============================================================================
// Overlays
// ============================================================================

KWin::LogicalOutput *Organizer::overlayOutput() const
{
    return m_overlayOutput.isEmpty() ? nullptr : KWin::effects->findScreen(m_overlayOutput);
}

void Organizer::repaintOverlay()
{
    if (KWin::LogicalOutput *output = overlayOutput()) {
        KWin::effects->addRepaint(output->geometryF());
    }
}

void Organizer::closeOverlay()
{
    if (m_overlay == Overlay::None) {
        return;
    }
    repaintOverlay();
    if (m_overlay == Overlay::Selection) {
        KWin::effects->hideOnScreenMessage();
    }
    m_overlay = Overlay::None;
    m_overlayOutput.clear();
    m_menuTexture.reset();
    m_menuTextureHover = -2;
    m_selecting = false;
    m_mapDragging = false;
    // Labels and icons are cheap to make again; nothing stays in GPU memory at rest.
    m_labels.clear();
    m_icons.clear();
    m_effect->updateActivity();
}

void Organizer::beginOverlay(Overlay overlay, KWin::LogicalOutput *output)
{
    closeOverlay();
    // A window used while zoomed out gives the pointer back first.
    if (m_effect->m_inputFilter) {
        m_effect->m_inputFilter->endZoomedInput();
    }
    m_overlay = overlay;
    m_overlayOutput = output->name();
    m_effect->updateActivity();
    repaintOverlay();
}

bool Organizer::needsPainting(const Canvas &canvas) const
{
    if (m_overlay != Overlay::None) {
        return true;
    }
    return m_effect->settings().showAreas == EffectSettings::Grid::Always && hasAreas(canvas);
}

// ---- Menu -------------------------------------------------------------------

QList<Organizer::MenuItem> Organizer::menuItems() const
{
    QList<MenuItem> items;
    const EffectSettings &settings = m_effect->settings();
    if (settings.notesEnabled) {
        items.append(MenuItem{QStringLiteral("new-note"), i18nc("@action canvas menu", "New Note Here")});
        items.append(MenuItem{QStringLiteral("toggle-notes"), i18nc("@action canvas menu", "Show / Hide Notes")});
        items.append(MenuItem{QString(), QString(), true});
    }
    items.append(MenuItem{QStringLiteral("new-group"), i18nc("@action canvas menu", "New Group from Selection…")});
    items.append(MenuItem{QStringLiteral("new-area"), i18nc("@action canvas menu", "New Area from Selection…")});
    if (KWin::LogicalOutput *output = overlayOutput()) {
        if (Canvas *canvas = m_effect->currentCanvas(output)) {
            if (areaAt(areasOf(*canvas), m_menuCanvasPos) >= 0) {
                items.append(MenuItem{QStringLiteral("remove-area"), i18nc("@action canvas menu", "Remove This Area")});
            }
        }
    }
    items.append(MenuItem{QStringLiteral("map"), i18nc("@action canvas menu", "Show Map")});
    items.append(MenuItem{QString(), QString(), true});
    items.append(MenuItem{QStringLiteral("show-all"), i18nc("@action canvas menu", "Show All Windows")});
    if (KWin::LogicalOutput *output = overlayOutput()) {
        if (Canvas *canvas = m_effect->currentCanvas(output); canvas && !m_effect->targetCamera(*canvas).isUnitZoom()) {
            items.append(MenuItem{QStringLiteral("zoom-reset"), i18nc("@action canvas menu", "Zoom to 100 %")});
        }
    }
    items.append(MenuItem{QStringLiteral("settings"), i18nc("@action canvas menu", "Infinit Desktop Settings…")});
    return items;
}

QRectF Organizer::menuRect() const
{
    qreal height = 2 * MenuPadding;
    for (const MenuItem &item : menuItems()) {
        height += item.separator ? MenuSeparatorHeight : MenuItemHeight;
    }
    QRectF rect(m_menuPos + QPointF(2, 2), QSizeF(MenuWidth, height));
    if (KWin::LogicalOutput *output = overlayOutput()) {
        // Opens towards the free side, never off screen.
        const QRectF area = m_effect->workArea(output);
        if (rect.right() > area.right()) {
            rect.moveRight(m_menuPos.x() - 2);
        }
        if (rect.bottom() > area.bottom()) {
            rect.moveBottom(m_menuPos.y() - 2);
        }
        rect.moveLeft(std::max(rect.left(), area.left()));
        rect.moveTop(std::max(rect.top(), area.top()));
    }
    return rect;
}

int Organizer::menuItemAt(const QPointF &pos) const
{
    const QRectF rect = menuRect();
    if (!rect.contains(pos)) {
        return -1;
    }
    qreal y = rect.top() + MenuPadding;
    const QList<MenuItem> items = menuItems();
    for (int i = 0; i < items.size(); ++i) {
        const qreal height = items[i].separator ? MenuSeparatorHeight : MenuItemHeight;
        if (pos.y() >= y && pos.y() < y + height) {
            return items[i].separator ? -1 : i;
        }
        y += height;
    }
    return -1;
}

QImage Organizer::renderMenu(qreal scale) const
{
    const QRectF rect = menuRect();
    const QSize pixels(std::ceil(rect.width() * scale), std::ceil(rect.height() * scale));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(scale);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette palette = QGuiApplication::palette();
    const QColor base = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(0.5, 0.5, rect.width() - 1, rect.height() - 1), 8, 8);
    painter.fillPath(frame, base);
    QColor border = text;
    border.setAlphaF(0.18);
    painter.setPen(QPen(border, 1));
    painter.drawPath(frame);

    QFont font = QGuiApplication::font();
    painter.setFont(font);
    const QList<MenuItem> items = menuItems();
    qreal y = MenuPadding;
    for (int i = 0; i < items.size(); ++i) {
        const MenuItem &item = items[i];
        if (item.separator) {
            painter.setPen(QPen(border, 1));
            painter.drawLine(QPointF(MenuPadding * 2, y + MenuSeparatorHeight / 2), QPointF(rect.width() - MenuPadding * 2, y + MenuSeparatorHeight / 2));
            y += MenuSeparatorHeight;
            continue;
        }
        const QRectF itemRect(MenuPadding, y, rect.width() - 2 * MenuPadding, MenuItemHeight);
        if (i == m_menuHover) {
            QPainterPath highlight;
            highlight.addRoundedRect(itemRect, 5, 5);
            QColor accent = palette.color(QPalette::Highlight);
            painter.fillPath(highlight, accent);
            painter.setPen(palette.color(QPalette::HighlightedText));
        } else {
            painter.setPen(text);
        }
        painter.drawText(itemRect.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, item.text);
        y += MenuItemHeight;
    }
    painter.end();
    return image;
}

void Organizer::showCanvasMenu(KWin::LogicalOutput *output, const QPointF &screenPos)
{
    if (!output) {
        return;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return;
    }
    m_menuPos = screenPos;
    m_menuCanvasPos = canvas->camera.screenToCanvas(screenPos, rectOf(output));
    m_menuHover = -1;
    beginOverlay(Overlay::Menu, output);
}

void Organizer::triggerMenuItem(const QString &id)
{
    KWin::LogicalOutput *output = overlayOutput();
    const QPointF pos = m_menuPos;
    const QPointF canvasPos = m_menuCanvasPos;
    closeOverlay();
    if (!output) {
        return;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (id == QLatin1String("new-note")) {
        newNote(output, pos);
    } else if (id == QLatin1String("toggle-notes")) {
        toggleNotes();
    } else if (id == QLatin1String("new-group")) {
        startSelection(true, output);
    } else if (id == QLatin1String("new-area")) {
        startSelection(false, output);
    } else if (id == QLatin1String("remove-area") && canvas) {
        const QList<Area> areas = areasOf(*canvas);
        const int index = areaAt(areas, canvasPos);
        if (index >= 0) {
            const QString areaId = areas[index].id;
            const QString name = areas[index].name;
            m_areas.remove(areaId);
            for (auto &[w, managed] : m_effect->m_windows) {
                if (managed->areaId == areaId) {
                    managed->areaId.clear();
                }
            }
            m_areasDirty = true;
            markDirty();
            m_effect->requestRepaint(*canvas);
            m_effect->updateActivity();
            m_effect->flashMessage(i18n("Area “%1” removed", name), QStringLiteral("edit-delete"));
            Q_EMIT areasChanged();
        }
    } else if (id == QLatin1String("map")) {
        toggleMap(output);
    } else if (id == QLatin1String("show-all") && canvas) {
        m_effect->zoomToFit(*canvas, false);
    } else if (id == QLatin1String("zoom-reset") && canvas) {
        m_effect->setZoom(*canvas, pos, 1.0, true);
    } else if (id == QLatin1String("settings")) {
        openSettings();
    }
}

// ---- Selection --------------------------------------------------------------

void Organizer::startSelection(bool group, KWin::LogicalOutput *output)
{
    if (!output) {
        output = KWin::effects->activeScreen();
    }
    if (!output || m_effect->isLocked(output)) {
        return;
    }
    m_selectGroup = group;
    m_selecting = false;
    beginOverlay(Overlay::Selection, output);
    // Stays until the rectangle is drawn or the selection is cancelled.
    m_effect->showMessage(group ? i18n("Draw a rectangle around the windows of the new group (Esc cancels)") : i18n("Draw the new area (Esc cancels)"),
                          QStringLiteral("draw-rectangle"));
}

// ---- Map --------------------------------------------------------------------

void Organizer::toggleMap(KWin::LogicalOutput *output)
{
    if (m_overlay == Overlay::Map) {
        closeOverlay();
        return;
    }
    if (!output) {
        output = KWin::effects->activeScreen();
    }
    if (!output || m_effect->isLocked(output) || !m_effect->currentCanvas(output)) {
        return;
    }
    m_mapDragging = false;
    beginOverlay(Overlay::Map, output);
    updateMapView();
}

void Organizer::updateMapView()
{
    KWin::LogicalOutput *output = overlayOutput();
    Canvas *canvas = output ? m_effect->currentCanvas(output) : nullptr;
    if (!canvas) {
        m_map = MapView{};
        return;
    }
    const QRectF viewport = rectOf(output);
    const QRectF area = m_effect->workArea(output);
    const qreal share = std::clamp(m_effect->settings().mapSize, 30, 95) / 100.0;
    const QSizeF panelSize(area.width() * share, area.height() * share);
    m_mapPanel = QRectF(area.center() - QPointF(panelSize.width() / 2, panelSize.height() / 2), panelSize);

    QList<QRectF> content;
    for (const auto &[w, managed] : m_effect->m_windows) {
        if (!managed->closed && managed->canvasKey == canvas->key && m_effect->modeOf(*managed) == WindowMode::Canvas) {
            content.append(m_effect->canvasRect(*managed));
        }
    }
    for (const Area &a : areasOf(*canvas)) {
        content.append(a.rect);
    }
    KWin::LogicalOutput *groupOutput = output;
    for (const Group &group : m_groups.groups()) {
        if (m_effect->findOutput(group.output) == groupOutput && group.desktopId == canvas->desktopId) {
            content.append(group.canvasBounds());
        }
    }
    const QRectF visible = canvas->camera.visibleCanvasRect(viewport);
    // Do not jump around while the user drags inside the map: keep the view, just follow
    // if the camera leaves it.
    if (m_mapDragging && m_map.isValid() && m_map.canvas.contains(visible)) {
        return;
    }
    m_map = mapView(content, visible, m_mapPanel, 16, 0.5, viewport.size() * 2);
}

QPointF Organizer::mapCameraCenter(const QPointF &screenPos) const
{
    return m_map.toCanvas(screenPos);
}

// ---- Input ------------------------------------------------------------------

bool Organizer::pointerMotion(const QPointF &pos)
{
    switch (m_overlay) {
    case Overlay::None:
        return false;
    case Overlay::Menu: {
        const int hover = menuItemAt(pos);
        if (hover != m_menuHover) {
            m_menuHover = hover;
            repaintOverlay();
        }
        return true;
    }
    case Overlay::Selection:
        if (m_selecting) {
            m_selectEnd = pos;
            repaintOverlay();
        }
        return true;
    case Overlay::Map: {
        if (!m_mapDragging) {
            return true;
        }
        KWin::LogicalOutput *output = overlayOutput();
        Canvas *canvas = output ? m_effect->currentCanvas(output) : nullptr;
        if (!canvas) {
            return true;
        }
        if ((pos - m_mapPressPos).manhattanLength() > 3) {
            m_mapMoved = true;
        }
        if (m_mapMoved) {
            const QPointF center = mapDragCenter(m_map, m_mapStartCenter, pos - m_mapPressPos);
            m_effect->stopMotion(*canvas);
            m_effect->setCamera(*canvas, Camera{center, canvas->camera.zoom});
            repaintOverlay();
        }
        return true;
    }
    }
    return false;
}

bool Organizer::pointerButton(KWin::PointerButtonEvent *event)
{
    if (m_overlay == Overlay::None) {
        return false;
    }
    const bool pressed = event->state == KWin::PointerButtonState::Pressed;
    const QPointF pos = event->position;
    KWin::LogicalOutput *output = overlayOutput();
    if (!output) {
        closeOverlay();
        return true;
    }

    switch (m_overlay) {
    case Overlay::None:
        return false;
    case Overlay::Menu:
        if (pressed && !menuRect().contains(pos)) {
            closeOverlay();
        } else if (!pressed && event->button == Qt::LeftButton) {
            const int index = menuItemAt(pos);
            const QList<MenuItem> items = menuItems();
            if (index >= 0 && index < items.size()) {
                triggerMenuItem(items[index].id);
            }
        }
        return true;
    case Overlay::Selection: {
        if (event->button == Qt::RightButton && pressed) {
            closeOverlay();
            return true;
        }
        if (event->button != Qt::LeftButton) {
            return true;
        }
        if (pressed) {
            m_selecting = true;
            m_selectStart = pos;
            m_selectEnd = pos;
            repaintOverlay();
            return true;
        }
        if (!m_selecting) {
            return true;
        }
        const QRectF screenRect = QRectF(m_selectStart, pos).normalized();
        const bool group = m_selectGroup;
        Canvas *canvas = m_effect->currentCanvas(output);
        closeOverlay();
        if (!canvas || screenRect.width() < MinimumSelection || screenRect.height() < MinimumSelection) {
            return true;
        }
        const QRectF viewport = rectOf(output);
        const QRectF canvasRect(canvas->camera.screenToCanvas(screenRect.topLeft(), viewport), canvas->camera.screenToCanvas(screenRect.bottomRight(), viewport));
        if (group) {
            createGroupFromRect(canvasRect, *canvas);
        } else {
            createAreaFromRect(canvasRect, *canvas);
        }
        return true;
    }
    case Overlay::Map: {
        Canvas *canvas = m_effect->currentCanvas(output);
        if (!canvas) {
            closeOverlay();
            return true;
        }
        if (pressed && (event->button == Qt::RightButton || !m_mapPanel.contains(pos))) {
            closeOverlay();
            return true;
        }
        if (event->button != Qt::LeftButton) {
            return true;
        }
        if (pressed) {
            m_mapDragging = true;
            m_mapMoved = false;
            m_mapPressPos = pos;
            const QRectF visible = m_map.toScreen(canvas->camera.visibleCanvasRect(rectOf(output)));
            // Grabbing the visible rect drags it; anywhere else first brings it there.
            m_mapStartCenter = visible.contains(pos) ? canvas->camera.center : mapCameraCenter(pos);
            return true;
        }
        const bool moved = m_mapMoved;
        m_mapDragging = false;
        m_mapMoved = false;
        if (moved) {
            updateMapView();
            repaintOverlay();
            return true;
        }
        // A click: a window flies there and ends the map, empty space moves the view.
        const QPointF canvasPos = mapCameraCenter(pos);
        for (const auto &[w, managed] : m_effect->m_windows) {
            if (!managed->closed && managed->canvasKey == canvas->key && m_effect->modeOf(*managed) == WindowMode::Canvas
                && m_effect->canvasRect(*managed).contains(canvasPos)) {
                ManagedWindow &target = *managed;
                closeOverlay();
                m_effect->activateAndReveal(target, false);
                return true;
            }
        }
        m_effect->animateTo(*canvas, Camera{canvasPos, m_effect->targetCamera(*canvas).zoom});
        repaintOverlay();
        return true;
    }
    }
    return true;
}

bool Organizer::pointerAxis(KWin::PointerAxisEvent *event)
{
    if (m_overlay == Overlay::None) {
        return false;
    }
    if (m_overlay == Overlay::Map && event->orientation == Qt::Vertical && event->delta != 0) {
        if (KWin::LogicalOutput *output = overlayOutput()) {
            if (Canvas *canvas = m_effect->currentCanvas(output)) {
                const qreal factor = event->delta > 0 ? 1.0 / MapWheelStep : MapWheelStep;
                m_effect->zoomBy(*canvas, rectOf(output).center(), factor, true);
                repaintOverlay();
            }
        }
    }
    return true;
}

bool Organizer::keyboardKey(KWin::KeyboardKeyEvent *event)
{
    if (m_overlay == Overlay::None || event->state == KWin::KeyboardKeyState::Released) {
        return false;
    }
    switch (event->key) {
    case Qt::Key_Escape:
        closeOverlay();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (m_overlay == Overlay::Menu) {
            const QList<MenuItem> items = menuItems();
            const int step = event->key == Qt::Key_Down ? 1 : -1;
            int index = m_menuHover;
            for (int n = 0; n < items.size(); ++n) {
                index = (index + step + items.size()) % items.size();
                if (!items[index].separator) {
                    break;
                }
            }
            m_menuHover = index;
            repaintOverlay();
            return true;
        }
        return false;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_overlay == Overlay::Menu) {
            const QList<MenuItem> items = menuItems();
            if (m_menuHover >= 0 && m_menuHover < items.size() && !items[m_menuHover].separator) {
                triggerMenuItem(items[m_menuHover].id);
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

// ============================================================================
// Painting
// ============================================================================

KWin::GLTexture *Organizer::labelTexture(const QString &text, int pixelHeight, const QColor &color)
{
    const QString key = QStringLiteral("%1|%2|%3").arg(pixelHeight).arg(color.rgba()).arg(text);
    auto it = m_labels.find(key);
    if (it != m_labels.end() && it->second.texture) {
        return it->second.texture.get();
    }
    if (m_labels.size() >= std::size_t(MaxLabels)) {
        m_labels.clear();
    }
    QFont font = QGuiApplication::font();
    font.setPixelSize(std::max(8, pixelHeight));
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF metrics(font);
    const QString elided = metrics.elidedText(text, Qt::ElideRight, pixelHeight * 24);
    const QSize size(std::ceil(metrics.horizontalAdvance(elided)) + 2, std::ceil(metrics.height()) + 2);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRectF(QPointF(1, 1), QSizeF(size) - QSizeF(2, 2)), Qt::AlignLeft | Qt::AlignVCenter, elided);
    painter.end();
    CachedTexture &cached = m_labels[key];
    cached.texture = uploadImage(image);
    cached.size = size;
    return cached.texture.get();
}

KWin::GLTexture *Organizer::iconTexture(KWin::EffectWindow *window, int pixels)
{
    CachedTexture &cached = m_icons[window];
    if (!cached.texture || cached.size.width() != pixels) {
        QImage image(QSize(pixels, pixels), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        window->icon().paint(&painter, QRect(0, 0, pixels, pixels));
        painter.end();
        cached.texture = uploadImage(image);
        cached.size = QSize(pixels, pixels);
    }
    return cached.texture.get();
}

void Organizer::paintAreas(const Canvas &canvas, qreal alpha, const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                           KWin::LogicalOutput *output, const KWin::Region &deviceRegion)
{
    if (alpha <= 0.0 || !output || !KWin::effects->openglContext()) {
        return;
    }
    const QList<Area> areas = areasOf(canvas);
    if (areas.isEmpty()) {
        return;
    }
    const QRectF screen = rectOf(output);
    const qreal scale = viewport.scale();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (const Area &area : areas) {
        const QRectF rect = canvas.camera.canvasToScreen(area.rect, screen);
        if (!rect.intersects(screen)) {
            continue;
        }
        const QColor color = AreaColors[std::clamp(area.color, 0, AreaStore::Colors - 1)];
        fillRects({rect.intersected(screen.adjusted(-4, -4, 4, 4))}, color, 0.10 * alpha, renderTarget, viewport, deviceRegion);
        fillRects(outlines({rect}, 2.0), color, 0.75 * alpha, renderTarget, viewport, deviceRegion);
        if (!area.name.isEmpty() && rect.width() > 60) {
            const int pixelHeight = std::lround(std::clamp(14.0 * std::sqrt(canvas.camera.zoom), 10.0, 18.0) * scale);
            if (KWin::GLTexture *label = labelTexture(area.name, pixelHeight, color.lighter(130))) {
                drawTexture(label, rect.topLeft() + QPointF(10, 8), alpha, renderTarget, viewport, deviceRegion);
            }
        }
    }
    glDisable(GL_BLEND);
}

void Organizer::paintOverlay(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::LogicalOutput *output,
                             const KWin::Region &deviceRegion)
{
    if (m_overlay == Overlay::None || !output || output != overlayOutput() || !KWin::effects->openglContext()) {
        return;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return;
    }
    const qreal scale = viewport.scale();
    const QRectF screen = rectOf(output);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    switch (m_overlay) {
    case Overlay::None:
        break;
    case Overlay::Menu:
        if (!m_menuTexture || m_menuTextureHover != m_menuHover || m_menuTextureScale != scale) {
            m_menuTexture = uploadImage(renderMenu(scale));
            m_menuTextureHover = m_menuHover;
            m_menuTextureScale = scale;
        }
        drawTexture(m_menuTexture.get(), menuRect().topLeft(), 1.0, renderTarget, viewport, deviceRegion);
        break;
    case Overlay::Selection: {
        // A light veil tells that the screen waits for a rectangle.
        fillRects({screen}, QColor(0, 0, 0), 0.12, renderTarget, viewport, deviceRegion);
        if (m_selecting) {
            const QRectF rect = QRectF(m_selectStart, m_selectEnd).normalized();
            const QColor color = m_selectGroup ? AccentColor : AreaColors[m_areas.areas().size() % AreaStore::Colors];
            fillRects({rect}, color, 0.18, renderTarget, viewport, deviceRegion);
            fillRects(outlines({rect}, 2.0), color, 0.9, renderTarget, viewport, deviceRegion);
        }
        break;
    }
    case Overlay::Map: {
        updateMapView();
        if (!m_map.isValid()) {
            break;
        }
        const QPalette palette = QGuiApplication::palette();
        fillRects({screen}, QColor(0, 0, 0), 0.35, renderTarget, viewport, deviceRegion);
        fillRects({m_mapPanel}, palette.color(QPalette::Window), 0.94, renderTarget, viewport, deviceRegion);
        fillRects(outlines({m_mapPanel}, 1.0), palette.color(QPalette::WindowText), 0.2, renderTarget, viewport, deviceRegion);

        // Areas and groups first, windows on top, the visible rect over all.
        for (const Area &area : areasOf(*canvas)) {
            const QRectF rect = m_map.toScreen(area.rect).intersected(m_mapPanel);
            const QColor color = AreaColors[std::clamp(area.color, 0, AreaStore::Colors - 1)];
            fillRects({rect}, color, 0.18, renderTarget, viewport, deviceRegion);
            fillRects(outlines({rect}, 1.5), color, 0.8, renderTarget, viewport, deviceRegion);
            if (rect.width() > 50) {
                drawTexture(labelTexture(area.name, std::lround(12 * scale), color.darker(110)), rect.topLeft() + QPointF(6, 4), 1.0, renderTarget,
                            viewport, deviceRegion);
            }
        }
        for (const Group &group : m_groups.groups()) {
            if (m_effect->findOutput(group.output) != output || group.desktopId != canvas->desktopId) {
                continue;
            }
            const QRectF rect = m_map.toScreen(group.canvasBounds().adjusted(-12, -12, 12, 12)).intersected(m_mapPanel);
            if (rect.isEmpty()) {
                continue;
            }
            fillRects(outlines({rect}, 1.5), AccentColor, 0.9, renderTarget, viewport, deviceRegion);
            drawTexture(labelTexture(group.name, std::lround(12 * scale), AccentColor), rect.topLeft() + QPointF(4, -16), 1.0, renderTarget,
                        viewport, deviceRegion);
        }
        QList<QRectF> windows;
        QList<QRectF> notes;
        QList<QRectF> active;
        QList<std::pair<KWin::EffectWindow *, QRectF>> icons;
        const auto stack = KWin::effects->stackingOrder();
        for (KWin::EffectWindow *w : stack) {
            const ManagedWindow *managed = m_effect->record(w);
            if (!managed || managed->closed || managed->canvasKey != canvas->key || m_effect->modeOf(*managed) != WindowMode::Canvas) {
                continue;
            }
            const QRectF rect = m_map.toScreen(m_effect->canvasRect(*managed)).intersected(m_mapPanel);
            if (rect.isEmpty()) {
                continue;
            }
            (managed->noteId.isEmpty() ? windows : notes).append(rect);
            if (w == KWin::effects->activeWindow()) {
                active.append(rect);
            }
            if (rect.width() >= 28 && rect.height() >= 28) {
                icons.append({w, rect});
            }
        }
        fillRects(windows, palette.color(QPalette::Base), 1.0, renderTarget, viewport, deviceRegion);
        fillRects(outlines(windows, 1.0), palette.color(QPalette::WindowText), 0.45, renderTarget, viewport, deviceRegion);
        fillRects(notes, NoteColor, 1.0, renderTarget, viewport, deviceRegion);
        fillRects(outlines(active, 2.0), palette.color(QPalette::Highlight), 1.0, renderTarget, viewport, deviceRegion);
        for (const auto &[w, rect] : std::as_const(icons)) {
            const qreal size = std::min({rect.width(), rect.height(), 48.0}) * 0.6;
            const int pixels = std::max(8, int(std::lround(size * scale)));
            drawTexture(iconTexture(w, pixels), rect.center() - QPointF(size / 2, size / 2), 1.0, renderTarget, viewport, deviceRegion);
        }
        const QRectF visible = m_map.toScreen(canvas->camera.visibleCanvasRect(screen)).intersected(m_mapPanel);
        fillRects({visible}, palette.color(QPalette::Highlight), 0.12, renderTarget, viewport, deviceRegion);
        fillRects(outlines({visible}, 2.0), palette.color(QPalette::Highlight), 1.0, renderTarget, viewport, deviceRegion);
        break;
    }
    }
    glDisable(GL_BLEND);
    Q_UNUSED(scale)
}

} // namespace InfinitDesktop

#include "moc_organizer.cpp"
