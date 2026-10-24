# Pakete und Vertriebswege

Kurzfassung für Maintainer: Wie kommt KDE Infinit Desktop zu den Nutzern, und
wer baut es nach jedem KWin-Update neu?

## Das Grundproblem

KWin lädt nur Plugins, die für **genau seine Version** gebaut wurden. Die
Plugin-Kennung (IID) lautet `org.kde.kwin.EffectPluginFactory` +
`KWIN_PLUGIN_VERSION_STRING`, also z. B. `…EffectPluginFactory6.8.1`. Ein für
KWin 6.8.1 gebautes Plugin wird von KWin 6.8.2 kommentarlos ignoriert (kein
Absturz, der Effekt ist einfach weg). **Jedes KWin-Patch-Release braucht einen
Neubau.**

Daraus folgen zwei Strategien:

| Strategie | Wo | Verhalten beim KWin-Update |
|---|---|---|
| **Selbst neu bauen** (wie DKMS) | `scripts/install.sh`, AUR-Paket (`_autorebuild=1`) | Ein Paketmanager-Hook baut das Plugin aus `/usr/src/kde-infinit-desktop-<version>` neu. Der Nutzer merkt nichts außer „ab- und wieder anmelden“. |
| **Fest angeheftetes Binärpaket** | `.deb`, `.rpm`, Arch-Binärpaket (`KID_AUTOREBUILD=0`) | Das Paket hängt von genau der KWin-Version ab, gegen die es gebaut wurde. Jemand muss nach jedem KWin-Update ein neues Paket bauen und veröffentlichen. |

Abhängigkeiten der Binärpakete (automatisch beim Bau eingesetzt):

- Debian/Ubuntu/neon: `kwin-wayland (>= 4:6.8.1), kwin-wayland (<< 4:6.8.1.1~)` –
  passt auf jede Paketrevision von 6.8.1, aber auf kein anderes KWin.
  Version `0.1.0-1+kwin6.8.1`.
- Fedora/RHEL: `Requires: kwin(x86-64) = 6.8.1`, Release `1.kwin6.8.1.fc46`.
- openSUSE: `Requires: kwin6 = 6.8.1`, Release `1.kwin6.8.1`.
- Arch (Binärpaket): `depends=(kwin=6.8.1)`; Dateiname
  `kde-infinit-desktop-0.1.0+kwin6.8.1-1-x86_64.pkg.tar.zst`.

Was passiert, wenn KWin aktualisiert wird, das passende Paket aber noch fehlt?

| Paketmanager | Verhalten |
|---|---|
| apt | `apt upgrade` hält das KWin-Update zurück („zurückgehalten“); `apt full-upgrade` würde stattdessen den Effekt entfernen |
| dnf | überspringt das KWin-Update mit einer Meldung über das Abhängigkeitsproblem (Fedora-Standard `best=False`) |
| zypper (`dup`) | fragt nach einer Lösung (Paket entfernen oder KWin nicht aktualisieren) |
| pacman | bricht `pacman -Syu` ab („breaks dependency kwin=6.8.1“) – deshalb ist das AUR-Paket standardmäßig **nicht** angeheftet, sondern baut sich selbst neu |

## Vertriebswege im Vergleich

### 1. openSUSE Build Service (OBS) – empfohlen für Distributionspakete

- Baut aus **einer** Quelle für openSUSE Tumbleweed/Slowroll, Fedora,
  Debian, Ubuntu und Arch (und weitere); KDE neon lässt sich als
  „Download on Demand“-Repository einbinden.
- **Baut automatisch neu, wenn sich eine Build-Abhängigkeit ändert** (also
  `kwin6-devel` / `kwin-devel` / `kwin-dev`) – genau das, was wir brauchen.
  Die neue Paketversion erscheint im Repository meist wenige Stunden nach dem
  KWin-Update; apt/dnf/zypper installieren sie dann zusammen mit KWin.
- Nutzer binden einmalig ein Repository ein, danach läuft alles über die
  normalen Updates.
- Aufwand: OBS-Projekt anlegen, `packaging/opensuse/*.spec`,
  `packaging/fedora/*.spec` (über `%if 0%{?fedora}` zusammenführbar) und
  `packaging/debian/` (als `debian.tar.gz` bzw. per `obs_scm`-Service)
  hochladen, Ziel-Repositories auswählen.

