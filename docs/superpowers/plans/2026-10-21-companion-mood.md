# 情绪延续 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for inline execution, or superpowers:subagent-driven-development if the user selects that method. A fresh final independent review is required. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 在现有即时表情后增加一分钟、可积累并自然衰减的安静余韵。

**Architecture:** 纯C状态保存亲近程度及更新时间，pose只在待机叠加有上限的细微动作；不新增云端服务或持久化写入。预览复用真实core导出。

**Tech Stack:** C11、Node行为harness、真实LVGL、现有无声预览。

**Spec:** [已批准设计](../specs/2026-10-21-companion-voice-evolution-design.md)，第2节。

## Global Constraints

- 抚摸增加35，轻触增加10，最大100；60秒线性回到平静。
- 保留1.8秒开心/2秒好奇即时反馈；余韵不覆盖字幕，不刷新空闲时间、不阻止休眠、不高频写NVS。
- 听/想/说/错误/困倦/休眠优先；BOOT不受触摸影响，触屏不录音。
- 招呼最多每日3次、至少间隔30分钟、08:00–22:00且有效时钟；始终不自动发声。

## Review Focus

- 在衰减中反复抚摸须先结算衰减再增加，不能无限维持满值（Task1）。
- 倒退或极大时钟、计时器回绕不能溢出或产生NaN pose（Task1）。
- 余韵须保留IDLE语义，不能遮住上一句可读字幕或唤醒睡眠（Task1）。
- 语音期间触摸只记录余韵，不能改变当前音频状态或生成提交（Task1）。
- 预览固定选中状态与真实事件控制器均须匹配新core，不能只改网页动画（Task1）。

### Task 1: 模型、呈现与无声预览

**Files:** 修改`firmware/pet/components/pet_core/pet_core.{h,c}`、`tests/pet_core_harness.c`、`tests/pet_face_harness.c`、`tests/pet_pose_export.c`、`tests/pet-preview.test.mjs`、`preview/companion/controller.mjs`、`states.json`，必要时修改preview渲染参数；不改网络/账号。

**Interfaces:** state追加`warmth`和`warmth_at_ms`；`pet_warmth(const pet_state_t*,uint64_t now_ms)`返回0–100当前值，按60秒线性衰减。事件先算旧值再饱和增加并更新时间。余韵只影响IDLE pose的小幅呼吸、眼神和身体参数，保持原expression值；原pet_step、pet_pose签名不变。

- [x] 写失败测试：+35/+10、上限100、30秒减半/60秒归零、半途再摸按衰减值累积；倒退时间无新增能量，uint64边界不溢出；通话与错误优先，余韵不阻止90/120秒电池休眠；有真实字幕时仍可点读。
- [x] `node --test tests/pet-core.test.mjs tests/pet-preview.test.mjs tests/pet-readability.test.mjs` Expected: 新行为FAIL。
- [x] 实现模型与受限pose；更新预览控制器和真实core导出固定例，禁止背景自动音频。每次interaction仍只触发现有一次wake。
- [x] 同命令和`npm test` Expected: PASS；运行`npm run preview:export`同步真实参数。真实LVGL字幕及快照检查通过后提交`feat: retain gentle petting mood between interactions`。
- [x] 独立C/预览审查完整差异，重要项以失败测试修复。实屏手感与音频回归留到统一安装后人工确认。

Implementation and independent reviews complete. Two preview findings fixed RED→GREEN. Target/LVGL and physical motion checks remain in final integration, not claimed here.
