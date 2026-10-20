# 从零安装与恢复

先完成 Mac 语音验证，再处理设备。当前支持的是一块明确板型和已有兼容分区的开发设备；没有提供通用出厂固件转换器，也不更改分区表或启动器。

## 1. 准备 Mac

需要正常登录的 Grok Bot App、自己的已有 Bot、USB 数据线，以及互通的 2.4 GHz Wi-Fi。先确认 App 本身能和该 Bot 原生语音通话。

安装 Xcode Command Line Tools。使用 Homebrew 的用户可安装 Node.js 和 Python 3.11；已有符合版本的工具可直接使用。

```sh
xcode-select --install
brew install node python@3.11
node --version
python3.11 --version
```

Node.js 至少 22.19。此项目的运行代码只使用 Node 内置模块，没有 npm 运行依赖。

```sh
git clone https://github.com/vliuyt/grok-bot-esp32-bridge.git
cd grok-bot-esp32-bridge
npm test
node src/cli.mjs bots
node src/cli.mjs serve --bot "你的 Bot 名称"
```

`bots` 列出自己的 Bot；不要把包含账户关联信息的完整输出贴到公开 Issue。打开 <http://127.0.0.1:8788>，点击「连接」，允许正常钥匙串／麦克风权限。按住按钮说中文，松开听回答。建议先用耳机，完成三轮、上下文关联和一个简单计算测试。

结束时点击「挂断」，再在终端按 Control-C。**设备桥接与 Mac 试说同一时间只运行一种。**

## 2. 建立独立 USB 工具环境

在项目根目录执行：

```sh
mkdir -p .local
chmod 700 .local
python3.11 -m venv .local/hardware-venv
.local/hardware-venv/bin/python -m pip install -r requirements.txt
```

安装工具会读取 USB 身份以绑定私人备份；这些资料仅写入被忽略的 `.local/`。随包已经提供 Apache 许可串口模块，不需要再下载作者的 Muse SDK。

## 3. 理解可安装范围，先备份

公开安装工具只接受 ESP32-S3、8 MiB flash，并在安装时严格核对下列现有分区（分区表固定在 `0x10000`）。它不会改分区来适配不兼容设备：

| 名称 | 类型／子类型 | 起点 | 大小 |
|---|---|---:|---:|
| nvs | data/nvs | `0x11000` | `0xC000` |
| otadata | data/ota | `0x1D000` | `0x2000` |
| phy_init | data/phy | `0x1F000` | `0x1000` |
| ota_0 | app/ota_0 | `0x20000` | `0x3E0000` |
| ota_1 | app/ota_1 | `0x400000` | `0x3E0000` |
| prod_data | data/0x40 | `0x7E0000` | `0x1000` |
| prod_bak | data/0x41 | `0x7E1000` | `0x1000` |

安装流程只接受 CRC 校验正确、状态已确认且选择 **ota_0** 的有效启动记录；不会改写这些记录。全 `0xFF` 的空白记录也会拒绝，因为首次正常启动会初始化它，导致与升级前备份的引导状态不再相同。请先确认原应用已正常启动；保留已有备份，再在独立项目副本中为稳定状态建立新备份，不要覆盖原基线。待确认／损坏／回滚记录、活动 ota_1、其他布局、flash 加密或安全启动状态请由熟悉该设备的人另行评估，不要擦除数据来强行通过。本项目暂未提供其他布局的迁移工具。完全空白或厂商原始固件的板子可能不满足条件。

关闭串口监视器和其他占用 USB 的程序。确认手上的物理设备就是 Waveshare 1.43C；工具只能核对芯片、容量和 USB 身份，不能仅靠 ROM 识别所有外形相近的板子。

```sh
.local/hardware-venv/bin/python firmware/tools/pet_install.py backup --board 'Waveshare ESP32-S3-Touch-AMOLED-1.43C'
```

若接着多块设备，添加 `--port /dev/cu.usbmodemXXXX`，使用自己电脑上实际端口。该命令会读完整 8 MiB 并在同一暂停会话中校验，生成：

