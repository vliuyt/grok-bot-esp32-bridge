# Changelog

## Unreleased — v0.2.0 work in progress

- Recover a fully settled native idle expiry with one fresh credential and confirmed original conversation, preserving paired device TLS. Negotiate `native_resume`, restart the firmware's bounded native-ready deadline, require a fresh BOOT, and retain call-wide response/tool fences. Preserve authentication notices and reject retired diagnostic frames. 191 local checks, independent C/JS reviews, full backup and application-slot readback pass; real idle recovery and physical first replies remain separately tracked in `docs/idle-first-use-validation.json`.
- Diagnose the native idle close using fixed error signals and input-idle timing, with bounded detached snapshots and no error text, audio or credentials. Three silent reproductions included a server-initiated 1001 close 2,168 ms after the error; the earlier ASR-timeout hypothesis was rejected.

- Require release and a fresh BOOT press after a connection recovers; never resume a partial utterance. Show explicit not-recording/release guidance and accept authentication notices only for the current not-ready connection. Idle native service errors remain unresolved.
- React locally on touch-down and movement, follow the finger with bounded gaze, relax during strokes, gently dodge three taps within a total 900ms window, and stretch briefly on waking. Cancel contact on page changes, BOOT and lost contact; preserve voice priority and existing petting warmth. Include 900/901ms and fast-stroke regressions.
- Smooth eyes and body by elapsed time in the UI thread; settle speech motion after actual playback-level updates stop. Local157/157 passed with zero skips; independent reviews found one important tap-window defect and one minor fast-stroke defect, both reproduced and fixed. See `docs/companion-polish-validation.json` for target, installation and physical acceptance boundaries.
- Route exact device arithmetic through the existing Bot task capability with code/calculator verification and result-wait guidance. A saved native call confirmed correct recognition of123×45 but a direct5400 answer with no tool call. One bounded silent native task returned5535 and generated its native audio; local156/156 and independent reviews passed. The user then confirmed a clear correct5535 from the device, with one tool call and one result return. This remains prompt guidance; continuous acceptance and existing async-task rebuild limits are retained; two subsequent same-call context rounds passed.
- Add device-only peak-aware input gain after 24 kHz resampling: up to 4× for quiet speech, immediate peak reduction, 500 ms release time constant and a fresh envelope per BOOT. Preserve hardware gain, playback volume and raw-level telemetry; retain only numeric processed-input levels. A repeated low-level fixed-audio recognition failure improves with this module, with five clear baseline rounds followed later by a separate successful ten-round physical run; earlier failed trials remain retained. Local regression: 156/156, zero skips.
- Add numeric RMS/peak/sample-count metering of resampled native downlink audio without retaining or changing PCM. Preserve a human-reported silent-speaker failure even though native completion and codec frame consumption were observed. Local regression: 152/152, zero skips; Mac-only diagnostic update, no firmware or loudness change.
- Add bounded, content-free native voice event diagnostics in Mac memory, including the last disconnected session. Preserve an observed 2026-10-22 unanswered human trial as failed; diagnostics do not change audio behavior or establish a fix. Expanded local regression: 149/149, zero skips.
- Follow up a failed physical swipe trial: recognize moderate rightward motion before release, tolerate a slight diagonal and repeated release coordinates, preserve slider/vertical-scroll exclusions, and allow at most one back navigation per finger. The user confirmed the prior volume and return-button improvements; the subsequent reference-device settings-page trial confirmed that right swipes now basically return every time. This is qualitative feedback, not a measured success rate or full reader/voice acceptance.
- Remap the on-screen volume percentage to approximate signal amplitude while preserving stored codec settings, mute, maximum output and upgrade loudness. Enlarge return, footer and reader targets, add immediate pressed/disabled feedback, and support right-swipe back without stealing slider drags or vertical reading scroll. Cancel stale held controls on page changes. Physical usability acceptance remains separate from real LVGL pointer tests.
- Explain short input, delayed/failed/empty replies, authentication needs and disconnections with controlled per-turn feedback. Cancel stale tools/audio and coordinate device watchdogs with real progress.
- Renew an expired gateway route once for safe reads/presence, including verified 404 recovery. Coalesce renewal and never replay uncertain task or call-record writes. This does not establish the cause of the earlier unanswered voice attempt.
- Retain capped petting warmth for sixty seconds while preserving voice, error and sleep priorities; no extra audio or NVS writes.
- Add device-only concise Chinese chat guidance while preserving the Bot persona, voice, tools and explicit long-answer requests.
- Keep at most 24 KiB of current native reply text plus one 24 KiB frozen snapshot in Mac memory. Negotiate optional caption pages up to 300 UTF-8 bytes over unchanged 1024-byte metadata frames.
- Add large round-screen page navigation, bounded one-at-a-time reads, explicit page retry, stale reply rejection and reader-content snapshot redaction. Keep BOOT as the only voice input.
- Add real production LVGL paging/privacy checks, C reader/parser tests and TLS reader integration. Initial expanded local suite: 143/143, zero skips; target build fits the original slot. Final acceptance is tracked in `docs/companion-evolution-validation.json`.


