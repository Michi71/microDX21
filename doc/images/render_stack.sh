#!/usr/bin/env bash
# Render the microDX21 exploded stack image with the bundled image_gen CLI.
#
# Usage:
#   OPENAI_API_KEY=sk-... ./doc/images/render_stack.sh
#
# Produces:
#   doc/images/microdx21_stack_exploded.png  (1536x1024, PNG, quality=high)
set -euo pipefail
CODEX_HOME="${CODEX_HOME:-$HOME/.codex}"
IMAGE_GEN="$CODEX_HOME/skills/.system/imagegen/scripts/image_gen.py"
PROMPT_FILE="doc/images/microdx21_stack_exploded.body.txt"
OUT="doc/images/microdx21_stack_exploded.png"

if [[ -z "${OPENAI_API_KEY:-}" ]]; then
  echo "OPENAI_API_KEY is not set. Export it first." >&2
  exit 2
fi
if [[ ! -f "$PROMPT_FILE" ]]; then
  echo "Prompt body not found: $PROMPT_FILE" >&2
  exit 2
fi

python "$IMAGE_GEN" generate \
  --prompt-file "$PROMPT_FILE" \
  --size 1536x1024 \
  --quality high \
  --out "$OUT"
echo "Wrote $OUT"
