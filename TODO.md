# Manual Integration — ✅ DONE

Implementiere eine Integration von Markdown Manual/Wiki Seiten in die App. Ergänze dazu die Top
Level Panels um das neue Panel "Manual".

Manual zeigt ein WebEngineView, welches mit MkDocs generierte html seiten anzeigt.
Die html Seiten werden von cmake aus den manual/*md seiten erzeugt.
Die Hauptversion ist in deutsch, englisch ist eine automatisch übersetzte version.
Die erzeugten html Seiten werden über das qt ressourcensystem (.qrc) in die app eingebunden.

## Implementiert:
- `manual/mkdocs.yml` mit mkdocs-static-i18n Plugin (Deutsch primär, Englisch übersetzt)
- `manual/docs/` Verzeichnis mit i18n-Suffix-Dateien (`*.de.md`, `*.en.md`)
- CMake `execute_process` baut MkDocs HTML beim `cmake ..` configure
- `qt_add_resources` bindet die generierte HTML unter `qrc:/manual/` ein
- `qml/ManualPanel.qml` mit `WebEngineView` und Sprachumschaltung (Deutsch/English)
- `Main.qml` um "Manual"-Tab erweitert (StackLayout index 4)
- `main.cpp` initialisiert `QtWebEngineQuick::initialize()`
- `CMakeLists.txt` verlinkt `Qt6::WebEngineQuick`