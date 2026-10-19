# Changelog

## Unreleased

- Keep voice on the physical BOOT button: hold to record, release to submit, hold again to interrupt.
- Remove the experimental touch voice button after real-device feedback; touchscreen remains for petting and settings.
- Display connection/listening/interruption instructions independently of conversation captions.
- Accept CRC-validated, confirmed ota_0 boot records for application-only upgrades; never rewrite boot metadata. Reject erased OTA records, whose first-boot initialization would otherwise invalidate safe recovery checks.

## 0.1.0 — 2026-10-19

Initial public source release.

- Standalone Waveshare 1.43C pet firmware, local behavior, classic pink UI, audio and power policies.
- Existing-Bot native macOS voice bridge and task-history recovery.
- Wi-Fi discovery, private-LAN pairing, authenticated BLE and USB provisioning.
- Bundled USB helpers, guarded per-device backup/install/restore, portable optional login-service setup.
- Chinese newcomer guide, English overview, architecture, protocol, tests and explicit acceptance gaps.
- Clean public history; no account/device data, recordings, private backup manifests or precompiled firmware.
