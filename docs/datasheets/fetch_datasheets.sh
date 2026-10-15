#!/usr/bin/env bash
#
# Download local copies of the datasheets referenced in README.md.
# Requires internet access. Existing files are skipped unless -f is given.
#
#   ./fetch_datasheets.sh        # fetch anything missing
#   ./fetch_datasheets.sh -f     # re-fetch everything
#
set -uo pipefail

cd "$(dirname "$0")"

FORCE=0
[[ "${1:-}" == "-f" ]] && FORCE=1

# filename|url
DOCS=(
  "seeed-kit-102010710-datasheet.pdf|https://files.seeedstudio.com/Bazaar/product_pdf/102010710.pdf"
  "semtech-sx1261-2-datasheet-rev2.2.pdf|https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf"
  "semtech-sx1262-datasheet-rev1.2.pdf|https://cdn.sparkfun.com/assets/6/b/5/1/4/SX1262_datasheet.pdf"
)

ok=0
failed=()

for entry in "${DOCS[@]}"; do
  name="${entry%%|*}"
  url="${entry#*|}"

  if [[ -s "$name" && $FORCE -eq 0 ]]; then
    echo "skip    $name (already present)"
    ok=$((ok + 1))
    continue
  fi

  printf 'fetch   %-46s ' "$name"
  if curl -fsSL --retry 2 --max-time 120 -o "$name.part" "$url"; then
    mv "$name.part" "$name"
    echo "ok ($(du -h "$name" | cut -f1))"
    ok=$((ok + 1))
  else
    rm -f "$name.part"
    echo "FAILED"
    failed+=("$name  <-  $url")
  fi
done

echo
echo "$ok of ${#DOCS[@]} documents present."

if ((${#failed[@]})); then
  echo
  echo "Could not download:"
  printf '  %s\n' "${failed[@]}"
  echo
  echo "Vendors move these around. Grab them manually from the landing pages"
  echo "listed in README.md and drop them in this folder under the same name."
  exit 1
fi

echo
echo "Registration-gated or HTML-only references (see README.md for links):"
echo "  - Nordic nRF52840 Product Specification (Nordic doc portal)"
echo "  - Semtech SX1262 application notes (Semtech account required)"
echo "  - Seeed XIAO nRF52840 schematic (linked from the XIAO_BLE wiki page)"
