# Grok Bot ESP32 Companion

[简体中文](README.md) · [Setup guide](docs/GETTING_STARTED.zh-CN.md) · [Validation](docs/VALIDATION.md)

A standalone companion-pet firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.43C** and a macOS bridge to an existing Grok Bot. The round-screen pet blinks, breathes and responds locally to touch. Hold BOOT to talk; release to receive the Bot's native voice, with its conversation context and task capabilities.

![Actual device screen](docs/pet-device-screen.png)

This independent community prototype is not affiliated with xAI, Grok Bot, Meta or Waveshare. It uses the desktop App's internal native-voice contract, tested against version 0.71.2. Normal App login and account permissions are required; compatibility with future versions is not guaranteed. No App, account or cloud service is distributed.

## Architecture and features

The ESP32 runs local pet behavior, LVGL UI, settings, audio, power policy and Wi-Fi. A Mac remains online to connect to the Bot. Device audio uses 16 kHz mono PCM over a paired private-LAN TLS connection; the Mac handles native-session formats. BLE is an authenticated, time-limited provisioning channel, not the voice transport. USB provisioning is the recommended first setup path.

There is no Gemini dependency or extra speech-to-text/text-to-speech service in normal conversation. Account and temporary native-session credentials stay on the Mac. Only a random LAN pairing key reaches the device. Task submissions are not automatically replayed after failures; persisted messages from the current call deduplicate task results.

## Requirements

- macOS, Node.js **22.19 or newer**, Xcode Command Line Tools, and an existing normally signed-in Grok Bot with working native voice.
- The exact Waveshare 1.43C board, 466×466 display, 8 MiB flash/PSRAM; other ESP32-S3 boards are not supported without porting.
- Python ≥3.11, ESP-IDF **6.0.1**, and a USB data cable for firmware work.
- An accessible private LAN; no public port forwarding.

## Try the Mac voice path first

```sh
git clone https://github.com/vliuyt/grok-bot-esp32-bridge.git
cd grok-bot-esp32-bridge
npm test
node src/cli.mjs bots
node src/cli.mjs serve --bot "Your existing Bot name"
```

Open <http://127.0.0.1:8788>, connect, allow microphone access, and hold the on-screen button to speak. macOS may ask you to allow normal access to `Grok Bot Safe Storage`. The bridge reads existing login state without changing it.

Proceed to firmware only after this works. The [step-by-step guide](docs/GETTING_STARTED.zh-CN.md) covers a private verified full-flash backup, target build, guarded application-only install, USB/BLE provisioning, foreground operation, optional login service and recovery.

This initial release is **source-only**. It does not include a precompiled `.bin`. Installation requires the documented Muse-compatible partition layout and default OTA selection. The installer refuses unsupported layouts and does not repartition a board. USB utilities are bundled; an adjacent private SDK checkout is not required.

## Evidence and current limits

A reference device completed ten recognized Chinese acoustic rounds, a 37.1-second continuous response, Wi-Fi rollback, USB provisioning, BLE service advertisement, silent task-result recovery with and without SSE, and a real two-hour quiet soak. Native first audio reached the Mac in 737–1097 ms (median 862.5 ms); actual device-audible latency was not independently measured.

The proactive-behavior clock is currently fixed to Shanghai time (UTC+8), with no timezone setting.

Full acoustic interruption/reconnection, physical touch feel, authenticated BLE provisioning and battery behavior still require acceptance. Public packaging changes were host-tested; a second-device clean install has not been tested. See [validation and limitations](docs/VALIDATION.md) for precise boundaries.

`npm test` uses mocks and local compiler checks, with no account access, flashing or acoustic playback. Three target-dependent tests are explicitly skipped until the firmware is built. Optional hardware benches are separate and can make sound.

## Contributing and licensing

Read [CONTRIBUTING](CONTRIBUTING.md), [SECURITY](SECURITY.md), [architecture](docs/宠物固件架构.md), [protocol](docs/PROTOCOL.md) and [roadmap](docs/ROADMAP.md). Never submit credentials, Wi-Fi passwords, paired device configuration, full-flash backups, recordings or personal transcripts.

Original code is Apache-2.0; retained Muse sources remain Apache-2.0 with their original notices. The Unifont-derived font is distributed separately under OFL-1.1. See [third-party notices](THIRD_PARTY_NOTICES.md). Trademarks are not licensed by this project.