- `.local/before-pet-full.bin`：完整私人备份，可能包含密码和配对资料。
- `.local/device-backup.json`：摘要、设备绑定及验证状态。

它不会覆盖已有基线。不要删除这些文件来重复备份；设备换了应使用独立项目副本。备份可在布局不兼容时保留，但安装仍会拒绝。将备份另外保存到自己的安全位置，勿上传 GitHub。

## 4. 安装 ESP-IDF 6.0.1 并构建

按 [Espressif 官方安装指南](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html)准备工具。一个可选的独立目录安装方式：

```sh
mkdir -p "$HOME/esp"
git clone --branch v6.0.1 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.0.1"
cd "$HOME/esp/esp-idf-v6.0.1"
./install.sh esp32s3
. ./export.sh
```

回到本项目目录，使用自己的实际路径：

```sh
.local/hardware-venv/bin/python firmware/tools/build_pet.py --idf "$IDF_PATH"
npm test
```

构建不会刷机。不要执行 IDF 通用输出中的 `idf.py flash` 或整套分区写入建议；本项目只使用后面的应用槽安装器。为避开含空格的路径，脚本将源码同步到临时目录中独属于此项目的工作区；只有带本工具标记的目录才允许复用。`--stage` 可指定其他临时目录。实际路径记录在 `.local/build-info.json`，目标依赖测试自动读取它；可用 `GROK_PET_STAGE`、`CJSON_SOURCE_DIR` 覆盖测试路径。

输出包括 `firmware/build/grok-pet-<摘要>.bin`、兼容命名的 `grok-pet.bin` 和 `docs/pet-firmware-manifest.json`。安装前会同时核对镜像、源码和依赖锁摘要；改源码后必须重新构建。这些本地产物不进入 Git。

## 5. 应用槽安装及配网

确保第 3 步的备份和当前 USB 设备匹配：

```sh
.local/hardware-venv/bin/python firmware/tools/pet_install.py install
```

安装再次核对分区与 OTA 元数据没有变化，仅写 `0x20000` 起的应用槽并回读校验，不整片擦除，不修改分区、启动器或 NVS。公开版通用安装工具尚无另一台真实设备的干净安装验收，请保留可用的恢复路径。

等设备完成启动后，在 Mac 前台启动设备桥接：

```sh
node src/cli.mjs device --bot "你的 Bot 名称"
```

此时会生成本机随机配对密钥 `.local/device.json`，不要分享它。用 Chrome 打开 <http://127.0.0.1:8788/setup>，输入 Wi-Fi 名称和密码，选择 USB 配置并选择正确端口。页面通过同源令牌取得配对信息，不将密码放进网址或浏览器存储。

错误网络会恢复原配置，获取地址且保存成功后才提交新配置。设备连接页分别显示 Wi-Fi、Mac、原生语音；Wi-Fi 已连接不等于语音已就绪。

BLE 也使用同一配置验证：从设备连接页开启最长五分钟的配网窗口，在支持 Web Bluetooth 的浏览器选择设备，输入屏幕六位配对码完成认证。系统支持和提示可能不同；认证配网仍待完整人工验收，USB 是首版推荐路径。显式关闭窗口或按 BOOT 会关闭蓝牙配网。

## 6. 日常操作和夜间安静

- 按住实体 BOOT 说话，松开提交；播放中再次按住会先清除旧声音。屏幕下方显示 BOOT 操作提示，不提供触屏录音按钮。单次录音上限 55 秒，过短按下不提交。
- 点击或横向轻抚圆脸会回应；从底部上划或点底部短横打开设置。
- 下一版开发分支已加入「通话字幕」的完整／简洁／关闭设置：完整可滚动查看当前最多320字节片段，简洁显示首句且最多两行，关闭隐藏正文并保留操作提示。旧设置兼容、保存失败和旧轮次拒绝已做主机测试；设备重启保存及触摸滚动仍待验收。可先运行 `npm run preview` 打开 <http://127.0.0.1:8877/> 无声体验，详见[字幕说明](VOICE_INPUT.md)。发布版v0.1.1尚不包含这些新功能。
- 调整设备自己的音量／亮度／扬声器，Mac 系统音量不会代替设备扬声器音量。
- 主动行为时钟当前固定上海时区（UTC+8），还没有地区设置。安静陪伴默认开启。主动招呼目前是本地无声表现，不会自动开启麦克风；其真实白天表现仍待验收。
- USB 闲置：5 分钟降亮度、15 分钟困倦、30 分钟熄屏。电池：90 秒困倦、120 秒熄屏并暂停网络和音频。电池实测与续航未完成。
- 状态面板「挂断」暂停接入，「连接」恢复；程序重启后暂停不保留。夜间要持续停用，请停止服务。

