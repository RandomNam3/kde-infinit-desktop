# KDE Infinit Desktop installieren

> „Wenn es KDE Plasma ausführt, läuft auch KDE Infinit Desktop.“

KDE Infinit Desktop ist ein Effekt-Plugin für **KWin**, den Compositor von
KDE Plasma 6. Er macht jeden Monitor zu einer unendlichen Arbeitsfläche, die
man verschieben und zoomen kann. Dazu gehören ein Modul für die
Systemeinstellungen und eine eigene Einstellungs-App.

**Voraussetzungen**

- KDE Plasma mit **KWin 6.8 oder neuer** (ältere Versionen haben eine andere Plugin-Schnittstelle)
- eine **Wayland**-Sitzung („Plasma (Wayland)“ im Anmeldebildschirm). Unter X11 wird der Effekt nicht unterstützt.

> Quellcode: <https://github.com/RandomNam3/kde-infinit-desktop> (Branch `main`).
> Ein Release-Tag `v0.1.0` gibt es noch nicht – die Release-Pakete
> (`packaging/arch/PKGBUILD`, Spec-Dateien) erwarten
> `https://github.com/RandomNam3/kde-infinit-desktop/archive/refs/tags/v0.1.0.tar.gz`;
> bis der Tag angelegt ist, baut man aus dem Git-Klon (`./install.sh`, `./build.sh --package`).

---

## Inhalt

