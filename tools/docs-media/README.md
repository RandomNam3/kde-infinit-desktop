# Bilder und Animationen der Dokumentation

Die Bilder in `docs/images` entstehen automatisch in einer echten Plasma-Sitzung. Dazu läuft ein
eigener Container aus dem Demo-Image (`tools/demo`), eine laufende Demo bleibt unberührt.

```powershell
.\tools\docs-media\record.ps1                       # alle Szenen nach docs\images
.\tools\docs-media\record.ps1 -Scenes zoom,markers  # nur einzelne Szenen
.\tools\docs-media\record.ps1 -Language en          # Sitzung auf Englisch
```

Unter Linux: `tools/docs-media/record.sh [--scenes …] [--output DIR] [--lang de|en]`.

| Szene | Ergebnis | Zeigt |
|---|---|---|
| `overview` | `overview.png` | Übersicht bei 42 %, alle Fenster live und scharf |
| `pan` | `canvas-pan.webp` | Fläche verschieben; Hintergrund und Taskleiste bleiben |
| `zoom` | `zoom-overview.webp` | Heraus- und hineinzoomen |
| `work` | `work-while-zoomed.webp` | herausgezoomt in Kate tippen, Konsole an der Titelleiste verschieben |
| `resize` | `resize-while-zoomed.webp` | herausgezoomt die Fenstergröße ändern |
| `markers` | `offscreen-markers.webp` | Markierungen am Rand, Klick fliegt zum Fenster |
| `pin` | `pin-window.webp` | angepinntes Fenster bleibt beim Verschieben stehen |
| `fullscreen` | `fullscreen-tile.png` | minimiertes Vollbild-Video als Live-Kachel |
| `notes` | `notes.png`, `note-menu.png` | eine formatierte Notiz und eine beim Schreiben, das Menü einer Notiz (Rechtsklick in den Text) |

## Wie es funktioniert

`session.sh` baut das Plugin, die Einstellungs-App und das Eingabewerkzeug `kid-input` aus dem
Projektordner. Dann startet es KWin auf einem virtuellen 1920×1080-Bildschirm, plasmashell, Kate, Konsole,
die Systemeinstellungen und die Einstellungs-App. `scenes.py` spielt die Szenen ab.

Animationen entstehen **im Stop-Motion-Verfahren**:
- **Schrittweise Kamera:** Die Kamera wird über D-Bus (`setCamera`) in kleinen Schritten bewegt.
- **Echte Eingaben:** Ein Tastendruck oder ein Stück Mausbewegung kommt über KWins
  Fake-Input-Protokoll (`kid-input -`, Zeilenmodus).
- **Bild für Bild:** Nach jedem Schritt nimmt `org.kde.KWin.ScreenShot2` ein Bild auf.

So werden die Animationen flüssig, egal wie langsam das Software-Rendering im Container ist. Was zu sehen
ist, ist das echte Verhalten des Plugins. Gespeichert wird als animiertes WebP (GitHub und Browser zeigen
es wie ein GIF, bei einem Bruchteil der Größe).