## 7. 可选登录自动运行

先确认前台设备通话与 USB 配网成功，按 Control-C 停止前台。然后：

```sh
python3 scripts/install-service.py --bot "你的 Bot 名称" --start
```

安装器解析当前 Node 的绝对路径，复制 `src/` 和局域网配置到 `~/Library/Application Support/GrokPetBridge/`，写入自己的登录服务 `io.github.grokpet.bridge`。`--node /实际/node/路径` 可指定 Node；无 `--start` 时只安装，不立刻启动。安装前没有 `.local/device.json` 会安全拒绝；不会留下无法重试的半成品。

双击「启动设备桥接.command」启用自动运行；双击「停止设备桥接.command」停用并禁止下次登录自动启动。未安装服务时，启动脚本在终端前台运行，并询问 Bot 名称；关闭窗口即结束。

运行副本不会自动跟随 Git 更新。升级时先停止服务，再保留旧运行副本与密钥，按新版本迁移说明更新；安装器遇到已有安装会拒绝覆盖。

如需移除该可选服务，先执行停止脚本，再只删除 `~/Library/LaunchAgents/io.github.grokpet.bridge.plist` 和 `~/Library/Application Support/GrokPetBridge/`。项目 `.local/` 中的私人备份继续由你保留。

## 8. 恢复升级前应用

停止设备桥接，连接同一物理设备并保留完整私人备份：

```sh
bash 停止设备桥接.command
.local/hardware-venv/bin/python firmware/tools/pet_install.py restore
```

恢复不依赖当前应用能启动；它核对 USB 身份和完整备份摘要，读取并检查当前引导元数据，然后只恢复原 `0x20000..0x400000` 应用槽。如果元数据或设备身份变化会拒绝，工具不提供整片擦除／完整 flash 强制回写入口。若串口不存在，需用数据线物理重连；软件无法替代接线。

## 常见问题

| 现象 | 检查方向 |
|---|---|
| 等待 Grok／原生语音未就绪 | 先确认 App 正常语音、Mac 正常登录；区分 Wi-Fi、Mac TLS、原生会话三层 |
| `LAN_ADDRESS_AMBIGUOUS` | 当前存在多个符合条件的 `en*` IPv4 地址；先确认要使用哪个本地接口。本版没有自动猜测或专门的接口选择参数，不要盲目改系统 DNS／代理 |
| `PARTITION_LAYOUT_UNSUPPORTED` / `OTA_SELECTION_UNSUPPORTED` / `OTA_RECORD_NOT_CONFIRMED` | 现有引导布局不在公开安装支持范围；保留备份，不要擦除或改表来绕过检查 |
| `BOOT_METADATA_CHANGED` | 自备份后分区或 OTA 数据变化；停止刷写并核对设备历史 |
| `RUN_FOREGROUND_DEVICE_SETUP_FIRST` | 先启动一次 `device --bot`，完成前台配网与验证后再装服务 |
| 钥匙串／登录过期 | 在 Grok Bot App 正常登录，再重启桥接；不要复制令牌进配置文件 |
| 无声／声学回声 | 核对设备扬声器开关和音量；Mac 试说使用耳机，避免声音回送 |
| 任务没回流 | 查看 Bot 内任务状态；不要自动重提。语音通道与任务通道可分别失败 |
| 超长回答中止 | 未播放 PCM 缓存上限约 180 秒；可要求 Bot 分段回答 |
| 找不到板／USB 超时 | 检查数据线、启动完成、其他串口占用和自己实际的端口；当前一次只操作一块板 |
