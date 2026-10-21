# 设备对话与长字幕 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for inline execution, or superpowers:subagent-driven-development if the user selects that method. A fresh final independent review is required. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给设备会话自然简短的闲聊风格，并可靠阅读超过旧320字节窗口的回答。

**Architecture:** 设备会话专用风格追加到原Bot计划；Mac缓存当前轮完整回答和一个冻结阅读快照；能力协商后设备一次只取一个小段。主屏摘要、原音频和旧端兼容保持。

**Tech Stack:** Node.js ESM、TLS v2元数据、C11、LVGL9、ESP-IDF6.0.1。

**Spec:** [已批准设计](../specs/2026-10-21-companion-voice-evolution-design.md)，第3节和交付边界。

## Global Constraints

- 仅设备会话追加建议；原Bot身份、工具、声音、明确用户要求优先，网页麦克风会话不变。
- 闲聊默认一两句；详细说明、故事、计算和任务按需展开。
- 当前回答最多UTF-8 24KiB，冻结阅读快照最多24KiB，总正文最多48KiB；只有当前轮，无磁盘历史。
- 每段不超过300字节有效UTF-8；整条JSON元数据不超过1024字节；保留换行/tab，删除NUL及其余C0并统一换行。
- generation/turn/回答版本/请求序号/段号全部核对；一个在途请求，1秒限流，3秒超时可显式重试。
- BOOT/取消/断线清空阅读身份和缓存；OFF清空设备正文；实际LVGL操作留在UI任务，快照隐藏所有私密正文和配对码。

## Review Focus

- 原生item更新不得重复追加；新轮/任务片段乱序不得混入旧回答（Task2）。
- 引号、反斜线、换行与emoji转义后仍须符合1024字节上限，文本可逐页还原（Task2）。
- 旧新端混用不能因未知op断线；无效能力和伪造页请求须拒绝（Task3）。
- 返回后/新轮后/新请求后晚到页面不能重新打开或覆盖阅读；超时不重提问题（Task3）。
- 本轮正文增长时正在阅读的快照稳定，快照与缓存实际内存有界（Task2/3）。

### Task 1: 仅设备启用的回答建议

**Files:** 新增`src/companion-style.mjs`、`tests/companion-style.test.mjs`；修改`src/native-bot.mjs`、`src/device-server.mjs`及对应测试。

**Interfaces:** `companionPlan(plan)`返回新对象，保留tools与其他字段，向instructions追加设备建议，不原地修改。NativeBotSession增加可选`replyStyle`，默认null；仅startDeviceServer默认sessionFactory传`replyStyle:'companion'`。

- [ ] 失败测试：原plan不变、原身份指令仍在、工具对象保持、未启用风格的会话instructions完全不变；设备默认启用，用户明确长回答要求在建议中优先。
- [ ] `node --test tests/companion-style.test.mjs tests/native-bot.test.mjs tests/device-server.test.mjs` Expected: FAIL，再实现纯转换与设备注入，Expected: PASS。`npm test`全绿后提交`feat: tune device conversations without changing the bot persona`。

### Task 2: 当前轮缓存与固定分段

**Files:** 新增`src/caption-book.mjs`、`tests/caption-book.test.mjs`；修改`src/pet-protocol.mjs`及protocol测试。

**Interfaces:** `CaptionBook`支持`start(generation,turn)`、`receive({turn,itemId,text})`、`open()`、`page({version,page})`、`clear()`。open返回当前冻结`{version,pages,truncated,responding}`；page返回`{version,page,pages,text,truncated,responding}`。重复open替换唯一快照。版本为单连接非零uint32；清空不允许复用旧版本。本模块不包含网络和磁盘。

协议新增`caption_catalog`与`caption_page`下行、`caption_read`上行；每条含generation、turn，页请求含request:uint32、page:uint16；page=0且version=0打开新快照，后续请求需匹配返回version。目录给出available/responding/truncated，实际页数在open返回；错误用受控`unavailable/stale/busy`，不回传请求原文。

