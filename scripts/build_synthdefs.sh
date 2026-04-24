#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYNTHDEF_DIR="$ROOT_DIR/SynthDefs"
MANIFEST_PATH="$SYNTHDEF_DIR/manifest.tsv"
SCLANG_BIN="${SCLANG_BIN:-/Applications/SuperCollider.app/Contents/MacOS/sclang}"
MODE="${1:-validate}"

if [[ ! -f "$MANIFEST_PATH" ]]; then
  echo "Manifest not found: $MANIFEST_PATH" >&2
  exit 1
fi

validate_manifest() {
  local missing=0

  while IFS=$'\t' read -r name kind source output; do
    [[ -z "${name:-}" || "${name:0:1}" == "#" ]] && continue

    if [[ ! -f "$SYNTHDEF_DIR/$source" ]]; then
      echo "Missing source for $name: $source" >&2
      missing=1
    fi

    if ! grep -q "\"name\": \"$name\"" "$SYNTHDEF_DIR/catalog.json"; then
      echo "Catalog entry missing for $name" >&2
      missing=1
    fi

    if [[ -f "$SYNTHDEF_DIR/$output" ]]; then
      echo "ready    $name ($kind) -> $output"
    else
      echo "pending  $name ($kind) -> $output"
    fi
  done < "$MANIFEST_PATH"

  return "$missing"
}

check_sclang() {
  if [[ ! -x "$SCLANG_BIN" ]]; then
    echo "sclang not executable: $SCLANG_BIN" >&2
    return 1
  fi

  local probe_base
  local probe_file
  local probe
  probe_base="$(mktemp "${TMPDIR:-/tmp}/logiclikedaw_sclang_probe_XXXXXX")"
  probe_file="${probe_base}.scd"
  mv "$probe_base" "$probe_file"
  cat > "$probe_file" <<'EOF'
"LOGICLIKEDAW_SCLANG_OK".postln;
0.exit;
EOF
  probe="$("$SCLANG_BIN" "$probe_file" 2>&1 || true)"
  rm -f "$probe_file"

  if grep -qi "Incompatible processor" <<<"$probe"; then
    echo "sclang is installed but unusable on this machine:" >&2
    echo "$probe" >&2
    return 1
  fi

  if ! grep -q "LOGICLIKEDAW_SCLANG_OK" <<<"$probe"; then
    echo "sclang did not complete the probe script:" >&2
    echo "$probe" >&2
    return 1
  fi

  return 0
}

compile_one() {
  local name="$1"
  local source="$2"
  local output="$3"
  local tmpbase
  local tmpfile
  local logfile
  tmpbase="$(mktemp "${TMPDIR:-/tmp}/logiclikedaw_synthdef_XXXXXX")"
  tmpfile="${tmpbase}.scd"
  mv "$tmpbase" "$tmpfile"
  logfile="$(mktemp "${TMPDIR:-/tmp}/logiclikedaw_sclang_log_XXXXXX")"

  cat > "$tmpfile" <<EOF
~logicLikeSynthDef = nil;
thisProcess.interpreter.executeFile("${SYNTHDEF_DIR}/${source}");
if (~logicLikeSynthDef.isNil) {
    "Missing ~logicLikeSynthDef in ${source}".postln;
    1.exit;
};
~logicLikeSynthDef.writeDefFile("${SYNTHDEF_DIR}");
0.exit;
EOF

  if ! "$SCLANG_BIN" "$tmpfile" >"$logfile" 2>&1; then
    echo "Failed to compile $name" >&2
    cat "$logfile" >&2
    rm -f "$tmpfile"
    rm -f "$logfile"
    return 1
  fi

  rm -f "$tmpfile"
  rm -f "$logfile"

  if [[ ! -f "$SYNTHDEF_DIR/$output" ]]; then
    echo "sclang ran but did not produce $output for $name" >&2
    return 1
  fi

  echo "compiled $name -> $output"
}

case "$MODE" in
  validate)
    validate_manifest
    ;;
  compile)
    validate_manifest
    check_sclang

    while IFS=$'\t' read -r name kind source output; do
      [[ -z "${name:-}" || "${name:0:1}" == "#" ]] && continue
      compile_one "$name" "$source" "$output"
    done < "$MANIFEST_PATH"
    ;;
  report)
    validate_manifest || true
    echo "sclang: $SCLANG_BIN"
    if check_sclang; then
      echo "sclang-status: usable"
    else
      echo "sclang-status: blocked"
    fi
    ;;
  *)
    echo "Usage: $0 [validate|compile|report]" >&2
    exit 1
    ;;
esac
