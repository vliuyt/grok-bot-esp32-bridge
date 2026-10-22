# 小糖日常体验 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans in this conversation. Steps use checkbox (`- [ ]`) syntax. Independent review follows implementation.

**Goal:** 让首次录音时机明确，触摸有即时差异反应，动作和真实声音自然衔接。

**Architecture:** 既有纯C状态机负责门禁与触摸状态；纯目标姿态由UI线程的固定数值平滑器跟随。复用现有音频／连接／LVGL路径。

**Tech Stack:** C11、LVGL9、ESP-IDF6.0.1、Node主机测试。

**Spec:** [已授权范围和实现约定](../specs/2026-10-22-companion-polish-design.md)。

## Global Constraints

- 保留BOOT唯一语音入口、原生小糖、经典圆脸、音量和配对。
- 不自动发声、录音或重放任务；不新增周期性强制重连。
- 不放宽1024字节帧、音频优先级、单SW字体渲染与保护安装约束。
- 所有触摸和动作状态固定容量，无NVS写入、账户／会话正文保存。
- 已有独立公开开发目录和codex/v0.2.0-roadmap分支复用；基线2eb7ff5。

## Review Focus

- 连接在按住中恢复不能提交半句，正常READY首次按压不受影响。
- turn0登录提示只允许同代数且未就绪，不跨通话污染或阻止休眠。
- 移动事件限频，丢失触点／上划切页／BOOT不能残留按压。
- 触摸反应不能盖过语音、错误或休眠；坐标和时间异常不使姿态越界。
- 平滑只在UI线程修改、按真实时间收敛，停顿不能继续假装说话。

### Task 1: 首次对话与连接提示

**Files:** pet_core.{h,c}、pet_readability.{h,c}、pet_ui.{h,c}、pet_app.c；tests/pet_core_harness.c、pet_readability_harness.c。

**Interfaces:** 由既有held/dialogue导出本次按压尚未录音标记，readability接收该标记。连接恢复不再在LINK处理器自动begin。未就绪同代数turn0的auth_required单独验收。

- [x] 新增行为断言并运行`node --test tests/pet-core.test.mjs tests/pet-readability.test.mjs`，Expected: 半句与auth提示断言FAIL。
- [x] 实现门禁和可读提示；相同命令Expected: PASS，完整测试通过后提交。

### Task 2: 即时触摸与不同反应

**Files:** pet_core.{h,c}、pet_ui.{h,c}、pet_app.c、pet_face.c；新增tests/pet-polish.test.mjs、pet_polish_harness.c；扩充display_host/ui_harness.c。

**Interfaces:** 末尾追加CONTACT事件／UI动作，phase为DOWN/MOVE/UP/CANCEL，phase/x/y经严格校验打包入现有int value，不扩大队列；目标pose增加look_y。状态保存固定容量触点、反应与快点计数，时间和坐标严格限界。旧PET／TOUCH_TAP动作继续负责已有余韵。

- [x] 先测试落指立即看向触点、移动跟随、三连点躲闪、困倦唤醒、抬指和失触取消、语音优先；Expected: FAIL。
- [x] 实现事件交付和纯状态；Expected: 新行为及真实LVGL指针PASS，旧设置／阅读／右滑不回归。
- [x] 验证事件限频、屏幕范围外输入与重复触点，完整测试通过后提交。

### Task 3: 连贯表情与真实说话节奏

**Files:** pet_core.{h,c}、pet_ui.c；tests/pet_polish_harness.c与实际LVGL用例。

**Interfaces:** `pet_motion_t`固定容量保存pose/last_ms/initialized；`pet_motion_step(m,target,now)`返回平滑pose。UI draw唯一调用；body时间常数90ms、eye60ms，首帧／回退／超过500ms间隔重置。语音level在最后播放事件后80ms开始以120ms时间常数衰减。

- [x] 先测试姿态不跳变、不同帧率等价、长间隔／回退安全、屏幕状态立即生效、真实电平停顿收动作；Expected: FAIL。
- [x] 实现最小数值过渡；Expected: PASS，完整检查及栈检查通过后提交。

### Task 4: 集成、构建、保护安装和开发同步

- [x] 独立审查本轮完整差异，处理重要问题并保留测试证据。
- [x] ESP-IDF6.0.1目标构建、来源与依赖绑定、单渲染器、原应用槽大小验证。
- [x] 同板核验、新完整8MiB私人备份与恢复记录后，原应用槽保护安装并读回；不覆盖网络、配对、音量或引导元数据。
- [ ] 无声检查就绪与页面；用户参与实屏触摸、动作和长待机后首次BOOT试说。未经实物验证的时延／长待机指标保持pending。
- [ ] 更新体验说明和验收记录，通过公开检查后同步同一开发分支／草稿PR并等待CI；不发布正式版。
