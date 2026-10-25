/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "settingsmeta.h"

#include <KLocalizedString>

#include <cstring>

namespace InfinitDesktop
{

const QList<SettingsCategory> &settingsCategories()
{
    static const QList<SettingsCategory> categories{
        {"mouse", kli18n("Mouse"), kli18n("Pan and zoom with the mouse"), "mouse"},
        {"touch", kli18n("Touchpad & Touch"), kli18n("Gestures on touchpads and touchscreens"), "touchpad"},
        {"zoom", kli18n("Zoom & Motion"), kli18n("Zoom levels, gliding and animations"), "zoom"},
        {"windows", kli18n("Windows"), kli18n("How windows live on the canvas"), "windows"},
        {"screens", kli18n("Multiple Screens"), kli18n("One canvas per screen and moving windows between them"), "screens"},
        {"fullscreen", kli18n("Fullscreen & Games"), kli18n("Videos, games and other fullscreen windows"), "fullscreen"},
        {"appearance", kli18n("Appearance"), kli18n("Dot grid and visual feedback"), "appearance"},
        {"organize", kli18n("Notes & Groups"), kli18n("Notes, groups of windows, areas and the map"), "note"},
        {"plasma", kli18n("Plasma"), kli18n("Integration with the task bar and launcher"), "plasma"},
        {ShortcutsCategoryId, kli18n("Shortcuts"), kli18n("Keyboard shortcuts for fast navigation"), "keyboard"},
        {"advanced", kli18n("Advanced"), kli18n("Stability, persistence and diagnostics"), "advanced"},
    };
    return categories;
}

const QList<SettingsEntryMeta> &settingsEntries()
{
    static const QList<SettingsEntryMeta> entries{
        // Mouse
        {"DragDesktopToPan", "mouse", nullptr, kli18n("move,drag,scroll,canvas,left button"), nullptr},
        {"MiddleButtonPan", "mouse", nullptr, kli18n("move,drag,scroll,middle button"), nullptr},
        {"PanModifier", "mouse", nullptr, kli18n("move,drag,key,meta,alt,modifier"), nullptr},
        {"WheelZoomOnDesktop", "mouse", nullptr, kli18n("scroll,wheel,magnify,zoom"), nullptr},
        {"ZoomModifier", "mouse", nullptr, kli18n("scroll,wheel,magnify,key,meta"), nullptr},
        {"InvertWheelZoom", "mouse", nullptr, kli18n("reverse,direction,scroll,wheel"), "WheelZoomOnDesktop"},
        {"ShiftWheelPans", "mouse", nullptr, kli18n("horizontal,sideways,scroll,shift,ctrl"), nullptr},
        // Touchpad & touch
        {"TouchpadScrollPans", "touch", nullptr, kli18n("two fingers,scroll,move,trackpad"), nullptr},
        {"SwipeFingers", "touch", nullptr, kli18n("gesture,three fingers,four fingers,swipe,trackpad"), nullptr},
        {"PinchZoom", "touch", nullptr, kli18n("gesture,pinch,magnify,trackpad"), nullptr},
        {"TouchscreenPan", "touch", nullptr, kli18n("finger,tablet,touch,drag"), nullptr},
        {"InvertScrollPan", "touch", nullptr, kli18n("natural,reverse,direction"), nullptr},
        {"PanSpeed", "touch", "%", kli18n("sensitivity,speed,acceleration"), nullptr},
        // Zoom & motion
        {"ZoomStep", "zoom", "%", kli18n("wheel,notch,sensitivity"), nullptr},
        {"MinZoom", "zoom", "%", kli18n("minimum,overview,zoom out,limit"), nullptr},
        {"MaxZoom", "zoom", "%", kli18n("maximum,magnify,zoom in,limit"), nullptr},
        {"SnapZoomToOne", "zoom", nullptr, kli18n("snap,100,normal,magnet"), nullptr},
        {"SharpZoomedContent", "zoom", nullptr, kli18n("sharp,readable,blurry,text,font,quality,fractional,scale"), nullptr},
        {"Momentum", "zoom", nullptr, kli18n("inertia,kinetic,flick,glide"), nullptr},
        {"Friction", "zoom", nullptr, kli18n("inertia,kinetic,deceleration,stop"), "Momentum"},
        {"AnimationDuration", "zoom", "ms", kli18n("speed,animation,transition,time"), nullptr},
        {"KeyboardPanStep", "zoom", "%", kli18n("keyboard,arrow,step,distance"), nullptr},
        // Windows
        {"RevealOnActivate", "windows", nullptr, kli18n("taskbar,alt tab,activate,follow,jump"), nullptr},
        {"RevealMode", "windows", nullptr, kli18n("center,scroll,follow"), "RevealOnActivate"},
        {"NewWindowsExitZoom", "windows", nullptr, kli18n("open,launch,new,zoom"), nullptr},
        {"InteractWhileZoomed", "windows", nullptr, kli18n("interact,use,type,click,scroll,zoomed,overview,notes"), nullptr},
        {"ClickZoomedWindowFocuses", "windows", nullptr, kli18n("click,overview,open,focus"), nullptr},
        {"PinOnAllDesktops", "windows", nullptr, kli18n("sticky,picture in picture,pip,fixed"), nullptr},
        {"PinKeepAboveWindows", "windows", nullptr, kli18n("pin,always on top,keep above,fixed,sticky"), nullptr},
        {"PerDesktopCanvas", "windows", nullptr, kli18n("virtual desktop,workspace,camera"), nullptr},
        {"ExcludedWindowClasses", "windows", nullptr, kli18n("exclude,ignore,blacklist,application,class"), nullptr},
        {"RememberWindowPlacement", "windows", nullptr, kli18n("remember,restore,session,position,size,reopen,monitor"), nullptr},
        {"RestorePlacementMode", "windows", nullptr, kli18n("remember,restore,canvas,screen,position"), "RememberWindowPlacement"},
        {"RestoreWindowSize", "windows", nullptr, kli18n("remember,restore,size,maximized"), "RememberWindowPlacement"},
        {"PlacementExcludedClasses", "windows", nullptr, kli18n("remember,restore,exclude,ignore,application"), "RememberWindowPlacement"},
        // Multiple screens
        {"FlingToOtherScreen", "screens", nullptr, kli18n("monitor,display,drag,throw,move"), nullptr},
        {"FlingThreshold", "screens", "px/s", kli18n("monitor,speed,fast,throw"), "FlingToOtherScreen"},
        {"EdgePan", "screens", nullptr, kli18n("edge,border,scroll,drag"), nullptr},
        {"EdgePanMargin", "screens", "px", kli18n("edge,border,zone,width"), "EdgePan"},
        {"EdgePanSpeed", "screens", "px/s", kli18n("edge,border,speed"), "EdgePan"},
        // Fullscreen
        {"LockFullscreen", "fullscreen", nullptr, kli18n("game,video,movie,lock,focus"), nullptr},
        {"MinimizedFullscreenOnCanvas", "fullscreen", nullptr, kli18n("minimize,tile,card,game,video"), nullptr},
        {"MinimizedFullscreenContent", "fullscreen", nullptr, kli18n("thumbnail,icon,performance,power,live"), "MinimizedFullscreenOnCanvas"},
        {"TileSize", "fullscreen", "%", kli18n("tile,card,size,thumbnail"), "MinimizedFullscreenOnCanvas"},
        // Appearance
        {"BackgroundGrid", "appearance", nullptr, kli18n("dots,grid,background,wallpaper,pattern"), nullptr},
        {"GridSpacing", "appearance", "px", kli18n("dots,grid,distance"), "BackgroundGrid"},
        {"GridOpacity", "appearance", "%", kli18n("dots,grid,transparency,visibility"), "BackgroundGrid"},
        {"GridColor", "appearance", nullptr, kli18n("dots,grid,colour"), "BackgroundGrid"},
        {"ZoomIndicator", "appearance", nullptr, kli18n("osd,percent,zoom level,overlay"), nullptr},
        {"OffscreenMarkers", "appearance", nullptr, kli18n("find,lost,far away,offscreen,arrow,indicator,edge,navigation"), nullptr},
        // Notes, groups, areas and the map
        {"NotesEnabled", "organize", nullptr, kli18n("note,sticky,post-it,memo,markdown,text"), nullptr},
        {"NoteColor", "organize", nullptr, kli18n("note,sticky,colour,paper"), "NotesEnabled"},
        {"GroupLayoutMode", "organize", nullptr, kli18n("group,arrangement,layout,save,workspace,session"), nullptr},
        {"GroupStartsApplications", "organize", nullptr, kli18n("group,launch,start,open,application"), nullptr},
        {"GroupZoomToFit", "organize", nullptr, kli18n("group,zoom,fit,camera,jump"), nullptr},
        {"ShowAreas", "organize", nullptr, kli18n("area,region,zone,frame,section"), nullptr},
        {"AreaPadding", "organize", "px", kli18n("area,region,margin,space,grow"), "ShowAreas"},
        {"MapSize", "organize", "%", kli18n("map,minimap,overview,navigator"), nullptr},
        // Plasma
        {"LauncherPosition", "plasma", nullptr, kli18n("start menu,kickoff,super,windows key,application launcher,meta"), nullptr},
        // Advanced
        {"CrashGuard", "advanced", nullptr, kli18n("crash,safe mode,stability,recovery"), nullptr},
        {"RememberCanvas", "advanced", nullptr, kli18n("restore,session,persist,save,bookmarks"), nullptr},
        {"DebugLogging", "advanced", nullptr, kli18n("log,journal,debug,bug report"), nullptr},
    };
    return entries;
}

const SettingsEntryMeta *findSettingsEntry(const char *key)
{
    for (const SettingsEntryMeta &entry : settingsEntries()) {
        if (std::strcmp(entry.key, key) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace InfinitDesktop