- Add curious tap and confused connection/timeout feedback while preserving classic pink appearance and BOOT voice priority.
- Keep thinking visible throughout waiting; cancelled or timed-out turns reject late audio.
- Add a silent interactive expression preview with frames exported from the firmware core and shared face geometry.
- Add full/compact/off caption modes, UTF-8-safe sentence clipping, independent compatible NVS storage, and stale-caption rejection on cancellation or disconnection.
- Preview caption modes with fixed public examples generated by the firmware policy; full captions scroll in a separate area and BOOT instructions stay visible.
- Rebuild the round-screen UI with 28px home captions, a 30px reading page and larger settings targets; use grayscale Noto Sans SC glyphs with the original character fallback.
- Serialize LVGL software rendering for compressed glyph decoding and keep reading interactions awake.
- On 2026-10-21, the reference-device user confirmed clearer text, easier settings targets, native voice replies, real captions, reading scroll and return. A silent reboot retained settings and restored the connection. See `docs/readability-device-validation.json` for the image identity and scope.
- The initial unanswered voice attempt has no established root cause. The user confirmed a full67.18-second long answer with paged reading, active BOOT interruption with clear new audio and no old audio returning, and natural petting afterglow. An expected post-task rebuild and subsequent new questions passed. A fresh same-session ten-round run then passed with clear human-confirmed audio, completed recognition/replies and all1,020,800frames consumed, without reconnect/fault/drop. Precise acoustic timing, installable release packaging and second-device acceptance remain pending; idle native service errors and short unanswered inputs are retained. These are development-branch changes, with no new firmware release.

## 0.1.1 — 2026-10-19

- Keep voice on the physical BOOT button: hold to record, release to submit, hold again to interrupt.
- Remove the experimental touch voice button after real-device feedback; touchscreen remains for petting and settings.
- Display connection/listening/interruption instructions independently of conversation captions.
- Accept CRC-validated, confirmed ota_0 boot records for application-only upgrades; never rewrite boot metadata. Reject erased OTA records, whose first-boot initialization would otherwise invalidate safe recovery checks.

- Reference-device update, sanitized final-screen capture and 82 host tests passed. The user verified voice on the earlier touch/BOOT build; the final BOOT-only UI build was checked silently.

## 0.1.0 — 2026-10-19

Initial public source release.

- Standalone Waveshare 1.43C pet firmware, local behavior, classic pink UI, audio and power policies.
- Existing-Bot native macOS voice bridge and task-history recovery.
- Wi-Fi discovery, private-LAN pairing, authenticated BLE and USB provisioning.
- Bundled USB helpers, guarded per-device backup/install/restore, portable optional login-service setup.
- Chinese newcomer guide, English overview, architecture, protocol, tests and explicit acceptance gaps.
- Clean public history; no account/device data, recordings, private backup manifests or precompiled firmware.
