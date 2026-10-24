# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# KDE Infinit Desktop for NixOS / nixpkgs.
#
#   nix-build packaging/nix                 # build from this source tree
#
# NixOS configuration (Plasma 6, nixpkgs with KWin >= 6.8):
#
#   environment.systemPackages = [ (pkgs.callPackage /path/to/kde-infinit-desktop/packaging/nix { }) ];
#
# KWin finds the effect through the system profile's Qt plugin path. Because
# nixpkgs rebuilds every dependent package when kwin changes, the plugin always
# matches the installed KWin version - no manual rebuild is needed on NixOS.
{
  pkgs ? import <nixpkgs> { },
  src ? pkgs.lib.cleanSourceWith {
    src = ../..;
    filter =
      path: type:
      let
        base = baseNameOf path;
      in
      !(
        type == "directory"
        && builtins.elem base [
          "build"
          "dist"
          ".git"
        ]
      );
  },
}:

pkgs.kdePackages.callPackage (
  {
    lib,
    stdenv,
    cmake,
    ninja,
    extra-cmake-modules,
    wrapQtAppsHook,
    qtbase,
    qtdeclarative,
    qtsvg,
    kconfig,
    kconfigwidgets,
    kcoreaddons,
    kcmutils,
    kglobalaccel,
    ki18n,
    kwidgetsaddons,
    kxmlgui,
    kwin,
    wayland,
    libepoxy,
    libdrm,
    libxkbcommon,
    vulkan-headers,
    vulkan-loader,
    gettext,
  }:
  stdenv.mkDerivation {
    pname = "kde-infinit-desktop";
    version = "0.3.0";
    inherit src;

    nativeBuildInputs = [
      cmake
      ninja
      extra-cmake-modules
      wrapQtAppsHook
      gettext
    ];

    buildInputs = [
      qtbase
      qtdeclarative
      qtsvg
      kconfig
      kconfigwidgets
      kcoreaddons
      kcmutils
      kglobalaccel
      ki18n
      kwidgetsaddons
      kxmlgui
      kwin
      wayland
      libepoxy
      libdrm
      libxkbcommon
      vulkan-headers
      vulkan-loader
    ];

    # Install into $out with the nixpkgs Qt layout (lib/qt-6/plugins), NOT the
    # Qt system paths - the system profile adds it to KWin's plugin path.
    cmakeFlags = [
      (lib.cmakeBool "BUILD_TESTING" true)
      (lib.cmakeBool "BUILD_EFFECT" true)
    ];

    doCheck = true;
    preCheck = ''
      export QT_QPA_PLATFORM=offscreen
    '';

    meta = {
      description = "Turns every monitor into an infinite pannable and zoomable canvas (KWin effect)";
      homepage = "https://github.com/RandomNam3/kde-infinit-desktop";
      license = lib.licenses.gpl3Plus;
      platforms = lib.platforms.linux;
    };
  }
) { }
