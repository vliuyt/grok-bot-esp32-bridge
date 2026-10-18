#!/bin/bash
set -euo pipefail
grok_label="io.github.grokpet.bridge"
grok_agent="$HOME/Library/LaunchAgents/$grok_label.plist"
grok_domain="gui/$(id -u)"
if [[ ! -f "$grok_agent" ]]; then printf '%s\n' '未安装后台服务；前台运行时请在终端按 Control-C。'; exit 0; fi
launchctl disable "$grok_domain/$grok_label"
if launchctl print "$grok_domain/$grok_label" >/dev/null 2>&1; then launchctl bootout "$grok_domain" "$grok_agent"; fi
printf '%s\n' '桥接已停止，下次登录也不会启动；启动脚本可重新启用。'