- [ ] 写失败测试：超过320字节可完整还原；24KiB边界按codepoint截断并标记；同item替换不追加；多个片段顺序稳定；新轮/clear拒旧turn；冻结后更新正文不改旧快照，重新open版本变化；NUL/转义/emoji/空文本、无效UTF-8及满帧预算；连续open仅保留一个快照。
- [ ] `node --test tests/caption-book.test.mjs tests/pet-protocol.test.mjs` Expected: FAIL，再实现缓存和严格编码验证，Expected: PASS；`npm test`后提交`feat: keep bounded native reply pages in Mac memory`。

### Task 3: 端到端阅读和大按钮

**Files:** 修改`src/device-server.mjs`、`firmware/pet/components/pet_link/pet_link.{h,c}`、`pet_link_wire.c`（仅按需，不扩大帧限制）、`firmware/pet/main/pet_app.{h,c}`、`firmware/pet/components/pet_core/pet_caption.{h,c}`、`pet_readability.{h,c}`、`firmware/pet/components/pet_ui/pet_ui.{h,c}`；扩充server/protocol/C/真实LVGL harness及无声预览。

**Interfaces:** 沿用前一计划capabilities并协商`caption_pages`。link新增固定容量`pet_reader_page_t`（text[321]、generation/turn/version/request、page/pages、状态），回调只复制，不调用LVGL。UI动作追加`PET_UI_READER_OPEN/PREV/NEXT/RETRY/CLOSE`，由app分配请求号、限流、匹配结果并更新view。reader上行JSON通过现有outgoing队列发送；旧端无能力时保持本地原片段滚动。

- [ ] 先写失败测试：TLS真实帧协商/页请求/回复；旧端只收旧caption，新端连旧桥接不发页请求；超过旧窗口的固定中文故事翻段可还原；新轮、返回、超时、乱序和伪造身份页不改变当前文字；只有一个在途请求，超时只重试页面；正文增长不跳页。
- [ ] 新增真实LVGL点击场景：大尺寸上一段/下一段、段号、加载/重试与返回；分页按钮各至少132×48，保留独立返回与BOOT提示，避免圆边裁切。各段仍可上下滚动，按BOOT回HOME。
- [ ] 加强脱敏像素用例：切换不同主屏正文/阅读段/配对码后普通快照像素一致；不脱敏时有差异。字幕OFF或断开清目录、页、请求身份。新中文文案检查字形，必要时仅更新受控字库并核对来源。
- [ ] 对应测试Expected: 新行为FAIL；接入Book、协商、请求门禁和页面，Expected: PASS。`npm test`、公开文件检查全绿后提交`feat: read native replies in bounded device pages`。

### Task 4: 整体验证、目标构建、安装和开发同步

**Files:** 更新README中英、CHANGELOG、VALIDATION及VOICE_INPUT；新增`docs/companion-evolution-validation.json`，固定示例预览/脱敏实机图按实际完成记录。工具沿用现有build_pet、verify_application、保护安装器。

- [ ] 独立审查三项计划最终完整差异，重点C并发/栈/音频优先级、JS异步/回收/认证、UI可读性/脱敏；重要问题用失败测试修复，完整测试全绿。
- [ ] 用现有ESP-IDF6.0.1构建，Expected: PET_APPLICATION_BUILT且小于0x3e0000；verify_application通过源码与依赖绑定；不放宽单SW绘制保护。
- [ ] 真实设备读取状态，核对同一设备，保存新的完整8MiB备份和有效恢复记录；保护安装器仅写原应用槽并验证，Expected: PET_INSTALL_VERIFIED。未获得可用串口或备份校验失败则保留固件，不冒险写入。
- [ ] 无声核对状态、页面和重启保留；有使用者参与时低音量BOOT完成十轮、上下文/计算、长回答分页、播放中打断与断线后新问题；记录首段和停声时间，不自动出声，不重复任务。缺真人/声学证据项保持pending。
- [ ] 更新开发介绍和验收记录，公开检查通过后推送同一开发分支与草稿PR并等待CI；不发布正式版。主机、云端跳过项和实机成绩分别记录。
