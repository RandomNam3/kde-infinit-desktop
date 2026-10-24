# KDE Infinit Desktop – Technische Dokumentation

Dieses Dokument beschreibt Aufbau, Datenmodell und die wichtigsten Designentscheidungen. Es richtet sich an
Entwickler, Paketierer und alle, die Fehler analysieren wollen. Zielplattform ist **KWin 6.8** (Plasma 6.8,
Wayland); alle Aussagen zur KWin-API beziehen sich auf den Quellcode von KWin 6.8.1.

---

## 1. Überblick

```
┌──────────────────────────── kwin_wayland (ein Prozess) ─────────────────────────────┐
│                                                                                     │
│   KWin-Kern ──signals──▶ InfinitDesktopEffect (KWin::Effect)                        │
│      ▲  │                   │  Fensterverwaltung, Kamera, Commit/Parken,            │
│      │  │                   │  Zeichnen (paintWindow/paintScreen), Tastenkürzel     │
│      │  │                   ├── CanvasInputFilter (KWin::InputEventFilter)          │
│      │  │                   │     Maus, Mausrad, Touchpad-Gesten, Touch, Tastatur   │
│      │  │                   ├── CanvasRenderer     Punktraster, Symbol-Kacheln (GL) │
│      │  │                   ├── LauncherPositioner Startmenü an Mausposition        │
│      │  │                   ├── CrashGuard         abgesicherter Modus              │
│      │  │                   ├── StateStore         JSON-Dateien im State-Verzeichnis│
│      │  │                   └── DBusInterface      org.kde.InfinitDesktop           │
│      │  ▼                                                                           │
│   Window::move()  …  Szene/Renderer  …  Eingabe-Filterkette                         │
└─────────────────────────────────────────────────────────────────────────────────────┘
        ▲ D-Bus (reconfigureEffect, org.kde.InfinitDesktop)            ▲ kwinrc
        │                                                              │
 kde-infinit-desktop-settings (Qt Quick, eigener Prozess)    System-Settings-Modul (KCM)
```

| Verzeichnis | Inhalt | Abhängigkeiten |
|---|---|---|
| `src/core/` | reine Logik: Kamera, Animationen, Geschwindigkeitsmessung, Park-Positionen, Navigation, Fensterplatz-Gedächtnis | nur Qt Core – vollständig unit-getestet |
| `src/shared/` | Einstellungs-Schema (`infinitdesktop.kcfg`), UI-Metadaten, Tastenkürzel-Tabelle, D-Bus-XML | KConfig, KI18n |
| `src/effect/` | das KWin-Plugin | KWin::kwin (+ Kern, Shared) |
| `src/kcm/` | Modul für die Systemeinstellungen | KCMUtils, XmlGui |
| `src/app/` | Einstellungs-App (Qt Quick, eigenes Design) | Qt Quick, KConfig, KGlobalAccel |
| `tests/` | Unit-Tests (QtTest) | |
| `tools/demo/` | interaktive Plasma-Sitzung im Container (RDP) | Podman/Docker |

### Warum C++ als KWin-Effect?

- Nur ein Effect darf die Darstellung von Fenstern transformieren (Verschieben, Skalieren, Zuschneiden
  pro Bildschirm). Nur C++-Code im KWin-Prozess darf Eingabefilter installieren, die vor KWins eigenen
  Gesten und vor dem Fenster-Verschieben laufen. KWin-Skripte (JS/QML) können beides nicht.
