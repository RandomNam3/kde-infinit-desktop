# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fedora (and RHEL / AlmaLinux / Rocky Linux 10 with EPEL + CRB, "EPEL KDE").
#
#   ./build.sh --package                          (from the source tree)
#   rpmbuild -ba kde-infinit-desktop.spec         (tarball in SOURCES)
#   rpmbuild -ba --without effect ...             (KWin < 6.8: only the app)
#
# KWin only loads plugins built for exactly its version (the plugin IID
# contains MAJOR.MINOR.PATCH), so the package requires exactly the KWin
# version it was built against and carries it in the release tag
# (e.g. 0.1.0-1.kwin6.8.1.fc46). Rebuild it for every KWin update - COPR or
# the openSUSE Build Service can do that automatically, see docs/PAKETE.md.
#
# The URL is a placeholder until the project has a public repository.

%bcond_without effect
%bcond_without app
%bcond_without tests

%if %{with effect}
# Version of the kwin-devel we build against (empty while parsing the spec
# without the build dependencies, e.g. for the SRPM).
%global kwin_version %(rpm -q --qf '%%{VERSION}\\n' kwin-devel 2>/dev/null | grep -E '^[0-9]' | head -n1)
%if "%{kwin_version}" != ""
%global kwin_tag .kwin%{kwin_version}
%endif
%endif

Name:           kde-infinit-desktop
Version:        0.4.2
Release:        1%{?kwin_tag}%{?dist}
Summary:        Infinite pannable and zoomable canvas for KDE Plasma (KWin effect)
License:        GPL-3.0-or-later
URL:            https://github.com/RandomNam3/kde-infinit-desktop
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.22
BuildRequires:  ninja-build
BuildRequires:  gettext
BuildRequires:  extra-cmake-modules
BuildRequires:  kf6-rpm-macros
BuildRequires:  cmake(Qt6Core) >= 6.6
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6GlobalAccel)
BuildRequires:  cmake(KF6I18n)
%if %{with tests}
BuildRequires:  cmake(Qt6Test)
%endif
%if %{with effect}
BuildRequires:  cmake(KWin) >= 6.8
BuildRequires:  kwin-devel
BuildRequires:  cmake(KF6KCMUtils)
BuildRequires:  cmake(KF6ConfigWidgets)
BuildRequires:  cmake(KF6XmlGui)
BuildRequires:  cmake(KF6WidgetsAddons)
# find_dependency() of KWinConfig.cmake, not pulled in by kwin-devel
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(epoxy)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  vulkan-headers
BuildRequires:  vulkan-loader-devel
%endif
%if %{with app}
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
%endif

%if %{with effect}
%if "%{?kwin_version}" != ""
Requires:       kwin%{?_isa} = %{kwin_version}
%else
Requires:       kwin%{?_isa} >= 6.8
%endif
%endif
%if %{with app}
Recommends:     %{name}-settings = %{version}-%{release}
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
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       qt6-qtdeclarative%{?_isa}
Requires:       qt6-qtsvg%{?_isa}

%description settings
Standalone settings app (kde-infinit-desktop-settings) for KDE Infinit
Desktop, the infinite canvas for KDE Plasma.
%endif

%prep
%autosetup -p1

%build
%cmake_kf6 \
    -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
    -DBUILD_EFFECT=%{?with_effect:ON}%{!?with_effect:OFF} \
    -DBUILD_KCM=%{?with_effect:ON}%{!?with_effect:OFF} \
    -DBUILD_SETTINGS_APP=%{?with_app:ON}%{!?with_app:OFF} \
    -DBUILD_TESTING=%{?with_tests:ON}%{!?with_tests:OFF}
%cmake_build

%install
%cmake_install
# Translations (po/) are optional while the project is young; without them give
# "%files -f" a (harmless, duplicate) entry, an empty file list is an error.
%find_lang kwin_infinitdesktop || :
[ -s kwin_infinitdesktop.lang ] || echo '%{_datadir}/config.kcfg/infinitdesktop.kcfg' > kwin_infinitdesktop.lang

%check
%if %{with tests}
export QT_QPA_PLATFORM=offscreen
%ctest
%endif
%if %{with app}
desktop-file-validate %{buildroot}%{_datadir}/applications/kde-infinit-desktop-settings.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/org.kde.infinitdesktop.companion.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/org.kde.infinitdesktop.metainfo.xml
%endif

%files -f kwin_infinitdesktop.lang
%{_datadir}/config.kcfg/infinitdesktop.kcfg
%{_datadir}/dbus-1/interfaces/org.kde.InfinitDesktop.xml
%if %{with effect}
%{_kf6_qtplugindir}/kwin/effects/plugins/infinitdesktop.so
%{_kf6_qtplugindir}/kwin/effects/configs/kwin_infinitdesktop_config.so
%dir %{_datadir}/kde-infinit-desktop
%{_datadir}/kde-infinit-desktop/built-for-kwin
%endif

%if %{with app}
%files settings
%{_bindir}/kde-infinit-desktop-settings
%{_datadir}/applications/kde-infinit-desktop-settings.desktop
%{_bindir}/kde-infinit-desktop-companion
%{_datadir}/applications/org.kde.infinitdesktop.companion.desktop
%{_datadir}/dbus-1/services/org.kde.InfinitDesktop.Companion.service
%{_sysconfdir}/xdg/autostart/org.kde.infinitdesktop.companion.desktop
%{_datadir}/icons/hicolor/*/apps/org.kde.infinitdesktop.*
%{_metainfodir}/org.kde.infinitdesktop.metainfo.xml
%endif

%changelog
* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.4.2-1
- Update to 0.4.2

* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.4.1-1
- Update to 0.4.1

* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.4.0-1
- Update to 0.4.0

* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.3.0-1
- Update to 0.3.0

* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.2.0-1
- Update to 0.2.0

* Sat Oct 24 2026 KDE Infinit Desktop contributors <maintainers@kde-infinit-desktop.invalid> - 0.1.0-1
- Initial package
