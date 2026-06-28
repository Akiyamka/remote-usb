#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_UI_DIR="$ROOT_DIR/web_ui"
WEB_DIST_DIR="$ROOT_DIR/firmware/web_dist"

pnpm --dir "$WEB_UI_DIR" build

rm -rf "$WEB_DIST_DIR"
mkdir -p "$WEB_DIST_DIR"

while IFS= read -r -d '' file; do
  rel_path="${file#"$WEB_UI_DIR/dist/"}"
  target_dir="$WEB_DIST_DIR/$(dirname "$rel_path")"
  mkdir -p "$target_dir"
  gzip -n -c "$file" > "$WEB_DIST_DIR/$rel_path.gz"
done < <(find "$WEB_UI_DIR/dist" -type f -print0)

printf 'Updated %s from %s\n' "$WEB_DIST_DIR" "$WEB_UI_DIR/dist"