- Rust bringt hier keinen Gewinn: Die riskante Stelle ist die Grenze zu KWin-Objekten, deren Lebensdauer
  KWin bestimmt. Eine C++-Zwischenschicht wäre ohnehin nötig. Ein Segfault im Compositor lässt sich in
  keiner Sprache „abfangen“ (siehe [Stabilität](#9-stabilität)).
- KWin-Plugins haben **keine stabile ABI**. Die Plugin-IID enthält `KWIN_PLUGIN_VERSION_STRING`
  (`major.minor.patch`), KWin lädt also nur Plugins, die gegen genau seine Version gebaut wurden. Ein
  Versionskonflikt führt nicht zum Absturz, das Plugin wird nur nicht geladen.

## 2. Datenmodell

### Canvas

Eine **Canvas** gehört zu genau einem Bildschirm (Ausgang) und – wenn eingeschaltet – einem virtuellen
Desktop. Schlüssel: `"<Ausgangsname>|<Desktop-ID>"` bzw. `"<Ausgangsname>|*"`.

```cpp
struct Canvas {
    Camera camera;        // was gerade angezeigt wird
    Camera committed;     // wozu die physischen Fensterpositionen passen
    CameraMotion motion;  // Übergangs- oder Gleitanimation (zeitbasiert)
    bool navigation;      // Zoom ≠ 100 %: alle Fenster geparkt, Eingabe macht das Plugin
    int interactions;     // laufende Gesten → nicht committen
    …
};
```

Canvases liegen in einem `std::map<QString, std::unique_ptr<Canvas>>` (`CanvasStore`). Ihre Adressen
bleiben stabil, Zeiger auf Canvases werden durch neue Einträge nicht ungültig. Ein `QHash` hätte genau
diesen Use-after-free-Fehler erzeugt.

### Kamera und Abbildung

```
Kamera = (center, zoom)        center: Punkt der Fläche in der Bildschirmmitte
Bildschirm(p) = Ausgang.center + (p − center) · zoom
Fläche(s)     = center + (s − Ausgang.center) / zoom
```

Fensterpositionen auf der Fläche sind in logischen Pixeln bei 100 % gespeichert (`ManagedWindow::canvasPos`
= linke obere Ecke des Rahmens). Zoomen um einen Ankerpunkt hält den Flächenpunkt unter dem Anker fest
(`Camera::zoomedAt`). Kamerafahrten interpolieren den Zoom logarithmisch.

### Fenster-Modi

| Modus | Bedingung | Verhalten |
|---|---|---|
| `Canvas` | normales Fenster | bewegt sich mit der Fläche |
| `Pinned` | angepinnt, auf allen Desktops, „immer im Vordergrund“ (optional), nicht verschiebbar | fest auf dem Bildschirm |
| `Fullscreen` | Vollbild | fest, sperrt den Bildschirm solange fokussiert |
| `Tile` | minimiertes Vollbild-Fenster | Kachel auf der Fläche |
| `Moving` | KWin verschiebt/skaliert interaktiv | wird nicht angefasst |
| `Minimized`, `Hidden` | minimiert / anderer Desktop | wird nicht gezeichnet |

Verwaltet werden nur normale Fenster, Dialoge und Werkzeugfenster. Nie verwaltet werden: Desktop,
Panels, Benachrichtigungen, OSDs, Applet-Popups, Menüs/Tooltips/Popups (die folgen ihrem Elternfenster),
Sperrbildschirm, Eingabemethoden, **Layer-Shell-Flächen** (KWin meldet sie mit beliebigem Namensraum als
„normal“, deshalb wird `KWin::LayerShellV1Window` explizit ausgeschlossen) und eine konfigurierbare Liste
von Fensterklassen.

## 3. Physisch und visuell: das Hybridmodell

Ein KWin-Effect kann Fenster nur **zeichnen**, wo er will. Eingaben liefert KWin an die **physische**
Position. Deshalb gibt es zwei Ebenen:

1. **Bei 100 % und in Ruhe** stimmt die physische Position mit der sichtbaren überein. Das Plugin
   verschiebt Fenster wirklich (`Window::move`), alles ist normal bedienbar, und das Plugin muss nichts
   zeichnen.
2. **Während einer Bewegung** (Ziehen, Wischen, Gleiten, Kamerafahrt) wird nur die Darstellung verschoben.
   Fenster werden nicht bei jedem Frame bewegt, also kein Konfigurations-Sturm an X11-Clients und keine
   Geometrie-Flut an plasmashell. Am Ende der Bewegung folgt ein **Commit**: Alle Fenster der Canvas
   springen physisch an ihre sichtbare Position. Weil Darstellung und Position dann übereinstimmen,
   ist das unsichtbar.

Ein Commit passiert außerdem **vor jedem Mausklick**, falls die Canvas gerade nicht festgeschrieben ist.
Der Filter läuft vor KWins Klickverarbeitung, und `PointerInputRedirection::update()` aktualisiert sofort
den Fokus. Ein Klick trifft deshalb immer das Fenster, das man sieht.

### Parken

Fenster, die nicht im Bild sind, werden **geparkt**: an eine Stelle außerhalb aller Bildschirme
(`computeParkingSpot`). Die Stelle erfüllt zwei Bedingungen:

- Sie schneidet keinen Bildschirm. Das Fenster kann keine Eingaben abfangen.
- Ihr Mittelpunkt liegt nach KWins Metrik (`Workspace::outputAt`, quadratischer Abstand zum
  geklemmten Punkt) am **nächsten am eigenen Bildschirm**. Das ist aus zwei Gründen nötig:
  - KWin schickt Frame-Callbacks nur an Items, deren Mittelpunkt dem gerade gezeichneten Ausgang
    zugeordnet ist (`Item::collectItems`). Ein teilweise sichtbares geparktes Fenster bleibt so live.
  - `Window::output()`, Taskleisten-Filter („nur Fenster dieses Bildschirms“) und die Auto-Pause von
    Wallpaper-Engines sehen das Fenster beim richtigen Bildschirm bzw. als nicht verdeckend.

Unsichtbare Fenster liegen nicht im Sichtbereich. KWin meldet sie dann als `suspended`, und Anwendungen wie
Browser oder GTK4/Qt-Apps stellen das Zeichnen ein.

### Fenster, die über einen Bildschirmrand ragen

Ragt ein Fenster von Bildschirm A in den Nachbarbildschirm B, bleibt es physisch und damit auf A voll
bedienbar. Auf B wird es nicht gezeichnet (`Skip`) und als durchscheinend markiert, damit KWins
Verdeckungs-Optimierung darunterliegende Fenster nicht weglässt. Erst wenn der Zeiger auf B über dem
unsichtbaren Teil erscheint, wird das Fenster geparkt (*lazy overlap resolution*). Auf A bleibt es dann
als Vorschau sichtbar, ein Klick holt es ganz ins Bild.

### Navigationsmodus (Zoom ≠ 100 %)

Sobald der Zoom (oder das Zoom-Ziel einer Animation) nicht 100 % ist, parkt das Plugin **alle**
Canvas-Fenster des Bildschirms und übernimmt die Eingabe. KWin sieht dann nur noch Panels,
Benachrichtigungen, angepinnte Fenster und den Desktop, und die bleiben normal bedienbar. Das Plugin
zeichnet die Fenster verkleinert und live. Erreicht der Zoom wieder genau 100 % und läuft keine Geste
mehr, wird der Navigationsmodus verlassen und committet.

### KWin-Rettungsaktionen

`Workspace::updateClientArea()` ruft `checkWorkspacePosition()` für alle Fenster auf, z.B. wenn sich
Panels, Monitore oder die Auflösung ändern. Dabei holt KWin Fenster außerhalb des Bildschirms teilweise
zurück. Das Plugin erkennt diese Phase an `Workspace::inRearrange()`, ignoriert die Änderungen und wendet
danach sein eigenes Layout neu an. Fenster eines entfernten Bildschirms übernehmen die Position, an die KWin
sie gesetzt hat.

Alle anderen Geometrie-Änderungen, die nicht vom Plugin kommen (Benutzer verschiebt, App verschiebt sich,
Maximieren, Kacheln), werden in Flächenkoordinaten übernommen. Grundlage ist die *festgeschriebene*
Kamera, denn zu ihr passen die physischen Positionen.

## 4. Zeichnen

Pro Ausgang und Frame:

1. `prePaintScreen`: Animationen zeitbasiert fortschreiben (`CameraMotion::sample(now)` – unabhängig
   davon, wie oft oder von wie vielen Ansichten gezeichnet wird). Ist ein Fenster transformiert, wird
   `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` gesetzt.
2. `decide(window, output)` liefert `Default`, `Skip` oder `Transform(scale, translation)`.
   - KWin skaliert um die Fensterposition und verschiebt dann (`translate(pos) · data.toMatrix()`).
     Daher gilt `translation = sichtbare Position − physische Position` und `scale = zoom`.
   - Popups, Menüs und Tooltips bekommen die Transformation ihres Hauptfensters, relativ versetzt.
   - Wayland-Popups (xdg-popup) platziert das Plugin selbst neu (`visualPopupPosition`). KWin hält ein
     Popup im Arbeitsbereich rund um die *physische* Position des Elternfensters. Bei einem geparkten
     Fenster liegt die weit weg, Kontextmenüs würden daneben aufgehen. Das Plugin merkt sich die
     `XdgPopupInterface`s (`XdgShellInterface::popupCreated`). Es rechnet die Platzierung des
     Positioners neu, mit dem gezeichneten Elternfenster und dem Arbeitsbereich in dessen
     Koordinaten (`(Arbeitsbereich − Client-Ecke) / zoom`). Untermenüs hängen an ihrem
     gezeichneten Elternmenü. X11-Menüs behalten den relativen Versatz.
3. `prePaintWindow`: `setTransformed()` bzw. für übersprungene Fenster `setTranslucent()`.
4. `paintWindow`: die Transformation in `WindowPaintData` eintragen. Direkt über dem Hintergrund (oder
   unter dem ersten Fenster, falls es kein Desktop-Fenster gibt) zeichnet `CanvasRenderer` die
   Canvas-Ebene, also Punktraster und Symbol-Kacheln. Geschnitten wird dabei auf die Reparatur-Region
   des Bildschirms.

**Zuschnitt verkleinerter Fenster:** Mit unendlicher Zeichenregion (generisches Zeichnen) schneidet KWin
die Quads eines Fensters in Software am Bildschirm zu (`clipQuads`). Dabei nutzt es nur die Verschiebung
der Fenstertransformation, nicht die Skalierung. Ein herausgezoomtes Fenster würde deshalb dort
abgeschnitten, wo es *unverkleinert* den Bildschirm verlassen würde, also rechts und unten. `paintWindow`
übergibt für transformierte Fenster deshalb eine endliche Region (den Bildschirm) und `PAINT_WINDOW_TRANSFORMED`.
KWin schneidet dann mit dem Scissor-Test zu, der die Skalierung berücksichtigt. Szenario 1 prüft das per
Screenshot.

**Scharfe Inhalte beim Herauszoomen:** 350 ms nachdem sich der Zoom beruhigt hat, setzt `applyContentScales()`
für jedes xdg-Toplevel der Fläche `Window::setNextTargetScale(Ausgabeskalierung × Zoom)`.
- Die Werte sind in 1/24-Schritten quantisiert, Minimum 0,25. KWin schickt daraufhin eine neue
  Konfiguration mit der bevorzugten Bruchteilsskalierung (`wp_fractional_scale`).
- Programme, die das unterstützen, zeichnen ihren Puffer in der verkleinerten Auflösung. Text wird damit
  mit Hinting in der kleinen Größe gesetzt, statt dass die GPU ihn herunterrechnet. Das Layout bleibt
  gleich, die Eingabe-Transformation also auch.
- Beim Verlassen des Navigationsmodus wird die Ausgabeskalierung sofort zurückgesetzt. KWin rundet die
  Fenstergröße auf das Pixelraster der kleineren Skala; deshalb stellt das Plugin die ursprüngliche
  Größe wieder her (Abweichung höchstens 3 px, nicht bei maximierten Fenstern).
- X11-Fenster bleiben unverändert; bei ihnen würde nur die Dekoration unscharf.

**Geparkte Fenster werden nie übernommen:** KWin „rettet“ Fenster, die ganz außerhalb aller Bildschirme
liegen, sobald es sie neu konfiguriert. Das passiert zum Beispiel bei einer neuen Größe oder einer neuen
Zielskalierung. Eine solche Position zu übernehmen, würde das Fenster über die Fläche springen lassen.
Geometrieänderungen geparkter Fenster führen deshalb immer zu erneutem Parken an der alten Flächenposition.
Verschiebt der Benutzer ein Fenster, ist das eine interaktive Bewegung und wird getrennt behandelt.

**Markierungen für Fenster außerhalb des Bildes:** `offscreenMarkers()` legt für jedes Flächenfenster
außerhalb des Arbeitsbereichs eine Markierung an:
- Sie sitzt auf der Linie von der Bildschirmmitte zum Fenster, dort wo diese das um 30 px eingerückte
  Arbeitsbereichsrechteck verlässt. Überlappende Markierungen werden entlang der Kante auseinandergeschoben.
- `CanvasRenderer::paintMarkers` zeichnet sie nach allen Fenstern als Textur (Kreis mit Programmsymbol)
  plus Pfeil. Ein- und ausgeblendet wird wie beim Punktraster, mit 2 s Nachlauf.
- Ein Klick (vor allen anderen Treffertests) ruft `activateAndReveal` mit der aktuellen Zoomstufe auf.

**Effekt-Kette:** Position 5, sehr früh. Blur/Better Blur, abgerundete Ecken, Transparenz usw. sehen
dadurch bereits die endgültige Transformation in `drawWindow` und arbeiten korrekt mit verschobenen und
gezoomten Fenstern.

**Schließ-Animationen anderer Effekte:** Effekte wie Skalieren oder Ausblenden halten ein geschlossenes
Fenster am Leben, bis ihre Animation gelaufen ist. Die Animation läuft aber nur weiter, wenn KWin ein Bild
zeichnet, und die Effekte fordern Neuzeichnen dort an, wo das Fenster *physisch* liegt. Bei einem geparkten
Fenster ist das außerhalb aller Bildschirme. Ohne Gegenmaßnahme würde dort nie gezeichnet, und das Fenster
bliebe samt Texturen für immer im Speicher. `repaintClosingWindows()` fordert deshalb für geschlossene,
geparkte Fenster ein Neuzeichnen an der *sichtbaren* Stelle an. Liegt die ebenfalls außerhalb, genügt ein
einzelnes Pixel, damit die Animation zu Ende läuft (längstens 10 s).

**Direct Scanout:** `blocksDirectScanout()` liefert `false`. Transformierte Fenster werden über
`KWin::ItemEffect` markiert. KWin legt nur diese Items nicht auf Overlay-Planes und kann alles andere
(Vollbild-Spiele, Videos) weiterhin direkt ausgeben. In Ruhe ist der Effect komplett aus der Kette
(`isActive() == false`).

**Hintergrund:** Desktop-Fenster und Layer-Shell-Hintergründe werden nie transformiert. Wallpaper-Plugins
(Wallpaper Engine, waywallen) bleiben statisch.

## 5. Eingabe

`CanvasInputFilter` hat das Gewicht `InputFilterOrder::GlobalShortcut`. Damit läuft er **nach**
Sperrbildschirm, Bildschirmkanten, Drag & Drop, Fensterauswahl und Alt+Tab. Er läuft **vor** KWins
globalen Gesten und Kürzeln, vor anderen Effects (Overview, …) und vor dem interaktiven
Verschieben/Skalieren.

| Ereignis | Verhalten |
|---|---|
| Linke/mittlere Taste auf leerer Fläche | Der Druck geht an den Desktop durch. Erst ab 6 px Bewegung wird ein Pan daraus (Bewegungen ab dann verbraucht, Loslassen geht wieder durch). Klicks auf Desktop-Symbole und Kontextmenü bleiben erhalten. |
| Modifier (Standard Meta+Alt) + linke Taste | Pan überall |
| Mausrad / Zwei-Finger-Scrollen | unterschieden über `PointerAxisSource`: Rad → Zoom, Finger → Pan; „Axis stop“ (Delta 0) startet das Nachgleiten |
| Wisch-/Pinch-Gesten | werden vor KWins 3-Finger-Desktop-Wechsel abgegriffen (konfigurierbar) |
| Navigationsmodus | Treffertest über die *sichtbaren* Rechtecke in Stapelreihenfolge; Panels usw. (`Foreign`) bleiben bei KWin |
| Navigationsmodus: Rand/Ecke (8 px) eines Fensters, oder Meta + rechte Taste | Größe ändern (`DragKind::Resize`). Das Plugin ruft `Window::resize()` am geparkten Fenster auf. Die Gegenecke ist in `ManagedWindow::resizeAnchor` festgehalten, und `slotFrameGeometryChanged` setzt die Flächenposition passend zur Größe, die der Client tatsächlich wählt (Mindestgröße, Zellraster). Den Größen-Mauszeiger zeigt KWin nur während einer Maus-Interception; das Plugin startet sie deshalb nur, solange der Zeiger über einer Randzone steht. |
| Tastatur | Esc/Enter im Navigationsmodus; Super-Taste allein bzw. Alt+F1 merkt die Mausposition für das Startmenü |

**Herausgezoomt benutzen:** KWin stellt die Zeigerfokussierung im Navigationsmodus auf das, was *physisch*
unter dem Zeiger liegt, also meist den Desktop, denn die Fenster sind geparkt. Das Plugin gibt den Zeiger
deshalb selbst an das Fenster, das *sichtbar* darunter liegt: `SeatInterface::notifyPointerEnter(surface,
position, transformation)` mit einer Matrix von der globalen Zeigerposition auf die Oberfläche des
Fensters (Verschiebung zur sichtbaren Position, Skalierung 1/Zoom, Versatz der Puffergeometrie).
- Bewegung, Tasten und Mausrad gehen per `notifyPointerMotion/Button/Axis` an den Seat, und das Ereignis
  wird verbraucht.
- Bei jeder Bewegung wird die Matrix neu gesetzt, weil sich die Kamera bewegen kann. Solange eine Taste
  gedrückt ist, behält das Fenster den Zeiger (impliziter Grab).
- Das funktioniert, weil KWin seinen eigenen Fokus *vor* den Filtern aktualisiert
  (`PointerInputRedirection::update()`) und den Seat nur bei einem Wechsel anfasst. Verlässt der Zeiger
  das Fenster, bekommt KWins Fokus den Zeiger zurück.
- Popups und Dialoge werden über ihre sichtbaren Rechtecke getroffen (`zoomedTargetAt`). Der Filter
  läuft vor KWins `Popup`-Filter, ein Klick in ein verkleinertes Menü schließt es also nicht.
- Die serverseitige Titelleiste gehört dem Plugin: Ziehen verschiebt auf der Fläche, ein Doppelklick führt
  zu 100 %.
- Startet ein Programm mit eigener Titelleiste (GTK, Browser, Steam, Wine/Proton) selbst ein Verschieben
  (`xdg_toplevel.move`, bei X11 `_NET_WM_MOVERESIZE`), übernimmt das Plugin schon beim Start
  (`windowStartUserMovedResized`): Ab da verschiebt es das Fenster auf der Fläche, und der Client bekommt
  sein Loslassen sofort. KWins Verschieben wird abgebrochen (`cancelInteractiveMoveResize`), sobald KWin
  es fertig gestartet hat (`QTimer::singleShot(0)`). Bis dahin bleibt das Fenster ein Flächenfenster
  (`ManagedWindow::moveTakenOver`). Sonst würde KWin das geparkte Fenster in Originalgröße unter den Zeiger
  holen, und das Ende des Verschiebens würde seine Flächenposition überschreiben.
- Bei X11-Clients setzt KWin den Zeiger vor dem Start dorthin, wo der Client ihn vermutet: auf das geparkte
  Fenster, also an den Bildschirmrand. Solange eine Taste auf einem verkleinerten Fenster gedrückt ist,
  geht ein solcher Warp nicht an den Client. Das Plugin rechnet mit der Position davor und setzt den
  Zeiger dorthin zurück.
- Die Tastatur folgt dem aktiven Fenster; ein Klick aktiviert es.
- Noch nicht unterstützt: Drag & Drop zwischen verkleinerten Fenstern (KWins DnD-Filter läuft vorher und
  arbeitet mit physischen Positionen) und Touch.

**Nachgleiten:** Nur ein *Wisch* gleitet nach (`VelocityTracker::flingVelocity`). Dafür muss die
Geschwindigkeit mindestens 250 px/s betragen. In den letzten 150 ms muss die Strecke mindestens 80 % des
zurückgelegten Wegs ausmachen und zur Geschwindigkeit passen (35°). Außerdem müssen die letzten 100 ms in
dieselbe Richtung zeigen wie die 150 ms davor (30°). Kreisbewegungen vor dem Loslassen gleiten deshalb
nicht.

**Bildschirmwechsel per Wurf:** Während KWin ein Fenster interaktiv verschiebt, prüft der Filter jede
Bewegung, die den Ausgang wechseln würde. Unter der Schwelle (Standard 1600 px/s) wird das Ereignis
verbraucht und der Zeiger an die Kante zurückgesetzt (`InputRedirection::warpPointer`; der Warp läuft
über KWins `PositionUpdateBlocker` erst nach dem aktuellen Ereignis). Das Kantenscrollen bewegt dann die
Fläche.

Die Geschwindigkeit ist die der *Hand*, nicht die des zurückgesetzten Zeigers:
- Relative Geräte (Maus): Summe der Deltas.
- Absolute Geräte (Tablet, Remote-Desktop, VMs): melden kein Delta, deshalb zählt dort die Rohposition.

Ausgewertet wird per Kleinste-Quadrate-Regression über die letzten 90 ms (`VelocityTracker`).

## 6. Vollbild und Kacheln

- Vollbild-Fenster sind nie Canvas-Fenster. `isLocked(output)` ist wahr, solange das aktive Fenster ein
  nicht minimiertes Vollbild-Fenster auf diesem Ausgang ist. Dann sind alle Gesten und Kürzel für den
  Ausgang wirkungslos.
- Minimiert → Modus `Tile`. Die Kachel sucht sich freien Platz (`findFreeTileSpot`, meidet Fenster und
  andere Kacheln).
  - **Live:** `EffectWindowVisibleRef(PAINT_DISABLED_BY_MINIMIZE)` hält das Fenster sichtbar. Es wird auf
    die Kachelgröße transformiert.
  - **Symbol:** Das Fenster bleibt unsichtbar und ist damit `suspended`, spart also Leistung. Gezeichnet
    wird eine per QPainter erzeugte Textur (Symbol, Titel), die nur bei deutlicher Größenänderung neu
    erzeugt wird.
- Klick auf die Kachel: `unminimize()` und aktivieren, ggf. vorher `sendToOutput()` auf den Bildschirm,
  auf den die Kachel gezogen wurde.

## 7. Fensterplatz-Gedächtnis

`PlacementMemory` (in `src/core`, unit-getestet) speichert pro Anwendung (Desktop-Dateiname, sonst
Fensterklasse) bis zu 8 Plätze, für höchstens 300 Anwendungen.

Ein Platz umfasst:
- Bildschirm-Identität (Anschluss **und** EDID: Hersteller, Modell, Seriennummer)
- Flächenposition und Bildschirmposition
- Größe
- maximiert, angepinnt, immer im Vordergrund

Mehrere Fenster einer Anwendung verhalten sich wie ein Stapel: Ein neues Fenster nimmt den jüngsten freien
Platz, ein schließendes Fenster ersetzt den Platz, den es genommen hatte.

Bildschirmzuordnung:

| Punktzahl | Bedeutung |
|---|---|
| 4 | gleicher Monitor, gleicher Anschluss |
| 3 | gleicher Monitor, anderer Anschluss |
| 1 | gleicher Anschluss ohne vergleichbare EDID |
| 0 | anderer Monitor, dann wird der aktuelle Bildschirm genommen |

Dateien werden beim Laden validiert: unendliche oder riesige Werte werden verworfen.

## 8. Persistenz und Konfiguration

| Datei | Inhalt |
|---|---|
| `~/.config/kwinrc` `[Effect-infinitdesktop]` | Einstellungen (Schema: `src/shared/infinitdesktop.kcfg`) |
| `~/.config/kwinrc` `[Plugins] infinitdesktopEnabled` | an/aus – KWin beobachtet die Datei und lädt live |
| `$XDG_STATE_HOME/kde-infinit-desktop/canvas.json` | Kameras und Lesezeichen je Canvas (nie gezoomt wiederhergestellt) |
| `$XDG_STATE_HOME/kde-infinit-desktop/placements.json` | Fensterplatz-Gedächtnis |
| `$XDG_STATE_HOME/kde-infinit-desktop/crashguard` | Zähler des Crash-Guards |
| `/usr/share/kde-infinit-desktop/built-for-kwin` | KWin-Version, gegen die gebaut wurde (Neubau-Erkennung) |

Einstellungen wirken live:
- Die App speichert und ruft `org.kde.kwin.Effects.reconfigureEffect("infinitdesktop")` auf.
- Zusätzlich beobachtet der Effect `kwinrc` mit `KConfigWatcher` (z.B. für
  `kwriteconfig6 --notify`).

## 9. Stabilität

Ein Absturz im Plugin ist ein Absturz von KWin. In keiner Sprache lässt sich ein Segfault im Prozess
sicher abfangen. Die Strategie hat deshalb drei Stufen.

**Vermeiden:**
- Logik im KWin-freien Kern mit Unit-Tests, Build-Option `ENABLE_SANITIZERS` (ASan/UBSan).
- Keine Exceptions (wie KWin selbst).
- Keine besitzenden Rohzeiger: `QPointer` für KWin-Fenster, stabile Canvas-Adressen, Einträge erst bei
  `windowDeleted` entfernen, Gesten speichern nur Schlüssel statt Zeigern.
- Keine Fensterbewegungen während eines Zeichendurchgangs: Commits am Ende einer Animation werden per
  `QTimer::singleShot(0)` aus dem Paint-Pfad herausgelöst.
- Eingelesene Dateien werden validiert, ungültige Kameras abgelehnt.

**Eingrenzen:**
- `CrashGuard` zählt Starts. Nach 30 s stabiler Laufzeit oder sauberem Beenden wird der Zähler
  zurückgesetzt.
- Nach 3 Abstürzen in Folge startet das Plugin im **abgesicherten Modus** und bleibt inaktiv, bis der
  Nutzer ihn verlässt.

**Überstehen:**
- KWin startet nach einem Absturz neu, Qt-6-Anwendungen verbinden sich wieder.
- Wird der Effect normal entladen, holt er alle geparkten Fenster auf den Bildschirm zurück.

## 10. Leistung (AMD/Mesa, Intel, NVIDIA)

- In Ruhe inaktiv: kein Eintrag in der Effekt-Kette, keine Timer, kein zusätzliches Zeichnen.
- Bewegung nur über Transformationsmatrizen. Keine Offscreen-Puffer pro Fenster, keine eigenen Shader.
  Es werden nur KWins `ShaderManager`/`GLVertexBuffer`/`GLTexture` benutzt, damit verhält es sich auf
  allen Treibern gleich.
- Animationen sind zeitbasiert und werden mit KWins Bildwiederholrate gezeichnet.
- Unsichtbare Fenster sind geparkt bzw. `suspended`. Symbol-Kacheln lassen Spiele und Videos schlafen.
- Direct Scanout und Overlay-Planes bleiben für alles nicht Transformierte erhalten.

## 11. D-Bus-Schnittstelle

Dienst `org.kde.KWin`, Objekt `/InfinitDesktop`, Schnittstelle `org.kde.InfinitDesktop`
(`src/shared/org.kde.InfinitDesktop.xml`):

| Mitglied | Zweck |
|---|---|
| `safeMode`, `paused` (Eigenschaften), `statusChanged` | Status für die App |
| `leaveSafeMode()`, `setPaused(b)`, `reloadSettings()` | Steuerung |
| `state()` | JSON: Ausgänge, Canvases, Kameras, alle Fenster mit Modus/Position |
| `pan(dx,dy)`, `zoomBy(f)`, `setZoom(z)`, `zoomToFit()`, `resetZoom()`, `goHome()` | Navigation auf dem Bildschirm unter dem Zeiger, sofort (für Skripte und Tests) |
| `setCamera(x,y,zoom)` | Kamera direkt setzen: Mitte in Flächenkoordinaten und Zoom (für Skripte, Tests und `tools/docs-media`) |
| `windowPlacements()`, `forgetWindowPlacements()`, `forgetApplicationPlacements(app)` | Fensterplatz-Gedächtnis: lesen, alles bzw. ein Programm vergessen |

Zusätzlich: `qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.debug infinitdesktop ""` liefert ebenfalls
den Zustand.

## 12. Tests

**Unit-Tests** (`ctest`): Kamera-Mathematik, Animationen, Geschwindigkeitsmessung, Park-Positionen
(nebeneinander, übereinander, versetzt, umschlossen), Navigation, Fensterplatz-Gedächtnis, Konsistenz von
Schema, Metadaten und Kürzeltabelle.

**Laufzeit-Tests** in einer echten, virtuellen KWin-Sitzung im Container:
- `kwin_wayland --virtual`, OpenGL ES über llvmpipe auf dem vgem-Render-Node.
- Eingaben über KWins Fake-Input-Protokoll, Screenshots über `org.kde.KWin.ScreenShot2`, Zustand über
  `org.kde.InfinitDesktop.state`.

Geprüft werden:
- Pannen, Parken, Zurückholen, Zoom und Navigationsmodus.
- Mausrad-Zoom mit Einrasten bei 100 %, Ziehen auf leerer Fläche, Meta+Alt-Ziehen.
- Zwei Bildschirme: getrennte Flächen, Zuschnitt (auch in Screenshots über alle Bildschirme),
  Überhang-Auflösung, langsames Ziehen blockiert (Kantenscrollen ohne KWins Rand-Kacheln), schneller Wurf
  wechselt den Bildschirm.
- Fensterplatz-Gedächtnis, Anpinnen per Kürzel.
- Vollbild-Sperre, Live- und Symbol-Kachel, Kachel-Klick.
- Belastung: zufällige Navigation, während Fenster kommen und gehen; Clients beenden, während ihre
  Fenster geparkt sind; Klick während des Nachgleitens.
- Herausgezoomt arbeiten (Szenario 6): Ein Testfenster (`probe.qml`) meldet Klicks, Mausrad und
  Tastatureingaben mit Position. Geprüft wird, dass sie bei 50 % pixelgenau ankommen, dass ein
  Rechtsklick-Popup am Mauszeiger aufgeht und Klicks annimmt, dass die Titelleiste verschiebt (auch eine vom Programm selbst gezeichnete, unter Wayland und X11), der Doppelklick zu 100 % führt und die Inhaltsskalierung gesetzt und
  zurückgesetzt wird. Außerdem: Markierungen und ihr Klick, und kein Nachgleiten nach Kreisbewegungen.
- Verträglichkeit (`compat-session.sh`): eingebaute Unschärfe oder Better Blur DX, im Container gegen
  dieselbe KWin-Version gebaut, dazu Transparenz, Wobbly Windows und Magic Lamp. Szenario 5 prüft Pannen,
  Zoom, Ziehen und Minimieren sowie die Freigabe geparkt geschlossener Fenster. Die Szenarien 1–4 laufen
  ebenso mit diesen Effekten. Auf llvmpipe erzwingt `KWIN_EFFECTS_FORCE_ANIMATIONS=1` die Animations-Effekte,
  die KWin bei Software-Rendering sonst nicht lädt.

Alle Szenarien laufen zusätzlich mit einem **AddressSanitizer/UBSan-Build** des Plugins innerhalb von KWin
(`tests/runtime/run.sh --asan`, Sanitizer-Laufzeit per `LD_PRELOAD`). Anleitung: `tests/runtime/README.md`.

**Klick während einer Bewegung:** KWin wählt das Ziel eines Mausklicks (`PointerInputRedirection::update()`)
*bevor* die Eingabefilter laufen. Ab dem Druck ist der Fokus gesperrt (`focusUpdatesBlocked()`). Ein
Klick, während die Fläche noch gleitet oder animiert, würde deshalb das Fenster treffen, das vorher an der
Stelle lag. Ein solcher Klick stoppt nur die Bewegung und wird mitsamt Loslassen verbraucht, wie bei
Touch-Listen.

## 13. KWin-Versionen

Das Plugin wird gegen die KWin-Header gebaut, die `kwin-dev`/`kwin-devel` (Arch: im Paket `kwin`)
mitbringen. Unter Arch zusätzlich nötig: `vulkan-headers` (`KWinConfig.cmake` verlangt Vulkan).

Die API hat sich zwischen 6.x-Versionen deutlich geändert, z.B.:
- `Region` statt `QRegion`
- `LogicalOutput` statt `Output`
- `RenderView` in `prePaintWindow`
- `paintScreen` mit Rückgabewert
- Zeigerereignisse für Effects seit 6.7

Unterstützt wird daher zunächst **KWin ≥ 6.8**. Ältere Versionen bräuchten Kompatibilitätszweige.
