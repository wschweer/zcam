#!/bin/bash
# Post-process MkDocs-generated HTML to fix broken links.
#
# The mkdocs-static-i18n plugin generates broken cross-language
# navigation links like href="../manual.en.md" that don't resolve
# in the browser.  This script fixes:
#
#   1. Directory URLs: href="X/" → href="X/index.html" (relative only)
#   2. German .en.md links → en/ directory pages
#   3. English .de.md links → root-level directory pages

set -e

SITE_DIR="$1"
if [ -z "$SITE_DIR" ]; then
    echo "Usage: $0 <site-dir>"
    exit 1
fi

find "$SITE_DIR" -name '*.html' -print0 | while IFS= read -r -d '' f; do
    rel="${f#$SITE_DIR/}"

    # 1. Fix directory URLs: append index.html to relative hrefs ending in /
    #    Skip absolute URLs (http://, https://, //) and anchors (#)
    sed -i -E '/href="(https?:)?\/\//!s|href="([^"#][^"]*)/"|href="\1/index.html"|g' "$f"

    # 2. Fix href="." → href="index.html" (current directory)
    sed -i 's|href="."|href="index.html"|g' "$f"

    # 3. Fix href=".." → href="../index.html"
    sed -i 's|href="\.\."|href="../index.html"|g' "$f"

    if echo "$rel" | grep -q '^en/'; then
        # English page (under en/):
        # href="../../index.de.md" → href="../../index.html"
        # href="../../manual.de.md" → href="../../manual/index.html"
        # href="../index.de.md" → href="../index.html"
        # href="../manual.de.md" → href="../manual/index.html"
        sed -i -E 's|href="\.\./\.\./index\.de\.md"|href="../../index.html"|g' "$f"
        sed -i -E 's|href="\.\./\.\./([a-z]+)\.de\.md"|href="../../\1/index.html"|g' "$f"
        sed -i -E 's|href="\.\./index\.de\.md"|href="../index.html"|g' "$f"
        sed -i -E 's|href="\.\./([a-z]+)\.de\.md"|href="../\1/index.html"|g' "$f"
    else
        # German page (root level):
        # href="index.en.md" → href="en/index.html"
        # href="manual.en.md" → href="en/manual/index.html"
        # href="../index.en.md" → href="../en/index.html" (from subdirectory pages)
        # href="../manual.en.md" → href="../en/manual/index.html" (from subdirectory pages)
        sed -i -E 's|href="index\.en\.md"|href="en/index.html"|g' "$f"
        sed -i -E 's|href="([a-z]+)\.en\.md"|href="en/\1/index.html"|g' "$f"
        sed -i -E 's|href="\.\./index\.en\.md"|href="../en/index.html"|g' "$f"
        sed -i -E 's|href="\.\./([a-z]+)\.en\.md"|href="../en/\1/index.html"|g' "$f"
    fi
done