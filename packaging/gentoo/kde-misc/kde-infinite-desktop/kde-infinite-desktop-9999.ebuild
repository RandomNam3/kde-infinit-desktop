# Copyright 2026 KDE Infinite Desktop contributors
# Distributed under the terms of the GNU General Public License v3 or later

EAPI=8

ECM_TEST="true"
KFMIN=6.22.0
QTMIN=6.8.0
inherit ecm git-r3

DESCRIPTION="Turns every monitor into an infinite pannable and zoomable canvas (KWin effect)"
HOMEPAGE="https://github.com/RandomNam3/kde-infinite-desktop"
# To build from a local clone set (e.g. in /etc/portage/make.conf)
#   EGIT_OVERRIDE_REPO_RANDOMNAM3_KDE_INFINITE_DESKTOP=file:///path/to/clone
EGIT_REPO_URI="https://github.com/RandomNam3/kde-infinite-desktop.git"

LICENSE="GPL-3+"
SLOT="0"
KEYWORDS=""
IUSE="+gui"

# KWin only loads plugins built for exactly its version (the plugin IID
# contains MAJOR.MINOR.PATCH). kde-plasma/kwin has no sub-slot, so a slot
# operator cannot trigger the rebuild: re-emerge this package after every
# kwin update (see pkg_postinst and docs/INSTALL.md).
DEPEND="
	>=dev-qt/qtbase-${QTMIN}:6[dbus,gui,widgets]
	>=kde-frameworks/kconfig-${KFMIN}:6
	>=kde-frameworks/kconfigwidgets-${KFMIN}:6
	>=kde-frameworks/kcoreaddons-${KFMIN}:6
	>=kde-frameworks/kcmutils-${KFMIN}:6
	>=kde-frameworks/kglobalaccel-${KFMIN}:6
	>=kde-frameworks/ki18n-${KFMIN}:6
	>=kde-frameworks/kwidgetsaddons-${KFMIN}:6
	>=kde-frameworks/kxmlgui-${KFMIN}:6
	>=kde-plasma/kwin-6.8:6
	>=dev-qt/qtdeclarative-${QTMIN}:6
	media-libs/libepoxy
	x11-libs/libdrm
	x11-libs/libxkbcommon
	dev-libs/wayland
	gui? (
		>=dev-qt/qtsvg-${QTMIN}:6
	)
"
RDEPEND="${DEPEND}"
BDEPEND="
	dev-util/vulkan-headers
	sys-devel/gettext
"

src_configure() {
	local mycmakeargs=(
		-DBUILD_EFFECT=ON
		-DBUILD_KCM=ON
		-DBUILD_SETTINGS_APP=$(usex gui)
	)
	ecm_src_configure
}

src_test() {
	local -x QT_QPA_PLATFORM=offscreen
	ecm_src_test
}

pkg_postinst() {
	ecm_pkg_postinst
	local kwin_ver
	kwin_ver=$(best_version kde-plasma/kwin)
	elog "KDE Infinite Desktop was built for ${kwin_ver#kde-plasma/}."
	elog "KWin only loads plugins built for exactly its version: after every"
	elog "kde-plasma/kwin update run"
	elog "    emerge --oneshot kde-misc/kde-infinite-desktop"
	elog "and log out and back in. Enable the effect in System Settings >"
	elog "Window Management > Desktop Effects."
}
