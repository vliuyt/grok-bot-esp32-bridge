# 圆屏可读性固件 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline, with a fresh final independent review. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将已认可的清晰布局落到1.43C固件，保留BOOT语音、字幕门禁和全部现有设置。

**Architecture:** 可复核字库生成器生成28／30px聊天字体与22px提示字体，分离原字符回退；纯C呈现策略与实际LVGL页面共用。主机链接实际LVGL渲染和快照，之后编译目标镜像。

**Tech Stack:** C11、LVGL9、ESP-IDF6.0.1、Node22、lv_font_conv1.5.3、FontTools仅用于开发字体核查。

**Spec:** [已确认的可读性设计及固件约定](../../UI_READABILITY.md)。

## Global Constraints

- 清晰档28／30／22px；大字档仍为网页对照，不扩旧配置结构。
- 常用字灰度2bpp；GB2312的6763汉字、ASCII及常用标点；保留原字符联合覆盖，不能静默减字。
- 当前320字节片段；BOOT专用语音；导出隐藏主屏正文、阅读正文与配对码。
- 原安静、音量、亮度、扬声器、连接、配网动作及存储继续有效。
- 不自动刷机、发声或改账号／网络／分区；目标镜像必须通过原源码绑定校验。

## Review Focus

- 稀有汉字和全角符号不能因回退去重而失去原覆盖（Task1）。
- 字体压缩／行高／位图索引与目标LVGL配置必须匹配（Task1）。
- 长无标点、空字幕和关闭模式不得挤掉BOOT或留下可读旧文字（Task2／3）。
- 设置或阅读中按BOOT必须清除所有旧页面，断线也不能留旧阅读正文（Task3）。
- 普通快照须在真实渲染像素中排除两处正文与配对码（Task3）。

### Task 1: 字库与真实LVGL字体测试

**Files:** 新增 `scripts/generate-pet-fonts.mjs`、`firmware/pet/components/pet_ui/pet_fonts.{h,c}`、`fonts/pet_font_*.c`、`fonts/chat-characters.txt`、`fonts/ui-characters.txt`及`fonts/provenance.json`；字体路径均在`firmware/pet/components/pet_ui/`下。新增 `tests/pet-display.test.mjs`、`tests/display_host/{CMakeLists.txt,lv_conf.h,font_harness.c}`；修改UI CMake、字体README和第三方说明。

**Interfaces:** `pet_font_role_t={PET_FONT_HINT,PET_FONT_BODY,PET_FONT_READER}`；`const lv_font_t *pet_font(pet_font_role_t role)`。生成器接受`--noto`、`--unifont`、`--converter`、`--output`，固定校验原字体SHA和转换器1.5.3；输出22／28／30及去重回退字体、来源摘要；保留旧字体源码供比较，最终UI切换前仍保留旧链接。

- [ ] 先写失败测试：实际字体绘制“中文苹果小糖”有灰度且28／30字形高度合适；对原范围逐字符验证新字体链覆盖；错误源文件不得修改输出。
- [ ] 运行 `node --test tests/pet-display.test.mjs` 观察缺少新字库／生成器的FAIL，再实现生成器和实际LVGL主机工程。配置与生成文件随交付一起核对。
- [ ] 使用固定摘要源及1.5.3生成，检查聊天字体字符集、非重复回退、行高与最大位图索引。`node --test tests/pet-display.test.mjs` Expected: PASS，无真实设备调用。
- [ ] `npm test` Expected: 全绿；提交。目标LVGL依赖尚未下载的干净环境明确跳过目标依赖测试。

### Task 2: 圆屏呈现与阅读门禁

**Files:** 新增 `firmware/pet/components/pet_core/pet_readability.{h,c}`、`tests/pet_readability_harness.c`、`tests/pet-readability.test.mjs`；修改core CMake。

**Interfaces:** `pet_page_t`按HOME=0、SETTINGS=1、CONNECTION=2、READING=3、VOLUME=4、BRIGHTNESS=5、CAPTIONS=6、MORE=7、PROVISION=8、DETAIL=9、COUNT排序；`pet_readability_input_t`含pose/dialogue/connection/caption_mode/waiting_long/caption；`pet_readability_text(input)`返回summary[321]、hint、can_read、local；`pet_readability_page(page,input)`拒绝非法页或无效阅读；`pet_readability_face(pose)`返回约200px、中心Y189的同款圆脸。

- [ ] 先用C harness写失败用例：FULL与COMPACT主屏均首句、OFF无正文但有本地状态；长无标点仍有效UTF8；LISTEN／WAIT／断线无阅读入口；旧阅读在空／关闭／新轮次回HOME；全部动画姿态离字幕与圆边有安全间隔。
- [ ] `node --test tests/pet-readability.test.mjs` Expected: 缺少实现FAIL；实现纯C策略后Expected: PASS。
- [ ] `npm test` Expected: 全绿；提交。

### Task 3: 实际页面、隐私像素与目标构建

**Files:** 重做 `pet_ui.c`，修改`pet_ui.h`、`pet_app.c`接入网络／配网状态；新增 `tests/display_host/{ui_harness.c,fakes/...}`，扩充display主机工程；用真实UI回归替代旧提取函数的`tests/pet-ui-navigation.test.mjs`。更新使用说明及`docs/firmware-readability-validation.json`。

**Interfaces:** 现有`pet_ui_start/update/home/request_snapshot/bench_page`保留；bench0/1/2含义不变。view追加wifi_online/mac_online/setup_open；现有配置和动作值不变。所有新页面只在UI任务中操作，真实主机测试仅替换板级／RTOS／USB编码出口，不替换LVGL或生产UI逻辑。

- [ ] 先给实际UI写失败场景：三项大设置点击与返回、更多中的旧动作、读取长字幕、BOOT返回和断线清除阅读、OFF提示保留；带不同私密正文和配对码的普通导出像素应一致，未脱敏渲染则有差异。
- [ ] 实现主屏28px两行、阅读30px滚动、22px独立提示和三项大设置及子页；接入Task1／2。主屏正文只有真实可读字幕才可点击，本地提示不触发阅读。
- [ ] `node --test tests/pet-display.test.mjs tests/pet-readability.test.mjs` Expected: PASS；导出固定示例的实际LVGL画面和安全检查记录，不含用户内容。
- [ ] 执行 `firmware/tools/build_pet.py --idf <ESP-IDF6.0.1>`，Expected: PET_APPLICATION_BUILT且小于0x3e0000；用verify_application检查源码／依赖绑定；不执行flash。
- [ ] `npm test`及`python3 scripts/check-publication.py` Expected: 全绿；提交并进行新上下文独立C／生成器审查，一次修复重要项。
- [ ] 记录仅主机通过，实屏、重启、触摸和音频仍待验收；保留当前开发分支，不发布版本。
