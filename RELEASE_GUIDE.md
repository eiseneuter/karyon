# Release-Leitfaden: Karyon 3.0 auf GitHub & im AppImage-Katalog

Dieser Leitfaden führt dich Schritt für Schritt durch den gesamten Prozess:
1. **Entwarnung & Hintergrund:** Was die E-Mails von GitHub/AppImage bedeuten.
2. **Screenshot-Problem lösen:** Wie du verhinderst, dass der Bot das Permissions-Fenster fotografiert.
3. **Repository aktualisieren:** Python-Code durch C++ ersetzen.
4. **Release auf GitHub veröffentlichen:** Version 3.0 sauber anlegen.
5. **AppImage-Katalog aktualisieren:** Wie das neue Release übernommen und verifiziert wird.

---

## 1. Was bedeuten deine E-Mails von AppImage?

Du musst dir **keine Sorgen** machen: Karyon ist bereits offiziell in den Katalog aufgenommen!

* **Mail 3 & 4 (Maintainer `probonopd` / PR #3774):**  
  Das war ein alter, abgelehnter Versuch, weil mehr als eine Datei geändert war. **Ist erledigt.**
* **Mail 1 & 2 (GitHub Actions Bot / PR #7159):**  
  Das war der **erfolgreiche** Versuch! Der Test war grün (`✅ Test passed`) und probonopd hat ihn in den Hauptzweig gemerged (`Merged #7159 into master`).
* **Wie der AppImage-Katalog verknüpft ist:**  
  Im Katalog [`AppImage/appimage.github.io`](https://github.com/AppImage/appimage.github.io) existiert die Datei `data/Karyon`. Darin steht:
  ```text
  https://github.com/eiseneuter/karyon
  ```
  Der Katalog verlinkt **nicht** auf eine feste Datei oder Version `1.0`. Der AppImage-Crawler schaut automatisch nach dem **neuesten („Latest“) Release** in deinem Repository. Dein Platz im Katalog bleibt dauerhaft erhalten.

---

## 2. Das Screenshot-Problem dauerhaft lösen

### Warum wurde das Permissions-Fenster fotografiert?
Wenn der AppImage-Bot ein Programm testet, startet er es in einem virtuellen Container **ohne Root-/Input-Rechte**. Karyon merkte beim Erststart, dass `/dev/uinput` nicht beschreibbar war, und öffnete den Berechtigungsdialog. Der Bot wartete 5 Sekunden und fotografierte dieses Dialogfenster.

### Die Lösung: AppStream Metainfo XML
AppImage-Kataloge und Software-Center (KDE Discover, GNOME Software) unterstützen den **AppStream-Standard**. Wenn in deinem AppImage eine Datei `usr/share/metainfo/io.github.eiseneuter.karyon.metainfo.xml` liegt, nimmt der Bot **immer** dein definiertes Bild und macht gar keinen Live-Screenshot mehr!

#### Schritt 2.1: Screenshot im Repo ablegen
Lege ein schönes Bild deines Karyon-Ringmenüs (z. B. `screenshot.png`) in deinem Projekt unter `resources/screenshot.png` ab.

#### Schritt 2.2: Metainfo-Datei erstellen
Erstelle die Datei `resources/io.github.eiseneuter.karyon.metainfo.xml`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>io.github.eiseneuter.karyon</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>
  <name>Karyon</name>
  <summary>Radial launcher and task switcher for KDE Plasma</summary>
  <developer id="io.github.eiseneuter">
    <name>eiseneuter</name>
  </developer>
  <description>
    <p>
      Karyon is a radial overlay launcher and window manager for KDE Plasma.
      By holding the right mouse button, Karyon opens a sleek, highly responsive
      radial menu right at your cursor, providing instant access to open windows,
      favorite applications, recent files, and 8-directional mouse gestures.
    </p>
  </description>
  <launchable type="desktop-id">karyon.desktop</launchable>
  <url type="homepage">https://github.com/eiseneuter/karyon</url>
  <url type="bugtracker">https://github.com/eiseneuter/karyon/issues</url>
  <provides>
    <binary>karyon</binary>
  </provides>
  <content_rating type="oars-1.1" />
  <screenshots>
    <screenshot type="default">
      <image>https://raw.githubusercontent.com/eiseneuter/karyon/main/resources/screenshot.png</image>
      <caption>Karyon radial menu in action</caption>
    </screenshot>
  </screenshots>
</component>
```

#### Schritt 2.3: In `build_appimage.sh` einbinden
In deinem `build_appimage.sh` ergänzt du bei Schritt `[2/6]` einfach:
```bash
mkdir -p "$APPDIR/usr/share/metainfo"
cp "$HERE/resources/io.github.eiseneuter.karyon.metainfo.xml" "$APPDIR/usr/share/metainfo/"
```
Und beim `appimagetool`-Aufruf nimmst du das Flag `--no-appstream` heraus (oder lässt es drin – der Bot liest die XML-Datei direkt aus dem gemounteten Dateisystem).

---

## 3. Quellcode auf GitHub aktualisieren (Python $\rightarrow$ C++)

Da das C++ Projekt aus mehreren Ordnern (`src/`, `qml/`, `resources/`, `polkit/`) besteht, erledigen wir das sauber und schnell über Git im Terminal.

### Schritt 3.1: `.gitignore` anlegen
Erstelle im Hauptverzeichnis deines C++ Projekts eine Datei `.gitignore`, damit temporäre Build-Dateien nicht hochgeladen werden:
```gitignore
build/
bin/
*.AppImage
dist/
.cache/
```

### Schritt 3.2: Git initialisieren und auf GitHub pushen
Öffne ein Terminal in deinem Ordner `Karyon C++` und führe folgende Befehle aus:

```bash
# 1. Git-Repository lokal initialisieren
git init

# 2. Den Standardzweig auf 'main' setzen
git branch -M main

# 3. Mit deinem GitHub-Repository verknüpfen
git remote add origin https://github.com/eiseneuter/karyon.git

# 4. Alle C++ Projektdateien für den Commit sammeln
git add .

# 5. Commit erstellen
git commit -m "Karyon 3.0: Full C++ & Qt Quick remake"

# 6. Auf GitHub hochladen (überschreibt den alten Python-Stand sauber)
git push -u origin main --force
```

> [!NOTE]
> Bei `git push` fragt GitHub nach deinem Benutzernamen (`eiseneuter`) und deinem **Personal Access Token** (GitHub verwendet keine Passwörter mehr für Terminal-Pushes. Einen Token erstellst du unter: *GitHub $\rightarrow$ Settings $\rightarrow$ Developer Settings $\rightarrow$ Personal access tokens $\rightarrow$ Tokens (classic)* mit Häkchen bei `repo`).

Sobald dieser Befehl durchgelaufen ist, zeigt deine GitHub-Hauptseite den C++ Quellcode.

---

## 4. Neues Release auf GitHub veröffentlichen

Du musst dein altes Release `1.0` **nicht** umbenennen oder überschreiben. Auf GitHub erstellt man einfach ein **neues Release**:

1. Öffne im Browser: [github.com/eiseneuter/karyon](https://github.com/eiseneuter/karyon)
2. Klicke rechts in der Seitenleiste auf **Releases**.
3. Klicke oben rechts auf den Button **„Draft a new release“**.
4. Fülle die Maske aus:
   * **Choose a tag:** Tippe `v3.0` ein und klicke auf `Create new tag: v3.0 on publish`.
   * **Target:** `main` (ist standardmäßig gewählt).
   * **Release title:** `Karyon 3.0`
   * **Describe this release:** Eine kurze Zusammenfassung, z. B.:
     ```markdown
     ### Karyon 3.0 (C++ / Qt Quick Remake)
     Complete rewrite from Python to native C++:
     - Ultra-low latency, smooth rendering
     - Wayland (KWin) and X11 native integration
     - 8-directional mouse flick gestures
     - Revamped settings & permission handling
     ```
5. **AppImage hochladen:**
   * Ziehe dein neu gebautes AppImage (z. B. `Karyon-3.0-x86_64.AppImage` oder `Karyon.AppImage`) in den Upload-Bereich (*„Attach binaries by dropping them here...“*).
6. **Häkchen setzen:**
   * Stelle sicher, dass **☑ Set as the latest release** aktiviert ist.
7. Klicke auf **„Publish release“**.

> [!TIP]
> **Was passiert mit dem alten Release 1.0?**  
> GitHub stuft `1.0` automatisch als älteres Release ein. Wenn du es aus Gründen der Übersichtlichkeit gar nicht mehr sehen möchtest, klickst du bei `1.0` einfach auf den Stift und ganz unten auf **„Delete this release“**.

---

## 5. Den AppImage-Katalog (`appimage.github.io`) aktualisieren

Weil in `data/Karyon` dein Repository hinterlegt ist, zieht sich der Katalog das neueste Release.

### Sofortige Aktualisierung antriggern (Pull Request)
Wenn du nicht auf den automatischen monatlichen Wartungslauf warten willst, kannst du den Test-Bot sofort manuell starten:

1. Öffne die Katalog-Datei im Browser:  
   [github.com/AppImage/appimage.github.io/blob/master/data/Karyon](https://github.com/AppImage/appimage.github.io/blob/master/data/Karyon)
2. Klicke oben rechts auf das **Stift-Symbol (Edit this file)**.  
   *(GitHub erstellt dabei automatisch einen Fork in deinem Account).*
3. Füge ganz am Ende der Datei eine neue Leerzeile ein (oder lösche ein Leerzeichen).
4. Klicke unten auf **„Propose changes“**.
5. Klicke auf der folgenden Seite auf **„Create pull request“**.

### Was der Bot jetzt macht:
* Der `github-actions[bot]` startet sofort seinen Test (`test.yml`).
* Er lädt dein neues `v3.0` AppImage von deinem GitHub-Release herunter.
* Er prüft die Desktop-Datei, die Bibliotheken und die Metadaten.
* Er liest deine `metainfo.xml`, bindet dein offizielles Screenshot-Bild ein und hinterlässt das bekannte grüne Häkchen `✅ Test passed`.
* Maintainer `probonopd` (oder der Auto-Merger) übernimmt den PR in `master`.
* Fertig! Karyon 3.0 ist im Katalog mit dem neuen Screenshot online.
