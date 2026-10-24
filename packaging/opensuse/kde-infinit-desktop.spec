#
# spec file for package kde-infinit-desktop (openSUSE Tumbleweed / Slowroll)
#
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   ./build.sh --package                         (from the source tree)
#   rpmbuild -ba kde-infinit-desktop.spec        (tarball in SOURCES)
#   rpmbuild -ba --without effect ...            (KWin < 6.8: only the app)
#
# KWin only loads plugins built for exactly its version (the plugin IID
# contains MAJOR.MINOR.PATCH). The package therefore requires exactly the
# kwin6 version it was built against. On the openSUSE Build Service the
# package is rebuilt automatically whenever kwin6 changes (see docs/PAKETE.md).
#
# The URL is a placeholder until the project has a public repository.

%bcond_without effect
%bcond_without app
%bcond_without tests

%if %{with effect}
%global kwin_version %(rpm -q --qf '%%{VERSION}\\n' kwin6-devel 2>/dev/null | grep -E '^[0-9]' | head -n1)
%if "%{kwin_version}" != ""
%global kwin_tag .kwin%{kwin_version}
%endif
%endif

Name:           kde-infinit-desktop
Version:        0.4.1
Release:        1%{?kwin_tag}
Summary:        Infinite pannable and zoomable canvas for KDE Plasma (KWin effect)
License:        GPL-3.0-or-later
URL:            https://github.com/RandomNam3/kde-infinit-desktop
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake >= 3.22
BuildRequires:  gcc-c++
BuildRequires:  gettext-tools
BuildRequires:  kf6-extra-cmake-modules
BuildRequires:  ninja
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6GlobalAccel)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(Qt6Core) >= 6.6
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Widgets)
%if %{with tests}
BuildRequires:  cmake(Qt6Test)
%endif
%if %{with effect}
BuildRequires:  kwin6-devel >= 6.8
BuildRequires:  cmake(KF6ConfigWidgets)
BuildRequires:  cmake(KF6KCMUtils)
BuildRequires:  cmake(KF6WidgetsAddons)
BuildRequires:  cmake(KF6XmlGui)
# find_dependency() of KWinConfig.cmake, not pulled in by kwin6-devel
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  vulkan-headers
BuildRequires:  pkgconfig(epoxy)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(vulkan)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xkbcommon)
%if "%{?kwin_version}" != ""
Requires:       kwin6 = %{kwin_version}
%else
Requires:       kwin6 >= 6.8
%endif
%endif
%if %{with app}
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Svg)
Recommends:     %{name}-settings = %{version}
%endif

%description
KDE Infinit Desktop turns every monitor into an infinite canvas: windows are
placed on a plane that can be panned and zoomed freely with the mouse, the
touchpad, a touchscreen or the keyboard. It is an effect for KWin, the
compositor of KDE Plasma 6 on Wayland, plus a System Settings module.

This build is for exactly one KWin version and has to be rebuilt after every
KWin update.

%if %{with app}
%package settings
Summary:        Settings app for KDE Infinit Desktop
Requires:       %{name} = %{version}
Requires:       libQt6Svg6
Requires:       qt6-declarative-imports

%description settings
Standalone settings app (kde-infinit-desktop-settings) for KDE Infinit
Desktop, the infinite canvas for KDE Plasma.
%endif

%prep
%autosetup -p1

%build
%cmake_kf6 \
  -DKDE_INSTALL_USE_QT_SYS_PATHS:BOOL=ON \
  -DBUILD_EFFECT:BOOL=%{?with_effect:ON}%{!?with_effect:OFF} \
  -DBUILD_KCM:BOOL=%{?with_effect:ON}%{!?with_effect:OFF} \
  -DBUILD_SETTINGS_APP:BOOL=%{?with_app:ON}%{!?with_app:OFF} \
  -DBUILD_TESTING:BOOL=%{?with_tests:ON}%{!?with_tests:OFF}
%kf6_build

%install
%kf6_install
# Translations (po/) are optional while the project is young; without them give
# "%files -f" a (harmless, duplicate) entry, an empty file list is an error.
%find_lang kwin_infinitdesktop || :
[ -s kwin_infinitdesktop.lang ] || echo '%{_datadir}/config.kcfg/infinitdesktop.kcfg' > kwin_infinitdesktop.lang

%check
%if %{with tests}
export QT_QPA_PLATFORM=offscreen
%ctest
%endif

%files -f kwin_infinitdesktop.lang
%{_datadir}/config.kcfg/infinitdesktop.kcfg
%{_kf6_dbusinterfacesdir}/org.kde.InfinitDesktop.xml
%if %{with effect}
%dir %{_kf6_plugindir}/kwin
%dir %{_kf6_plugindir}/kwin/effects
%dir %{_kf6_plugindir}/kwin/effects/plugins
%dir %{_kf6_plugindir}/kwin/effects/configs
%{_kf6_plugindir}/kwin/effects/plugins/infinitdesktop.so
%{_kf6_plugindir}/kwin/effects/configs/kwin_infinitdesktop_config.so
%dir %{_datadir}/kde-infinit-desktop
%{_datadir}/kde-infinit-desktop/built-for-kwin
%endif

%if %{with app}
%files settings
%{_kf6_bindir}/kde-infinit-desktop-settings
%{_kf6_applicationsdir}/kde-infinit-desktop-settings.desktop
%{_kf6_iconsdir}/hicolor/*/apps/org.kde.infinitdesktop.*
%{_kf6_appstreamdir}/org.kde.infinitdesktop.metainfo.xml
%endif

%changelog
