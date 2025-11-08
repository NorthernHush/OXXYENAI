#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./collect_re_prompts.sh urls.txt output.jsonl
# urls.txt: список URL или путей к локальным html файлов (по одному на строку)
# output.jsonl: файл с JSON-объектами по одному на строку: {"instruction":"...","output":"..."}

URLS_FILE="${1:-urls.txt}"
OUT_FILE="${2:-prompts.jsonl}"

# Dependencies: curl or wget, python3, one of: lynx/w3m/html2text (for HTML->text)
# Check deps
cmds=("python3" )
for c in "${cmds[@]}"; do
  if ! command -v "$c" >/dev/null 2>&1; then
    echo "Требуется '$c' — установите его и повторите." >&2
    exit 1
  fi
done

# pick HTML->text converter
HTML2TEXT=""
if command -v lynx >/dev/null 2>&1; then
  HTML2TEXT="lynx -stdin -dump -nolist"
elif command -v w3m >/dev/null 2>&1; then
  HTML2TEXT="w3m -dump -T text/html"
elif command -v html2text >/dev/null 2>&1; then
  HTML2TEXT="html2text -utf8 -nobs"
else
  # fallback: very naive tag stripper (less reliable)
  HTML2TEXT="sed -n '1,\$p' | sed 's/<[^>]*>/ /g'"
  echo "Предупреждение: нет lynx/w3m/html2text — буду использовать простую очистку тегов." >&2
fi

# Keywords to match (case-insensitive). Добавляйте по вкусу.
KEYWORDS=("reverse" "reverse-engineering" "reverse engineering" "ELF" "stack canary" "stack_chk_fail" "stack protector" "canary" "Ghidra" "radare" "rizin" "IDA" "disassembly" "disassemble" "decompile" "binary analysis" "objdump" "strace" "ltrace")

# function: check robots.txt (basic): if /robots.txt disallows path with User-agent: *
check_robots() {
  local url="$1"
  # only basic check for http(s)
  if [[ "$url" =~ ^https?:// ]]; then
    local base
    base=$(echo "$url" | sed -E 's#(https?://[^/]+).*#\1#')
    local path
    path=$(echo "$url" | sed -E "s#https?://[^/]+(/.*)?#\1#")
    path=${path:-/}
    # fetch robots.txt (cached in /tmp per host)
    local hostfile="/tmp/robots_$(echo "$base" | sed 's|[:/]|_|g')"
    if [ ! -f "$hostfile" ]; then
      if command -v curl >/dev/null 2>&1; then
        curl -sSf "$base/robots.txt" -o "$hostfile" || echo "" > "$hostfile"
      elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$hostfile" "$base/robots.txt" || echo "" > "$hostfile"
      else
        echo "" > "$hostfile"
      fi
    fi
    # naive check for "User-agent: *" rules, see if any Disallow matches prefix of path
    awk -v path="$path" '
      BEGIN{ua=""; disallowed=0}
      /^User-agent:/ { gsub(/\r/,""); ua=tolower($0); next }
      /^Disallow:/ {
        if (ua ~ /user-agent: \*/ || ua=="") {
          sub(/^Disallow:[ \t]*/,"",$0)
          if ($0=="" || $0=="/") { print "DISALLOW_ALL"; exit }
          # simple prefix match
          if (index(path, $0)==1) { print "DISALLOW_MATCH"; exit }
        }
      }
    ' "$hostfile"
  fi
}

# create/empty output
: > "$OUT_FILE"

# helper: escape string to JSON using python
json_escape() {
  python3 - <<PY
import json,sys
s = sys.stdin.read()
print(json.dumps(s, ensure_ascii=False))
PY
}

# Read URLs
while IFS= read -r url || [ -n "$url" ]; do
  url="$(echo "$url" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  [ -z "$url" ] && continue

  # robots check
  robot_result=$(check_robots "$url" || true)
  if [[ "$robot_result" == "DISALLOW_ALL" || "$robot_result" == "DISALLOW_MATCH" ]]; then
    echo "Пропускаю $url — robots.txt запрещает доступ (базовая проверка)." >&2
    continue
  fi

  echo "Обрабатываю: $url" >&2

  # fetch content
  content=""
  if [[ "$url" =~ ^https?:// ]]; then
    if command -v curl >/dev/null 2>&1; then
      raw=$(curl -L -sS "$url" || true)
    else
      raw=$(wget -q -O - "$url" || true)
    fi
  else
    # treat as local file
    if [ -f "$url" ]; then
      raw=$(cat "$url")
    else
      echo "Файл не найден: $url" >&2
      continue
    fi
  fi

  # convert HTML to text
  content=$(printf '%s' "$raw" | eval "$HTML2TEXT" 2>/dev/null || printf '%s\n' "$raw" | sed 's/<[^>]*>/ /g')

  # normalize spaces, split into paragraphs
  # paragraphs are blocks separated by 2+ newlines
  IFS=$'\n\n' read -d '' -r -a paras < <(printf '%s' "$content" | sed 's/\r//g' | awk 'BEGIN{RS="";ORS="\n\n"}{gsub(/\n/," ");print}' && printf '\0')

  for p in "${paras[@]}"; do
    # skip short lines
    p_trim=$(echo "$p" | sed -E 's/^[[:space:]]+|[[:space:]]+$//g')
    [ "${#p_trim}" -lt 80 ] && continue

    # check keywords
    matched=0
    for kw in "${KEYWORDS[@]}"; do
      if echo "$p_trim" | grep -qi -- "$kw"; then
        matched=1
        break
      fi
    done
    [ "$matched" -eq 0 ] && continue

    # extract first sentence (naive): up to first . ? !
    first_sentence=$(echo "$p_trim" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//; s/([.?!]).*/\1/; s/\s+/ /g' | awk '{
      s=$0; match(s,/([^.?!]*[.?!])/,m);
      if (length(m[0])>0) print m[0]; else print (length(s)>200?substr(s,1,200)"...":s)
    }')

    # build instruction: попросим ИИ объяснить/переформулировать фрагмент в виде вопроса
    # format instruction concisely in Russian
    instruction="Сформулируй вопрос/инструкцию для ИИ по следующему фрагменту: \"$first_sentence\""

    # output: краткая версия (усечённый параграф)
    output_text=$(echo "$p_trim" | sed -E 's/\s+/ /g' | cut -c1-800)
    # if longer than 800 chars, append ellipsis
    if [ "$(printf '%s' "$p_trim" | wc -c)" -gt 800 ]; then
      output_text="${output_text}..."
    fi

    # write JSON object (one per line)
    # use python to ensure proper escaping and no ascii-escape
    json_obj=$(printf '%s\n%s\n' "$instruction" "$output_text" | python3 - <<PY
import sys, json
ins = sys.stdin.readline().rstrip("\n")
out = sys.stdin.read().rstrip("\n")
obj = {"instruction": ins, "output": out}
print(json.dumps(obj, ensure_ascii=False))
PY
)
    printf '%s\n' "$json_obj" >> "$OUT_FILE"
  done

done < "$URLS_FILE"

echo "Готово — результаты в $OUT_FILE" >&2
