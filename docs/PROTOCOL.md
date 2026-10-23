# 设备、桥接与原生会话

## 本地连接

设备用 TLS PSK 连接 Mac 的私人局域网监听器，默认 TCP 8790。随机 32 字节密钥在首次本机配置生成；只通过受同源令牌保护的 localhost 配网页面及显式 USB／认证 BLE 下发。设备不知道账户凭证。Mac 使用 mDNS `_grokpet._tcp` 宣告公开桥接标识和版本，不宣告密钥。

已有二进制音频／控制线协议由 `src/device-wire.mjs` 和 `firmware/grok_wire.h` 定义，新增宠物消息由 `src/pet-protocol.mjs`、`pet_link_wire.c` 定义。不要把 Muse 云协议当作该本地协议。JSON 元数据只在宠物 hello 协商后发送，最大 1024 字节；字段采用 allowlist。

## 轮次与音频

设备音频为 16 kHz、单声道、16 位 PCM；Mac 根据原生会话格式转换。连接代数和轮次共同隔离旧字幕、状态、音频。按下取消旧播放和响应，清空本地队列；松开要等录音尾块确认后提交。回答的云端完成与设备实际排空分别处理，不能提前回到待机。

实体 BOOT 控制语音轮次：按下开始录音或打断旧回复，松开提交。触屏只负责抚摸、唤醒和设置，不产生语音录制事件。

播放使用有界队列和 credits。新问题令已出队旧块也失效；过载采用取消／重连，断线不重传旧录音。Mac 未播放音频最多约 180 秒。

## 原生登录与任务

`auth.mjs` 只读取当前 Grok Bot App 的正常登录，macOS Safe Storage 需要用户正常授权；`backend.mjs` 限制可信 HTTPS 目标。它们是当前内部契约适配，不是兼容其他产品账号的通用接口。

读取／SSE 在明确过期拒绝时可重新取得一次网关资料；任务提交、通话记录等可能有副作用的操作不会自动重放。任务结果以 `voice:<当前通话>` 持久化消息 ID 为唯一去重来源，SSE 只唤醒补读；单飞、退避与取消防止并发重复和挂断后的迟到回流。仅存在于 SSE 的特殊结果未认证支持。

## 测试

纯 Node／C mock 检查覆盖线协议、失效轮次、录音收尾、播放清空、任务去重与失败退避。修改任一侧格式时必须同时更新兼容测试；旧客户端不能被迫接收它未协商的宠物元数据。

## 可选通话反馈与长字幕（v0.2开发分支）

v2 hello可带`capabilities`，双方取交集，最多8个短字符串。`turn_status`启用受控等待／失败通知；`turn_progress`单独协商，只有真实进展才续设备等待时限。旧端不会收到未协商消息。

`native_resume`表示固件支持同一TLS上的原生语音重连窗口：进入未就绪时重新开始45秒期限，重复connecting不续期，20秒无收包期限不变。Mac仅在协商此能力、设备播放已排空且原生输入／识别／响应／任务全部结束时，才对已识别的长闲置超时尝试一次续接。期间发送link:connecting，忽略BOOT输入；新凭证连接完成配置并确认原conversation后才发ready。连接中按住的BOOT必须松开重按，不提交半句。旧固件保持原来的断线恢复路径。

`caption_pages`启用下列元数据，均带非零uint32 `generation`和uint32 `turn`：

| op | 字段 |
|---|---|
| `caption_catalog` | `available`, `responding`, `truncated` 布尔值 |
| `caption_read` | `request`:非零uint32；`version`:uint32；`page`:uint16 |
| `caption_page` | 同请求身份；成功时`version`非零、`pages`、`text`、`responding`、`truncated`；失败时`error`只允许`unavailable/stale/busy` |

`version=0,page=0`打开新快照；后续请求使用返回版本。一次只有一个请求，设备1秒限流、3秒超时，重试只读页面。返回或新轮后晚到页被丢弃。Mac最多保存当前正文24KiB和一个24KiB冻结快照，按原生item替换去重；清理取消／断线时释放，不写磁盘。保留换行与tab，移除NUL及其他控制字符，拒绝损坏UTF-8。页面正文最多300字节，JSON编码后整帧仍不超过1024字节。

旧`caption`继续用于主屏与旧固件。普通快照隐藏主屏正文、阅读正文／段号／导航及配对码；公开画面只用固定例文。

原生`async_update.add`没有已证实的响应关联标识；发送后将会话视为存在无标签异步响应的可能。跨越到下一次BOOT前安全重建，可能需要显式重试。不能把迟到正文或音频归给新问题。

## 可选当轮情绪（v0.2开发分支）

双方协商`companion_mood`后，固件将实际BOOT的type2纯文本`press`替换为单条JSON：`{"op":"press","generation":7,"turn":3,"mood":"warm"}`。`generation/turn`均为非零uint32，JSON turn必须等于外层帧turn，generation必须属于当前连接；ready且turn递增时才开始录音。mood仅允许`neutral/warm`，其他字段不参与指令。未协商此能力的旧端继续纯文本press；release/cancel/credit不变。

固件在实际录音开始时冻结衰减后的暖意，随后以同代同轮LISTEN快照编码，不传触点、历史、文本或任意提示。Mac只为当前有效真人轮次的response.create附加完整原instructions与固定风格建议，不修改session工具/声音，不写入用户或系统历史。工具后同轮续答保留该建议，新BOOT、短输入、取消、关闭与新idle socket清除。异步结果仍遵守现有不确定响应隔离，不据情绪解除任何门禁。

只读诊断的`companionMoodCapable`为布尔值，`lastCompanionMood`仅保留最后一个已成功开始轮次的有限值；它表示设备提交状态，不代表模型实际表达了相应情绪。