### 2. Fedora COPR – gut für Fedora und EPEL

- Baut `packaging/fedora/kde-infinit-desktop.spec` für alle Fedora-Versionen
  und EPEL 10.
- **Kein automatischer Neubau bei Abhängigkeitsänderungen.** Lösung: der
  geplante GitHub-Workflow (siehe 4.) erkennt eine neue KWin-Version und löst
  per COPR-Webhook bzw. `copr-cli build-package` einen Neubau aus.
- Nutzer: `sudo dnf copr enable <user>/kde-infinit-desktop && sudo dnf install kde-infinit-desktop`.

### 3. AUR – für Arch, CachyOS, Manjaro, EndeavourOS

- `packaging/arch/PKGBUILD` (Release) und `packaging/arch-git/PKGBUILD` (git).
- Quellpaket, wird beim Nutzer gebaut, also immer passend zum installierten
  KWin. Mit `_autorebuild=1` (Standard) installiert es zusätzlich einen
  pacman-Hook, der das Plugin nach jedem `kwin`-Update aus `/usr/src` neu baut –
  `pacman -Syu` wird nie blockiert.
- Hinweis Manjaro: Stable liefert derzeit KWin 6.7.4 (zu alt), Unstable 6.8.1.

### 4. GitHub Actions + Releases (`.github/workflows/packages.yml`)

- Läuft täglich und manuell. Prüft in Containern die aktuelle KWin-Version von
  Arch, Fedora (latest), openSUSE Tumbleweed und KDE neon. Gibt es für diese
  Kombination noch kein Paket im Release `kwin-builds`, wird es mit
  `./build.sh --package` gebaut und hochgeladen (Version/Dateiname enthalten
  die KWin-Version, z. B. `kwin-effect-infinitdesktop_0.1.0-1+kwin6.8.1_amd64.deb`).
- Distributionen mit KWin < 6.8 werden automatisch übersprungen.
- Vorteil: kein externer Dienst. Nachteil: kein Repository – Nutzer laden die
  Datei von Hand herunter und bekommen keine automatischen Updates. Geeignet
  für Tester und als Auslöser für COPR/OBS.

### 5. Distributionen selbst

Langfristig das Beste (Debian/Ubuntu, Fedora, openSUSE, Arch `extra`, Gentoo,
NixOS bauen bei KWin-Updates ohnehin alles Abhängige neu). Voraussetzung:
öffentliches Repository, Release-Tarballs, stabile Versionen.

## Empfehlung

1. **Jetzt:** `scripts/install.sh` als Standardweg für Nutzer – ein Befehl,
   automatischer Neubau auf allen Distributionen mit pacman, apt, dnf, zypper
   oder apk, sonst Rückfrage beim Anmelden.
2. **Mit den Releases** (Tags `vX.Y.Z`, ab `v0.1.0`): AUR-Pakete veröffentlichen,
   OBS-Projekt für openSUSE/Fedora/Debian/Ubuntu (+ neon als DoD) einrichten,
   optional COPR; `packages.yml` für Testpakete aktiv lassen.
3. Vor dem Veröffentlichen in AUR/OBS: in `packaging/arch/PKGBUILD` die
   Prüfsumme des Release-Tarballs eintragen (`updpkgsums`) und eine echte
   Maintainer-Adresse in `packaging/debian/` (`maintainers@kde-infinit-desktop.invalid`)
   und `packaging/gentoo/…/metadata.xml` setzen.

## Neue Version veröffentlichen

Jede Version bekommt einen Tag `vX.Y.Z` und ein GitHub-Release. Der Update-Check der
Einstellungs-App liest genau diese Releases.

1. Version in `CMakeLists.txt` (`project(... VERSION X.Y.Z)`), `src/effect/metadata.json`,
   AppStream (`data/org.kde.infinitdesktop.metainfo.xml`) und den Paketen unter `packaging/`
   erhöhen.
2. Patchnotes nach `docs/releases/X.Y.Z.md` schreiben (Markdown, ohne Titelzeile). Das CI
   bricht ab, wenn die Datei zur Projektversion fehlt.
3. Nach `main` übernehmen. `.github/workflows/release.yml` legt dann den Tag auf diesem
   Commit an und veröffentlicht das Release mit den Patchnotes.

Fehlt das Release einer schon übernommenen Version, den Workflow „Release“ von Hand
starten und den Commit dieser Version angeben.
