# 可靠通话 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for inline execution, or superpowers:subagent-driven-development if the user selects that method. Each plan requires a fresh final independent review. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 恢复失效网关，并让每轮失败可解释、可安全重试。

**Architecture:** Backend为安全操作做有界路由更新；原生语音独立监测本轮进展，受控状态经可选能力传到纯C呈现策略。连接恢复不重发用户输入或任务。

**Tech Stack:** Node.js ESM、内建node:test、TLS PSK、C11、LVGL9、ESP-IDF6.0.1。

**Spec:** [已批准设计](../specs/2026-10-21-companion-voice-evolution-design.md)，第1节。

## Global Constraints

- 保留正常登录、Bot原指令、工具、声音、BOOT操作和原音频格式。
- 新轮、取消、断开使本轮定时器和旧状态全部失效；不保存录音、正文、令牌或原生响应ID。
- 12秒提示等待，30秒无进展取消；工具/响应持续进展时不误判，60秒无进展结束等待。
- 只读/幂等请求可有界刷新路由，发送任务和保存通话不重放。
- hello保持v2，新状态只发给协商`turn_status`的设备；元数据最多1024字节。

## Review Focus

- 并发404只触发一次更新，取消一个等待者不得中断另一有效调用（Task1）。
- 更新后仍404必须结束本次尝试，不能永久循环，也不能重放有副作用请求（Task1）。
- 按BOOT新轮后旧识别、响应、超时和工具完成不得改变当前轮（Task2）。
- 工具响应、空回复和录音太短须分清，不能靠有声音字节数猜测没听清（Task2）。
- 老设备未知元数据会断线，必须协商后再发送；固件等待失败不得被脱敏关闭字幕隐藏（Task3）。

### Task 1: 有界网关恢复

**Files:** 修改`src/backend.mjs`、`tests/backend.test.mjs`。

**Interfaces:** 保留`Backend.connectGateway(signal)`、`gatewayCall(method,body,signal)`、`openEvents(signal)`。内部共享同一过期绑定的更新请求；调用者取消只取消自己的等待，最后等待者离开时终止底层更新。安全请求仅在401/403/404/417更新一次；原RPC接口和白名单不扩大。

- [x] 新增失败测试：旧绑定404→broker200→新绑定200，事件流同样恢复；10个并发404只有一次broker请求；一个中止其余继续，全部中止底层请求终止；更新后404只尝试一次；`sendPrompt/nudgeVoiceCall/recordVoiceCall`的404不重放，网络不通不重放。
- [x] 运行`node --test tests/backend.test.mjs`，Expected: 新测试因缺恢复逻辑FAIL。
- [x] 实现有界单次恢复及并发等待管理，完整保留路径前缀、网络令牌、允许域名和信号语义。
- [x] 同一测试Expected: PASS；`npm test` Expected: 全绿且现有本地目标依赖不跳过。提交`fix: renew stale gateway routes without replaying mutations`。

### Task 2: 原生轮次进展与超时

**Files:** 新增`src/voice-turn.mjs`、`tests/voice-turn.test.mjs`；修改`src/realtime.mjs`、`src/native-bot.mjs`、`tests/realtime.test.mjs`、`tests/native-bot.test.mjs`。

**Interfaces:** `createTurnMonitor({now,setTimer,clearTimer,onStatus,onExpire})`返回`start(turn)`、`note(turn,kind)`、`clear(turn)`、`close()`；kind为`recognized/response/audio/tool_started/tool_completed/done`。受控状态`turnStatus`事件为`{turn,code}`，code为`waiting/working/short_input/no_reply/empty_response/unrecognized/response_failed/auth_required`。RealtimeSession只为有效的本轮事件note；NativeBotSession原样转发事件。`onExpire`停止旧回复并允许新一轮，不重交输入。短输入沿用4800字节阈值并发状态，不假称识别失败。

- [x] 写失败测试：虚拟时钟12秒等待、30秒取消、工具开始/完成延长到60秒无进展兜底；新轮/close后旧timer无效；短输入无commit并提示；空完成和明确失败有状态；音频/识别属于旧item/response时忽略；工具完成、取消后的晚到音频仍拒绝。
- [x] `node --test tests/voice-turn.test.mjs tests/realtime.test.mjs tests/native-bot.test.mjs` Expected: 新行为FAIL。
- [x] 实现监测器和事件归属，不因一般状态通知关闭整条任务通道；保留旧任务去重及异常投递保护。无法证明无标签旧响应归属时采用停止/重建会话，而非认领新轮。
- [x] 同命令及`npm test` Expected: PASS；提交`feat: report bounded native voice turn progress`。

### Task 3: 设备状态和兼容握手

**Files:** 修改`src/pet-protocol.mjs`、`src/device-server.mjs`、`firmware/pet/components/pet_link/pet_link.{h,c}`、`firmware/pet/components/pet_core/pet_core.{h,c}`、`pet_readability.{h,c}`、`firmware/pet/main/pet_app.{h,c}`；扩充对应protocol/server/core/readability及真实LVGL测试。

**Interfaces:** hello可选`capabilities`字符串数组，本项能力`turn_status`。协商后消息`{op:'turn_status',generation,turn,code}`，仅接受上项枚举；C新增`PET_EV_TURN_STATUS`及受控notice，按generation/turn验收。向presenter交付notice，不把云端原文直接当错误提示。允许后续计划再协商`caption_pages`。

- [x] 写失败测试：老客户端不收新op；新设备对旧桥接保留兜底；旧轮状态不取消新录音；短输入、超时、空回复/明确识别失败映射不同中文；字幕OFF仍见状态，真实正文不泄漏到快照。
- [x] 对应Node/C/真实LVGL测试Expected: FAIL，再实现状态桥接与文案，保持旧enum顺序、页面编号和现有控制动作。
- [x] `npm test`、`python3 scripts/check-publication.py` Expected: PASS。暂不刷机；提交`feat: show actionable device voice feedback`。
- [x] 独立审查本计划完整差异；重要项用失败回归修复并重跑全套。只记录主机证据，实机统一在第三计划后完成。
