#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
grok_label="io.github.grokpet.bridge"
grok_agent="$HOME/Library/LaunchAgents/$grok_label.plist"
grok_domain="gui/$(id -u)"
if [[ -f "$grok_agent" ]]; then
  launchctl enable "$grok_domain/$grok_label"
  if ! launchctl print "$grok_domain/$grok_label" >/dev/null 2>&1; then
    launchctl bootstrap "$grok_domain" "$grok_agent"
  fi
  launchctl kickstart "$grok_domain/$grok_label"
  printf '%s\n' '设备桥接已启用：http://127.0.0.1:8788'
  exit 0
fi
grok_node="$(command -v node || true)"
if [[ -z "$grok_node" ]]; then
  for grok_candidate in /opt/homebrew/bin/node /usr/local/bin/node; do
    if [[ -x "$grok_candidate" ]]; then grok_node="$grok_candidate"; break; fi
  done
fi
if [[ -z "$grok_node" ]]; then printf '%s\n' '请先安装 Node.js 22.19 以上'; exit 1; fi
grok_bot="${GROK_BOT_NAME:-}"
if [[ -z "$grok_bot" ]]; then read -r -p '请输入已有 Bot 的名称: ' grok_bot; fi
exec "$grok_node" src/cli.mjs device --bot "$grok_bot"
