# Laufzeit-Tests in einer echten KWin-Sitzung

Diese Tests starten KWin selbst (`kwin_wayland --virtual`, OpenGL ES per Mesa llvmpipe) in einem Container,
laden KDE Infinit Desktop, öffnen Wayland- und X11-Testfenster und steuern alles mit echten Eingaben.
Die Eingaben laufen über KWins Fake-Input-Protokoll (`kid-input`). Geprüft wird über den D-Bus-Zustand
des Plugins und über Screenshots (`org.kde.KWin.ScreenShot2`).

```bash
tests/runtime/run.sh            # alle Szenarien
tests/runtime/run.sh 2          # nur Szenario 2
tests/runtime/run.sh --asan     # Plugin mit AddressSanitizer + UBSan, Laufzeit in KWin vorgeladen
```

Unter Windows mit Podman: `.\tests\runtime\run.ps1 [-Asan] [-Scenarios 1,2]`.

| Szenario | Inhalt |
|---|---|
| 1 | Pannen (D-Bus, Ziehen auf leerer Fläche, Meta+Alt+Ziehen), Parken, Zurückholen, Zoom/Navigationsmodus, Mausrad-Zoom mit Einrasten bei 100 % |
| 2 | Zwei Bildschirme: getrennte Flächen, Zuschnitt am Rand, Überhang-Auflösung, langsames Ziehen blockiert + Kantenscrollen ohne KWin-Kacheln, schneller Wurf wechselt den Bildschirm |
| 3 | Fensterplatz-Gedächtnis (Position + Größe), Anpinnen und „immer oben“ per Kürzel, Vollbild-Sperre, Live- und Symbol-Kachel, Klick auf Kachel, dasselbe für randloses Vollbild (X11-Fenster ohne Rahmen, wie Proton-Spiele) |
| 4 | Belastung: zufällige Navigation mit kommenden/gehenden Fenstern, Prozesse beenden während ihre Fenster geparkt sind, Klick während des Nachgleitens, neues Fenster beendet den Zoom, ein Fenster außerhalb der Taskleiste (Startbildschirm eines Launchers) nicht |
| 5 | Verträglichkeit mit anderen Effekten: Unschärfe (eingebaut oder Better Blur DX), Transparenz, Wabernde Fenster, Wunderlampe. Pannen, Zoom, Ziehen, Minimieren |
| 6 | Herausgezoomt arbeiten: Klick, Mausrad und Tastatur kommen im Fenster an der richtigen Stelle an (Testfenster `probe.qml`), Kontextmenü (xdg-Popup) geht am Mauszeiger auf und nimmt Klicks an, Titelleiste verschiebt (auch eine vom Programm selbst gezeichnete wie Browser-Tabs, unter Wayland und X11, und ein X11-Fenster, das sich selbst verschiebt wie Steam), Doppelklick führt zu 100 %, scharfe Inhalte (Bruchteilskalierung). Markierungen für Fenster außerhalb des Bildes. Kein Nachgleiten nach Kreisbewegungen |
| 7 | Notizen, Gruppen, Bereiche und Karte mit der echten Begleit-App: neue Notiz an der Mausposition, Tippen in den Markdown-Text, Strg + E für formatiert und zurück, Rechtsklick-Menü, Anheften; Gruppe öffnen; Fenster in einem dynamischen Bereich; Karte und Menü der Arbeitsfläche öffnen und schließen, danach keine Overlay-Texturen mehr |

Szenario 5 startet über `compat-session.sh`. Die Datei schaltet die Effekte ein und setzt
`KWIN_EFFECTS_FORCE_ANIMATIONS=1`, denn auf llvmpipe gilt das Rendern als Software-Emulation, und KWin
würde Animations-Effekte sonst gar nicht laden. Mit `BETTER_BLUR_SRC=<Checkout>` (bzw. `-BetterBlur <Checkout>`
in `run.ps1`) wird [Better Blur DX](https://github.com/xarblu/kwin-effects-better-blur-dx) im Container gegen
dieselbe KWin-Version gebaut und statt der eingebauten Unschärfe verwendet.

Die Szenarien 1–4 lassen sich auch mit allen diesen Effekten ausführen:
`bash /th/compat-session.sh /th/scenario<N>.sh` statt `session.sh`.

Voraussetzung ist ein DRM-Render-Knoten (`/dev/dri`). Ohne GPU erzeugt `modprobe vgem` einen virtuellen. Unter
WSL2 übernimmt `run.ps1` das in der Podman-Maschine.

Hinweis: Im Container setzt `session.sh` `QT_QUICK_BACKEND=software` für KWin. Auf llvmpipe/vgem bekommen
KWins eigene Qt-Quick-Overlays (z.B. Bildschirmmeldungen) sonst keinen OpenGL-Kontext. Auf echter
Hardware ist das nicht nötig.
