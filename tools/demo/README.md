# Interaktive Demo

Startet eine vollständige **Plasma-6-Wayland-Sitzung** in einem Container: KWin mit KDE Infinit Desktop,
plasmashell (Hintergrund, Taskleiste, Startmenü), Konsole, Dolphin, Kate, die Einstellungs-App. Du öffnest
sie per Remotedesktop (KRdp). Der aktuelle Quellcode des Projekts wird beim Start im Container gebaut.

Gedacht zum Ausprobieren ohne eigenes Plasma-System und für schnelle Tests während der Entwicklung.

## Windows (Podman Desktop / podman machine)

```powershell
.\tools\demo\start-demo.ps1            # bauen (beim ersten Mal), starten, Remotedesktop öffnen
.\tools\demo\start-demo.ps1 -Stop      # beenden
.\tools\demo\start-demo.ps1 -Rebuild   # Container-Image neu bauen (z.B. nach Plasma-Updates)
.\tools\demo\start-demo.ps1 -Language en   # Sitzung auf Englisch (Standard: Deutsch, mit deutscher Tastatur)
```

Eine Anmeldung ist nicht nötig. Das Skript schreibt die Verbindungsdatei
`%TEMP%\kde-infinit-desktop-demo.rdp` und öffnet sie. Die Datei sorgt für zwei Dinge:
- Sie nutzt TLS ohne NLA. So kommt die Verbindung ohne NTLM aus, das unter Windows oft
  deaktiviert ist (Fehler „…da die NTLM-Authentifizierung deaktiviert wurde“).
- Tastenkürzel mit der Windows-Taste (Meta) gehen an Plasma.

Windows fragt beim ersten Mal nach dem unbekannten Herausgeber der Datei. Das mit „Verbinden“ bestätigen.

Weil die Sitzung keine Anmeldung braucht, ist der Port nur lokal erreichbar (`127.0.0.1`).
Mit `DEMO_PASSWORD=<Passwort>` im Container gilt stattdessen Benutzer `kde` mit Passwort (NLA).

**Vollbild testen:** In der Sitzung liegt ein Testvideo (`~/Videos/Testvideo.mp4`: bewegte Farbverläufe mit
Titel und Zeitanzeige, erzeugt von `make-testvideo.sh`).
Öffne es in Dolphin (Ordner *Videos*) oder in der Konsole mit `mpv --fs ~/Videos/Testvideo.mp4`.
Meta + Bild ab minimiert es zur Kachel auf der Fläche.

## Linux (Podman oder Docker)

```bash
tools/demo/start-demo.sh
tools/demo/start-demo.sh --stop
```

Danach mit KRDC, Remmina oder `xfreerdp /v:localhost:13389 /u:root /p:x /sec:tls /cert:ignore` verbinden.
Benutzername `root`, das Passwort ist beliebig. Es wird keine Anmeldung geprüft, der Port ist nur lokal erreichbar.

## Wie es funktioniert

- KWin läuft mit dem virtuellen Backend (`kwin_wayland --virtual`). Für OpenGL-Compositing braucht KWin
  einen DRM-Render-Knoten. Unter WSL2 stellt ihn der Kernel-Treiber `vgem` bereit (das Skript lädt ihn),
  gerendert wird per Software (Mesa llvmpipe).
- KRdp (`krdpserver --plasma`) überträgt den Bildschirm über KWins Screencast-Protokoll und gibt Maus
  und Tastatur über KWins Fake-Input-Protokoll weiter.
- Ohne Anmeldung läuft KRdp im System-Benutzer-Modus (TLS, Prüfung über den PAM-Dienst `login`), und der
  Wegwerf-Container lässt dort jeden zu. Das Sitzungsskript kommt aus dem eingebundenen Projektordner.
  Änderungen daran gelten deshalb beim nächsten Start ohne Neubau des Images.
- Diagnose:
  - `podman exec kid-demo tail -f /tmp/kwin.log`
  - `podman exec kid-demo bash -c '. /tmp/demo-env; qdbus6 org.kde.KWin /InfinitDesktop org.kde.InfinitDesktop.state'`

Einschränkungen: Touchpad-Gesten (Wischen, Pinch) überträgt RDP nicht. Software-Rendering ist langsamer
als echte Hardware.
