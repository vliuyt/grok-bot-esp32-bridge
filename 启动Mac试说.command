#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
grok_node="$(command -v node || true)"
if [[ -z "$grok_node" ]]; then
  for grok_candidate in /opt/homebrew/bin/node /usr/local/bin/node; do
    if [[ -x "$grok_candidate" ]]; then grok_node="$grok_candidate"; break; fi
  done
fi
if [[ -z "$grok_node" ]]; then printf '%s\n' '请先安装 Node.js 22.19 以上'; exit 1; fi
grok_bot="${GROK_BOT_NAME:-}"
if [[ -z "$grok_bot" ]]; then read -r -p '请输入已有 Bot 的名称: ' grok_bot; fi
exec "$grok_node" src/cli.mjs serve --bot "$grok_bot"