1. [Schnellstart: ein Befehl](#1-schnellstart-ein-befehl)
2. [Welche Distributionen? (Stand 24.10.2026)](#2-welche-distributionen-stand-24102026)
3. [Warum nach jedem KWin-Update neu gebaut werden muss](#3-warum-nach-jedem-kwin-update-neu-gebaut-werden-muss)
4. [Der Installer im Detail](#4-der-installer-im-detail)
5. [Anleitungen je Distribution](#5-anleitungen-je-distribution)
6. [Pakete selbst bauen](#6-pakete-selbst-bauen)
7. [Manuell mit CMake bauen](#7-manuell-mit-cmake-bauen)
8. [Effekt aktivieren und deaktivieren](#8-effekt-aktivieren-und-deaktivieren)
9. [Nach KWin-Updates](#9-nach-kwin-updates)
10. [Deinstallieren](#10-deinstallieren)
11. [Fehlerbehebung](#11-fehlerbehebung)
12. [Für Entwickler](#12-für-entwickler)

---

## 1. Schnellstart: ein Befehl

Im Quellverzeichnis (`git clone https://github.com/RandomNam3/kde-infinit-desktop.git`), als **normaler Benutzer** (nicht mit `sudo` – das Skript
fragt selbst nach dem Passwort, wenn es Root-Rechte braucht):

```sh
./install.sh
```

(`./install.sh` ruft `scripts/install.sh` auf; falls die Datei nicht ausführbar
ist: `bash install.sh`.)

Oder ganz ohne vorherigen Download (lädt den Branch `main` herunter):

```sh
curl -fsSL https://raw.githubusercontent.com/RandomNam3/kde-infinit-desktop/main/install.sh | bash
# mit Optionen:
curl -fsSL https://raw.githubusercontent.com/RandomNam3/kde-infinit-desktop/main/install.sh | bash -s -- --yes
```

Der Installer

1. erkennt die Distribution (`/etc/os-release`, Felder `ID` und `ID_LIKE`),
2. prüft, ob KWin ≥ 6.8 installiert ist und ob eine Wayland-Sitzung läuft,
3. zeigt die fehlenden Build-Abhängigkeiten an und installiert sie nach
   Rückfrage mit dem Paketmanager der Distribution,
4. baut in einem temporären Verzeichnis und installiert mit `sudo`,
5. legt eine Kopie des Quellcodes in `/usr/src/kde-infinit-desktop-<version>`
   ab und richtet den **automatischen Neubau nach KWin-Updates** ein,
6. aktiviert den Effekt in der laufenden Sitzung.

Danach muss man sich um nichts mehr kümmern: Nach einem Plasma-Update wird der
Effekt automatisch neu gebaut; man meldet sich nur – wie nach jedem
Plasma-Update ohnehin empfohlen – einmal ab und wieder an.

Vorher ansehen, was passieren würde (ändert nichts):

```sh
./install.sh --dry-run
```

---

## 2. Welche Distributionen? (Stand 24.10.2026)

Ermittelt aus den Paketquellen der Distributionen (Debian `madison`,
Launchpad, Fedora mdapi/Bodhi, Arch-, Manjaro- und CachyOS-Repository-Datenbanken,
packages.gentoo.org, nixpkgs, Alpine, Void, Solus) bzw. direkt in Containern
(KDE neon, Fedora, openSUSE, Debian, AlmaLinux).

| Distribution | KWin / Plasma | ≥ 6.8? | Hinweis |
|---|---|:---:|---|
| **KDE neon** User Edition (Basis Ubuntu 24.04) | 6.8.1 | ✅ | Haupt-Testsystem |
| **Arch Linux**, EndeavourOS | 6.8.1 | ✅ | |
| **CachyOS** (cachyos-extra-v3/-v4) | 6.8.1 | ✅ | |
| Manjaro Stable | 6.7.4 | ❌ | Testing 6.7.5 ❌, **Unstable 6.8.1 ✅** |
| **openSUSE Tumbleweed** (Snapshot 20261022) | 6.8.1 | ✅ | Slowroll folgt mit Verzögerung |
| openSUSE Leap 16.0 | 6.4.2 | ❌ | |
| **Fedora Rawhide** (46) | 6.8.1 | ✅ | |
| Fedora 45 | 6.7.5 | ❌ | Update auf 6.8.1 ist in Bodhi eingereicht (23.10.2026) – bald ✅ |
| Fedora 44, 43 | 6.7.5 | ❌ | Plasma-Updates kommen bei Fedora auch für stabile Versionen |
| RHEL / AlmaLinux / Rocky Linux 10 + EPEL | 6.7.5 | ❌ | „EPEL KDE“, folgt Fedora |
| RHEL / AlmaLinux / Rocky Linux 9 + EPEL | 5.27.12 | ❌ | |
| Debian sid / testing (forky) | 6.7.4 | ❌ | |
| Debian 13 „trixie“ | 6.3.6 | ❌ | |
| Debian 12 „bookworm“ | 5.27.5 | ❌ | |
| Ubuntu / Kubuntu 26.10 | 6.7.5 | ❌ | |
| Ubuntu / Kubuntu 26.04 LTS | 6.6.6 | ❌ | auch Kubuntu-Backports-PPA: 6.6.6 |
| Ubuntu / Kubuntu 24.04 LTS | 5.27.11 | ❌ | → **KDE neon** verwenden |
| **Gentoo** | 6.8.1 (`~amd64`, `~arm64`) | ✅ | stabil: 6.7.5 ❌ |
| **NixOS unstable** | 6.8.0 | ✅ | NixOS 26.05: 6.6.6 ❌ |
| Alpine edge / postmarketOS edge | 6.8.1 | ✅ | Alpine 3.23: 6.5.6 ❌ |
| Solus | 6.8.1 | ✅ | Installer-Unterstützung ungetestet |
| OpenMandriva (Cooker/ROME) | 6.8.1 | ✅ | ungetestet |
| Chimera Linux | 6.8.1 | ✅ | vom Installer nicht unterstützt |
| Void Linux | 6.7.5 | ❌ | |

Die installierte Version zeigt `kwin_wayland --version` oder
`./scripts/check-kwin-update.sh --print-installed-kwin`.

---

## 3. Warum nach jedem KWin-Update neu gebaut werden muss

KWin-Plugins haben **keine stabile Binärschnittstelle**. KWin erkennt passende
Plugins an einer Kennung, die die vollständige KWin-Version enthält
(`org.kde.kwin.EffectPluginFactory` + „6.8.1“). Ein für KWin 6.8.1 gebautes
Plugin wird von KWin 6.8.2 deshalb **stillschweigend nicht geladen** – es
stürzt nichts ab, der Effekt ist einfach weg, bis er neu gebaut wurde.

Darum

- merkt sich jede Installation, für welche KWin-Version sie gebaut wurde:
  `/usr/share/kde-infinit-desktop/built-for-kwin`,
- richtet `scripts/install.sh` einen Hook im Paketmanager ein, der nach einem
  KWin-Update automatisch neu baut (siehe [Abschnitt 9](#9-nach-kwin-updates)),
- hängen die fertigen Pakete (`.deb`, `.rpm`, Arch-Binärpaket) von genau der
  KWin-Version ab, gegen die sie gebaut wurden (siehe [PAKETE.md](PAKETE.md)).

---

## 4. Der Installer im Detail

```text
./install.sh [OPTIONEN]          (= scripts/install.sh)

  -y, --yes             keine Rückfragen
  -n, --dry-run         nur anzeigen, was passieren würde
      --no-deps         keine Abhängigkeiten installieren
      --deps            Abhängigkeiten auch bei --rebuild installieren
      --deps-only       nur die Abhängigkeiten installieren (CI, Container)
      --prefix DIR      Installationspräfix (Standard: /usr)
      --rebuild         neu bauen und installieren (mit den Einstellungen der
                        letzten Installation, ohne Abhängigkeiten)
      --uninstall       deinstallieren
      --no-auto-rebuild keinen Paketmanager-Hook für den automatischen Neubau
      --no-update-check keine Prüfung beim Anmelden
      --no-enable       den Effekt nicht aktivieren
      --run-tests       vor der Installation die Unit-Tests bauen und ausführen
      --build-type T    CMake-Build-Typ (Standard: Release)
      --build-dir DIR   in DIR bauen und es behalten
      --source DIR      Quellcode aus DIR verwenden
      --source-url URL  Quellcode-Archiv (.tar.gz) herunterladen, wenn das
                        Skript nicht aus einem Quellbaum gestartet wurde
      --without-app     ohne die Einstellungs-App
      --without-kcm     ohne das Systemeinstellungsmodul
      --force           trotz fehlgeschlagener Prüfungen weitermachen
  -h, --help            Hilfe
```

**Was wohin installiert wird** (Präfix `/usr`):

| Datei | Zweck |
|---|---|
| `<Qt-Plugin-Verzeichnis>/kwin/effects/plugins/infinitdesktop.so` | der Effekt (z. B. `/usr/lib/x86_64-linux-gnu/qt6/plugins/…` auf Debian/neon, `/usr/lib/qt6/plugins/…` auf Arch, `/usr/lib64/qt6/plugins/…` auf Fedora/openSUSE) |
| `<Qt-Plugin-Verzeichnis>/kwin/effects/configs/kwin_infinitdesktop_config.so` | Modul für die Systemeinstellungen |
| `/usr/bin/kde-infinit-desktop-settings`, `/usr/share/applications/kde-infinit-desktop-settings.desktop`, `/usr/share/icons/hicolor/*/apps/org.kde.infinitdesktop.{svg,png}` | Einstellungs-App |
| `/usr/share/config.kcfg/infinitdesktop.kcfg`, `/usr/share/dbus-1/interfaces/org.kde.InfinitDesktop.xml`, `/usr/share/metainfo/org.kde.infinitdesktop.metainfo.xml` | Einstellungsschema, D-Bus-Schnittstelle, AppStream-Daten |
| `/usr/share/kde-infinit-desktop/` | `built-for-kwin`, `install-info`, `install_manifest.txt` |
| `/usr/lib/kde-infinit-desktop/` | `check-kwin-update.sh`, `kwin-update-hook.sh`, `uninstall.sh` |
| `/usr/src/kde-infinit-desktop-<version>/` | Quellcode für den automatischen Neubau (ca. 1 MB) |
| Paketmanager-Hook (siehe [Abschnitt 9](#9-nach-kwin-updates)) | automatischer Neubau |
| `/etc/xdg/autostart/kde-infinit-desktop-kwin-check.desktop` | Prüfung beim Anmelden |

Der Effekt landet immer im Qt-Plugin-Verzeichnis
(`-DKDE_INSTALL_USE_QT_SYS_PATHS=ON`), auch mit `--prefix /usr/local` – nur
dort sucht KWin nach Effekten.

**Unveränderliche Systeme** (Fedora Kinoite/Atomic, Aurora, Bazzite, SteamOS,
openSUSE Aeon/Kalpa) haben ein schreibgeschütztes `/usr`; dort bricht der
Installer ab. Möglich ist dort: in einer Toolbox/Distrobox mit
`./build.sh --package` ein RPM bauen und es mit
`rpm-ostree install ./dist/kde-infinit-desktop-*.rpm` einspielen – nach jedem
KWin-Update erneut.

---

## 5. Anleitungen je Distribution

Die Paketnamen in den Listen hat der Installer in Containern tatsächlich
installiert (siehe [Abschnitt 12](#12-für-entwickler)).

### KDE neon, Ubuntu, Kubuntu, Debian

KDE neon ist derzeit die einzige Debian/Ubuntu-artige Distribution mit KWin 6.8.

```sh
./install.sh
```

Benötigte Pakete (Debian-Namen; neon stellt sie ebenfalls bereit):

```text
build-essential cmake ninja-build pkgconf gettext extra-cmake-modules
qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-svg-dev
libkf6config-dev libkf6coreaddons-dev libkf6globalaccel-dev libkf6i18n-dev
libkf6kcmutils-dev libkf6configwidgets-dev libkf6xmlgui-dev libkf6widgetsaddons-dev
kwin-dev libwayland-dev libepoxy-dev libdrm-dev libvulkan-dev libxkbcommon-dev
```

Laufzeit (Einstellungs-App): `qml6-module-qtquick qml6-module-qtquick-controls
qml6-module-qtquick-layouts qml6-module-qtquick-effects qml6-module-qtquick-shapes
qml6-module-qtquick-window qml6-module-qtqml-workerscript qt6-svg-plugins`.

Automatischer Neubau: `/etc/apt/apt.conf.d/99kde-infinit-desktop`
(`DPkg::Post-Invoke`, vergleicht nur zwei Versionsnummern, solange sich KWin
nicht geändert hat).

Als `.deb`: `./build.sh --package`, dann
`sudo apt install ./dist/*.deb` (siehe [Abschnitt 6](#6-pakete-selbst-bauen)).

### Arch Linux, CachyOS, EndeavourOS, Manjaro

```sh
./install.sh
```

Pakete: `base-devel cmake ninja extra-cmake-modules qt6-base qt6-declarative
qt6-svg qt6-tools kconfig kcoreaddons kglobalaccel ki18n kcmutils kconfigwidgets
kxmlgui kwidgetsaddons kwin wayland libepoxy libdrm vulkan-headers libxkbcommon`.
Wichtig: `vulkan-headers` wird vom `kwin`-Paket nicht mitinstalliert, aber von
KWins CMake-Konfiguration verlangt.

Automatischer Neubau: `/etc/pacman.d/hooks/kde-infinit-desktop.hook`
(Trigger: Paket `kwin`).

Alternativ als Paket (AUR-Stil):

```sh
cd packaging/arch-git      # Entwicklungsversion direkt von GitHub (Branch main)
makepkg -si
# aus dem lokalen Klon statt von GitHub:
KID_GIT_URL=file:///pfad/zum/klon makepkg -si
# Release-Tarball (erst, wenn der Tag v0.1.0 existiert):
cd packaging/arch && makepkg -si
# oder ohne Tag aus dem Quellbaum:
./build.sh --package && sudo pacman -U ./dist/kde-infinit-desktop-*.pkg.tar.zst
```

Das AUR-Paket baut sich standardmäßig nach jedem `kwin`-Update selbst neu
(pacman-Hook in `/usr/share/libalpm/hooks/`, Quellcode in `/usr/src`).
`KID_AUTOREBUILD=0 makepkg` erzeugt stattdessen ein fest an die aktuelle
KWin-Version gebundenes Paket (`depends=(kwin=6.8.1)`), das ein `pacman -Syu`
mit neuerem KWin blockiert, bis es neu gebaut wurde.

Manjaro: Stable hat derzeit KWin 6.7.4 und ist damit zu alt.

### Fedora

Fedora 43–45 liefern derzeit KWin 6.7.5; KWin 6.8.1 ist für Fedora 45
eingereicht. Rawhide hat 6.8.1.

```sh
./install.sh
```

Pakete: `gcc-c++ cmake ninja-build gettext extra-cmake-modules kf6-rpm-macros
qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtsvg-devel
kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel
kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel kf6-kwidgetsaddons-devel
kwin-devel wayland-devel libepoxy-devel libdrm-devel vulkan-headers vulkan-loader-devel
libxkbcommon-devel`.

Automatischer Neubau: dnf5-„actions“-Plugin (Paket `libdnf5-plugin-actions`,
Datei `/etc/dnf/libdnf5-plugins/actions.d/kde-infinit-desktop.actions`).

Als RPM: `./build.sh --package` → `sudo dnf install ./dist/kde-infinit-desktop*.rpm`.

### RHEL, AlmaLinux, Rocky Linux (EPEL KDE)

KDE Plasma kommt auf Enterprise Linux aus EPEL. EPEL 10 hat derzeit KWin
6.7.5 – sobald EPEL KWin 6.8 ausliefert, funktioniert:

```sh
sudo dnf install epel-release          # AlmaLinux / Rocky (RHEL: siehe unten)
sudo crb enable                        # CodeReady Builder / CRB
./install.sh
```

Auf RHEL selbst:
`sudo subscription-manager repos --enable codeready-builder-for-rhel-10-$(arch)-rpms`
und `sudo dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm`.

Der Installer erledigt EPEL/CRB auf Alma/Rocky nach Rückfrage selbst. Der
automatische Neubau nutzt dort dnf4 (`python3-dnf-plugin-post-transaction-actions`,
`/etc/dnf/plugins/post-transaction-actions.d/kde-infinit-desktop.action`).
Die Fedora-Spec-Datei funktioniert unverändert auch für EL10.

### openSUSE Tumbleweed / Slowroll

```sh
./install.sh
```

Pakete: `gcc-c++ cmake ninja gettext-tools kf6-extra-cmake-modules qt6-base-devel
qt6-base-private-devel qt6-declarative-devel qt6-quickcontrols2-devel qt6-svg-devel
qt6-test-devel kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel
kf6-ki18n-devel kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel
kf6-kwidgetsaddons-devel kwin6-devel kf6-kwindowsystem-devel wayland-devel libepoxy-devel libdrm-devel
vulkan-headers vulkan-devel libxkbcommon-devel`; Laufzeit: `qt6-declarative-imports libQt6Svg6`.

Automatischer Neubau: zypp-Commit-Plugin
`/usr/lib/zypp/plugins/commit/kde-infinit-desktop` (startet den Neubau im
Hintergrund, sobald zypper fertig ist).

openSUSE Leap 16.0 (KWin 6.4) ist zu alt; Aeon/Kalpa sind unveränderlich.

### Gentoo

KWin 6.8 ist bei Gentoo noch als `~amd64` markiert. Die Live-Ebuild liegt in
`packaging/gentoo/` und wird über ein lokales Overlay eingebunden:

```sh
sudo mkdir -p /var/db/repos/local
sudo cp -r packaging/gentoo/* /var/db/repos/local/
sudo mkdir -p /var/db/repos/local/metadata /var/db/repos/local/profiles
echo 'masters = gentoo' | sudo tee /var/db/repos/local/metadata/layout.conf
echo 'local' | sudo tee /var/db/repos/local/profiles/repo_name
printf '[local]\nlocation = /var/db/repos/local\n' | sudo tee /etc/portage/repos.conf/local.conf
echo 'kde-misc/kde-infinit-desktop **' | sudo tee /etc/portage/package.accept_keywords/kde-infinit-desktop
# optional: aus einem lokalen Klon statt von GitHub bauen
# echo 'EGIT_OVERRIDE_REPO_RANDOMNAM3_KDE_INFINIT_DESKTOP=file:///pfad/zum/klon' | sudo tee -a /etc/portage/make.conf
sudo emerge -av kde-misc/kde-infinit-desktop
```

`kde-plasma/kwin` hat keinen Sub-Slot, deshalb kann Portage den Neubau nicht
selbst auslösen. Nach jedem KWin-Update:

```sh
sudo emerge --oneshot kde-misc/kde-infinit-desktop
```

(`./scripts/install.sh` funktioniert auf Gentoo ebenfalls, installiert dann
aber am Paketmanager vorbei; dort meldet die Anmelde-Prüfung nötige Neubauten.)

### NixOS

```nix
# configuration.nix (nixpkgs mit KWin >= 6.8, derzeit nixos-unstable)
environment.systemPackages = [
  (pkgs.callPackage /pfad/zu/kde-infinit-desktop/packaging/nix { })
];
```

Zum Testen: `nix-build packaging/nix`. Nixpkgs baut abhängige Pakete bei
jedem KWin-Update ohnehin neu – ein manueller Neubau ist unter NixOS nicht nötig.

### Weitere

- **Alpine / postmarketOS (edge):** `./scripts/install.sh` (vorher
  `apk add bash`); Neubau per apk-Commit-Hook `/etc/apk/commit_hooks.d/`.
- **Solus, Void, Mageia, OpenMandriva:** der Installer kennt die Paketnamen
  (Mageia/OpenMandriva über `cmake(…)`-Abhängigkeiten), ist dort aber
  ungetestet. Void hat derzeit KWin 6.7.5. Ohne passenden Paketmanager-Hook
  bietet die Anmelde-Prüfung den Neubau an.

---

## 6. Pakete selbst bauen

```sh
./build.sh --package                      # Paket für DIESE Distribution → ./dist/
./build.sh --container neon --package     # in einem Container (podman/docker) → ./dist/
./build.sh --container arch --package
./build.sh --container fedora --package
./build.sh --container tumbleweed --package
```

`--package` installiert mit `--deps` auch die Build-Abhängigkeiten. Die
KWin-Version steckt in Version bzw. Dateiname, z. B.
`kwin-effect-infinitdesktop_0.1.0-1+kwin6.8.1_amd64.deb`,
`kde-infinit-desktop-0.1.0-1.kwin6.8.1.fc46.x86_64.rpm`,
`kde-infinit-desktop-0.1.0+kwin6.8.1-1-x86_64.pkg.tar.zst`.

Debian/neon erzeugt vier Pakete: `kde-infinit-desktop` (Metapaket),
`kwin-effect-infinitdesktop` (Effekt + Systemeinstellungsmodul),
`kde-infinit-desktop-settings` (App) und `kde-infinit-desktop-data`.

```sh
sudo apt install ./dist/*.deb             # neon / Debian / Ubuntu
sudo dnf install ./dist/*.rpm             # Fedora / EL
sudo zypper install ./dist/*.rpm          # openSUSE
sudo pacman -U ./dist/*.pkg.tar.zst       # Arch
```

Diese Pakete sind an **eine** KWin-Version gebunden. Welche Vertriebswege
automatisch neu bauen (openSUSE Build Service, COPR, AUR, GitHub-Workflow),
beschreibt [PAKETE.md](PAKETE.md).

Ohne KWin 6.8 (z. B. Debian sid) baut `--package` nur App und Daten
(Debian-Build-Profil `pkg.kde-infinit-desktop.noeffect`, RPM
`--without effect`).

---

## 7. Manuell mit CMake bauen

```sh
B=~/.cache/kde-infinit-desktop/build/release      # außerhalb des Quellbaums
cmake -S . -B "$B" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DKDE_INSTALL_USE_QT_SYS_PATHS=ON
cmake --build "$B"
sudo cmake --install "$B"
```

`-DKDE_INSTALL_USE_QT_SYS_PATHS=ON` ist wichtig: KWin findet Effekte nur im
Qt-Plugin-Verzeichnis. Optionen: `-DBUILD_EFFECT=OFF`, `-DBUILD_KCM=OFF`,
`-DBUILD_SETTINGS_APP=OFF`, `-DBUILD_TESTING=ON`, `-DENABLE_SANITIZERS=ON`
(nur für Tests, nie in KWin laden). Deinstallieren:
`./scripts/uninstall.sh --manifest "$B"/install_manifest.txt`.
Ein solcher Build wird **nicht** automatisch neu gebaut.

---

## 8. Effekt aktivieren und deaktivieren

**Grafisch:** Systemeinstellungen → Fensterverwaltung → Arbeitsflächen-Effekte
(in älteren Versionen unter „Arbeitsbereich“) → „KDE Infinit Desktop“
ankreuzen → Anwenden. Das Zahnrad daneben öffnet die Einstellungen; zusätzlich
gibt es die App „KDE Infinit Desktop“ (`kde-infinit-desktop-settings`).

**Kommandozeile** (in der Plasma-Sitzung, als normaler Benutzer):

```sh
# dauerhaft ein-/ausschalten
kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled true  --notify
kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled false --notify

# sofort laden / entladen
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect   infinitdesktop
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect infinitdesktop

# geladen?
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded infinitdesktop
```

`qdbus6` heißt auf Fedora `qdbus-qt6`; ohne Qt-Tools geht es auch mit
`gdbus call --session --dest org.kde.KWin --object-path /Effects --method org.kde.kwin.Effects.loadEffect infinitdesktop`
oder `dbus-send --session --print-reply --dest=org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect string:infinitdesktop`.

---

## 9. Nach KWin-Updates

Mit `scripts/install.sh` installiert, passiert der Neubau **automatisch**:

| Paketmanager | Hook |
|---|---|
| pacman (Arch, CachyOS, Manjaro, EndeavourOS) | `/etc/pacman.d/hooks/kde-infinit-desktop.hook` |
| apt (neon, Debian, Ubuntu) | `/etc/apt/apt.conf.d/99kde-infinit-desktop` |
| dnf5 (Fedora) | `/etc/dnf/libdnf5-plugins/actions.d/kde-infinit-desktop.actions` |
| dnf4 (RHEL/Alma/Rocky) | `/etc/dnf/plugins/post-transaction-actions.d/kde-infinit-desktop.action` |
| zypper (openSUSE) | `/usr/lib/zypp/plugins/commit/kde-infinit-desktop` |
| apk (Alpine, postmarketOS) | `/etc/apk/commit_hooks.d/kde-infinit-desktop.post-commit` |
| alle anderen | Prüfung beim Anmelden (fragt, ob neu gebaut werden soll; Passwort über pkexec) |

Alle Hooks rufen `/usr/lib/kde-infinit-desktop/kwin-update-hook.sh` auf. Es
vergleicht `built-for-kwin` mit der installierten KWin-Version und baut nur bei
einem Unterschied neu – aus `/usr/src/kde-infinit-desktop-<version>`, ohne
Internet. Es lässt eine Paketinstallation **nie** fehlschlagen (Rückgabewert
immer 0), protokolliert nach `/var/log/kde-infinit-desktop-rebuild.log` und
schickt allen angemeldeten Benutzern eine Desktop-Benachrichtigung („neu gebaut
– bitte ab- und wieder anmelden“ bzw. „Neubau fehlgeschlagen“).

Nach dem Update **einmal ab- und wieder anmelden** – die laufende KWin-Instanz
ist ja noch die alte Version.

Von Hand:

```sh
./scripts/check-kwin-update.sh                           # passt alles?
sudo /usr/lib/kde-infinit-desktop/kwin-update-hook.sh    # neu bauen, falls nötig
./scripts/install.sh --rebuild                           # aus dem Quellbaum neu bauen
./scripts/install.sh --rebuild --deps                    # ... und fehlende Abhängigkeiten nachinstallieren
```

Pakete: siehe [PAKETE.md](PAKETE.md) (OBS/COPR bauen neu, AUR baut sich
selbst neu, angeheftete Binärpakete muss man ersetzen).

---

## 10. Deinstallieren

```sh
./scripts/install.sh --uninstall
# oder, falls der Quellbaum weg ist:
/usr/lib/kde-infinit-desktop/uninstall.sh
# zusätzlich eigene Einstellungen und gespeicherte Arbeitsflächen löschen:
/usr/lib/kde-infinit-desktop/uninstall.sh --purge
```

Das entfernt alle installierten Dateien (laut `install_manifest.txt`), die
Hooks, die Anmelde-Prüfung, die Quellkopie in `/usr/src` und deaktiviert den
Effekt. Pakete entfernt man mit dem Paketmanager (`sudo apt remove
kwin-effect-infinitdesktop kde-infinit-desktop-settings kde-infinit-desktop-data`,
`sudo dnf remove kde-infinit-desktop`, `sudo pacman -R kde-infinit-desktop` …).

Benutzerdaten: Einstellungen in `~/.config/kwinrc` (Gruppe
`[Effect-infinitdesktop]`), Zustand in `~/.local/state/kde-infinit-desktop/`.

---

## 11. Fehlerbehebung

**Der Effekt fehlt in der Liste oder lädt nicht**

```sh
./scripts/check-kwin-update.sh            # gebaut für KWin X, installiert ist Y?
kwin_wayland --version
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded infinitdesktop
ls "$(qtpaths6 --query QT_INSTALL_PLUGINS)"/kwin/effects/plugins/ | grep infinit
```

- Passt die Version nicht: neu bauen (siehe [Abschnitt 9](#9-nach-kwin-updates)) und neu anmelden.
- Liegt die `.so` nicht im Qt-Plugin-Verzeichnis: mit
  `-DKDE_INSTALL_USE_QT_SYS_PATHS=ON` neu bauen.
- Läuft eine X11-Sitzung: unter „Plasma (Wayland)“ anmelden
  (`echo $XDG_SESSION_TYPE` zeigt die Sitzungsart).

**Protokoll**

```sh
journalctl --user -b -g infinitdesktop
journalctl --user -b -u plasma-kwin_wayland        # KWin allgemein
```

Für ausführliche Meldungen in den Einstellungen unter „Erweitert“
„Debug-Protokollierung“ einschalten (oder mit `kdebugsettings` die Kategorie
`kwin_effect_infinitdesktop` auf „Alle“ stellen).

**Sicherheitsmodus**

Stürzt KWin mehrmals hintereinander ab, während der Effekt aktiv ist, startet
er im Sicherheitsmodus und bleibt inaktiv, damit der Desktop benutzbar bleibt.

```sh
# Ist der Sicherheitsmodus aktiv?
qdbus6 org.kde.KWin /InfinitDesktop org.freedesktop.DBus.Properties.Get org.kde.InfinitDesktop safeMode
# Sicherheitsmodus verlassen
qdbus6 org.kde.KWin /InfinitDesktop org.kde.InfinitDesktop.leaveSafeMode
# Absturzzähler zurücksetzen
rm -f ~/.local/state/kde-infinit-desktop/crashguard
```

**Der Desktop ist nicht mehr bedienbar**

Auf eine Textkonsole wechseln (Strg+Alt+F3), anmelden und den Effekt abschalten:

```sh
kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled false
```

Danach wieder zur grafischen Sitzung (meist Strg+Alt+F1 oder F2) und neu
anmelden.

**Automatischer Neubau**

- Protokoll: `/var/log/kde-infinit-desktop-rebuild.log`
- letzter Stand: `/var/lib/kde-infinit-desktop/last-rebuild`
- Schlägt er wegen fehlender Abhängigkeiten fehl (z. B. weil eine neue KWin-Version
  zusätzliche Entwicklerpakete braucht): `./scripts/install.sh --rebuild --deps`.

**Build-Fehler „Could not find a package configuration file provided by "Vulkan"“
o. Ä.** – die Pakete aus [Abschnitt 5](#5-anleitungen-je-distribution) fehlen;
KWins CMake-Konfiguration verlangt Vulkan-, Wayland-, epoxy-, libdrm- und
Qt-Quick-Entwicklerdateien, die die KWin-Entwicklerpakete nicht immer mitbringen.

---

## 12. Für Entwickler

```sh
./build.sh                       # konfigurieren, bauen, Unit-Tests (~/.cache/kde-infinit-desktop/build/debug)
./build.sh --sanitize            # ASan + UBSan (…/build/asan)
./build.sh --install             # bauen + installieren + aktivieren
./build.sh --container neon      # in Containern bauen und testen
scripts/dev/container-build.sh all              # alle Distributionen
scripts/dev/container-build.sh --sanitizers arch
```

`scripts/dev/container-build.sh` baut einmalig Abhängigkeits-Images
(`localhost/kid-dev-<distro>`, per `scripts/install.sh --deps-only`, prüft also
auch die Paketlisten des Installers) und baut dann mit schreibgeschützt
eingebundenem Quellcode in benannten Volumes (`kid-dev-build-<distro>`).
Es läuft unter Linux und in Git Bash unter Windows (podman/docker).

CI (`.github/workflows/ci.yml`): Build + Tests auf Arch, KDE neon, openSUSE
Tumbleweed, Fedora (latest + Rawhide), Debian sid; Sanitizer-Build;
`.deb`-Build auf neon; shellcheck. Der Effekt wird nur gebaut, wo KWin ≥ 6.8
vorhanden ist. `.github/workflows/packages.yml` baut täglich Pakete für neue
KWin-Versionen ([PAKETE.md](PAKETE.md)).
