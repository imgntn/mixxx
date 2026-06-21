# Ableton Link Private Development Log

This file is a private development log for the Ableton Link integration branch.
It is intended as a human-readable record for the fork, not necessarily as an
upstream PR artifact.

Branch: `codex/ableton-link-upstream`  
Latest recorded commit: `ce3fb5e3f0 Fix Ableton Link Linux include dependency`  
Base used for this log: `upstream/main..HEAD`  
Recorded: 2026-06-20

## What We Built

### Core Ableton Link Integration

- Added an `AbletonLink` sync engine component owned by `EngineSync`.
- Joined and left Ableton Link sessions through Mixxx controls.
- Published effective Link state, peer count, session BPM, beat phase, quantum,
  transport state, next beat time, next beat ETA, output latency, and host-time
  filter status.
- Connected Link tempo and phase to Mixxx sync logic.
- Kept Mixxx deck sync quantum at one beat so existing `EngineSync` phase logic
  stays stable.
- Added Link Start/Stop Sync as a separate opt-in transport feature.
- Ensured Link Start/Stop Sync only affects synced Mixxx decks, not manual decks.
- Added quantized launch so Mixxx can start synced decks on the selected Link
  launch grid.
- Added selectable launch quantum values: `1`, `2`, `4`, and `8` beats.
- Added cancellation behavior for disabled Link, disabled Start/Stop Sync,
  unsynced decks, unloaded decks, and no-deck cases.
- Guarded transport actions when no synchronized deck target exists.
- Added Link timing based on callback entry time, Link's `HostTimeFilter`, and
  Mixxx output latency.

### Link Audio

- Added compile-time detection for Link Audio headers with
  `__has_include(<ableton/LinkAudio.hpp>)`.
- Kept classic Ableton Link builds working when Link Audio headers are absent.
- Added Link Audio availability and diagnostics controls.
- Added main-output Link Audio publishing as `Mixxx Main`.
- Added optional per-source Link Audio publishing for:
  - Decks
  - Samplers
  - Microphones
  - Auxiliary inputs
  - Preview decks
- Added stable Link Audio source names such as `Mixxx Deck 1`,
  `Mixxx Sampler 1`, and `Mixxx Preview Deck 1`.
- Added inbound Link Audio receive as an explicit, default-off main-output input.
- Subscribed only to remote Link Audio channels, excluding Mixxx's own local peer.
- Added receive gain and receive mute controls.
- Mixed inbound Link Audio using the remote buffer beat metadata and local Link
  timeline.
- Added `link_audio_receive_active` so the UI and tests can tell when receive
  audio is actually being mixed.
- Prevented received Link Audio from being re-advertised through `Mixxx Main` by
  publishing local main before inbound receive mixing.
- Fixed callback snapshot ownership so the audio callback reads raw immutable
  snapshots without shared ownership churn.
- Added retirement handling for old Link Audio snapshots, sinks, and receive
  subscriptions after callback completion.
- Guarded registry and input buffer races under publish/receive churn.
- Ensured disabling Link Audio also clears per-source publishing and receive.
- Fixed inbound Link Audio so it cannot repopulate `m_main` when the main output
  is disabled.

### Build System

- Added CMake option `ABLETONLINK`.
- Added CMake option `FETCH_ABLETONLINK`.
- Added system-package lookup for `AbletonLink`, `ableton`, `ableton-link`, and
  `ableton-link-dev`.
- Added optional Link 4.0 fetch path from Ableton's upstream repository.
- Kept normal configure runs review-friendly by defaulting both `ABLETONLINK`
  and `FETCH_ABLETONLINK` to `OFF`.
- Changed fetched Link consumption to include Ableton's target config without
  building Ableton's examples or test binaries.
- Preserved Mixxx's project-wide C++ standard after including Ableton's config.
- Added compatibility handling for CMake's `FetchContent_Populate` policy.
- Verified classic Link and Link 4 fetch builds on Windows.

### Preferences UI

- Added a Sync preferences page for Ableton Link.
- Added controls for:
  - Link enable
  - Start/Stop Sync
  - Link Audio enable
  - Link Audio per-source publishing
  - Link Audio receive
  - Link Audio receive mute
  - Link Audio receive gain
  - Launch quantum
- Added diagnostics for:
  - Build availability
  - Effective Link state
  - Peer count
  - BPM
  - Beat phase
  - Sync quantum
  - Launch quantum
  - Link transport playing state
  - Link Audio availability
  - Link Audio discovered channel count
  - Link Audio subscribed receive channel count
  - Link Audio receive activity
  - Output latency
  - Host-time filter state
  - Next beat ETA
  - Quantized launch ETA
- Made disabled Link Audio state clear dependent source/receive state in the UI.
- Added Sync preference icons and resource entries.

### Skin UI

- Added compact Ableton Link controls to default skins:
  - Deere
  - LateNight
  - Shade
  - Tango
- Added a Link button.
- Added peer-count display.
- Added session BPM display.
- Added Start/Stop Sync button.
- Added Quantized Launch button.
- Added shared visual styling for Link controls so they read as one cluster.
- Added tooltips for the Link controls.
- Widened the `Start/Stop` and `Launch` controls to reduce clipping and
  translation risk.
- Updated docs to use the visible `Start/Stop` label instead of the older `SS`
  shorthand.

### Controller And Scripting Surface

- Added `[AbletonLink]` controls to the controller picker menu.
- Added writable Link controls to `res/controllers/mixxx-controls.d.ts`.
- Added read-only Link status controls to `res/controllers/mixxx-controls.d.ts`.
- Added `[AbletonLink]` controls to `src/test/co_dumps/co_dump_inital.csv`.
- Kept read-only controls protected against external writes.

### Documentation

- Added `res/abletonlink/README.md`.
- Added `res/abletonlink/DEVELOPER_NOTES.md`.
- Added `res/abletonlink/MANUAL_VERIFICATION.md`.
- Documented build options and default values.
- Documented system-package and fetch-based build modes.
- Documented Link Audio header detection and classic Link fallback behavior.
- Documented public controls.
- Documented timing model and host-time filtering.
- Documented Link Audio publishing and receiving model.
- Documented Windows validation steps.
- Corrected the Windows note: the Ableton Link button may not appear in Ableton
  Live when Live is using DirectX/MME audio drivers; users may need ASIO drivers
  from their interface or ASIO4ALL. Mixxx should still show its Link controls
  whenever built/enabled.
- Documented cross-platform validation expectations for Windows, macOS, and
  Linux.

### Validation And Stress Testing

- Added focused Link tests to `EngineSyncTest`.
- Covered transport start and stop behavior.
- Covered synced-vs-unsynced deck behavior.
- Covered manual deck protection.
- Covered enabling Link before tracks are loaded.
- Covered joining Link while synced decks are already playing.
- Covered disabling and re-enabling Link.
- Covered Start/Stop Sync toggles.
- Covered quantized launch scheduling.
- Covered repeated quantized launch requests.
- Covered launch cancellation paths.
- Covered selected launch quantum.
- Covered invalid launch quantum rejection.
- Covered status control read-only behavior.
- Covered finite status values under tempo/control churn.
- Covered useful BPM extremes.
- Covered unloaded deck and no-synced-deck cases.
- Added chaos tests for Link controls and launch cancellation.
- Added Link Audio callback churn coverage.
- Added Link Audio publish snapshot churn coverage.
- Added optional external peer discovery test gated by `MIXXX_LINK_PEER_EXE`.
- Added Windows validation planning and checklist artifacts during development.
- Added Windows audio preflight and WASAPI loopback exploration in the helper
  work outside the final upstream target.
- Tested multiple-peer scenarios during Windows validation planning.

### Bugs And Review Issues Fixed

- Fixed the early "Sync on each deck makes nothing happen" path by building out
  actual Link session state and transport handling.
- Fixed missing implementation around Link Start/Stop Sync and quantized launch.
- Fixed unclear/ cramped skin controls with tooltips, shared styling, and wider
  control sizing.
- Fixed dependency behavior so configure does not fetch or require Link by
  default.
- Fixed fetched Link 4 builds compiling Ableton's own tests and examples.
- Fixed Link Audio source publishing state staying armed after Link Audio was
  disabled.
- Fixed inbound Link Audio being able to write into a disabled main output.
- Fixed Link Audio callback lifecycle tests so publish churn exercises callback
  completion.
- Fixed races around Link Audio source registry and input buffer access.
- Fixed shared ownership on the audio callback path.
- Fixed path/command documentation for cross-platform validation.
- Fixed Windows WASAPI preflight helper issues in the validation tooling.

## Commit Timeline

- `829b019c47` Add Ableton Link integration
- `1607c85e33` Harden Ableton Link release validation
- `ef8eb0d1d7` Add optional Ableton Link external peer stress test
- `d0a5a4a966` Document Windows Ableton Link validation plan
- `8504235c25` Add Windows Ableton Link validation checklist UI
- `a88bb65b9d` Add Windows validation preflight checklist generator
- `11e15b53e0` Add Windows audio preflight validation helper
- `21836c3722` Add WASAPI loopback audio preflight
- `aa418d89e3` Cover additional Ableton Link edge cases
- `8acedc4301` Guard Link launch without synced deck target
- `5e83366a89` Document Ableton Link upstream review notes
- `eb4a5ffdeb` Add Ableton Link diagnostics and LinkAudio support
- `5c03fe2f17` Publish Mixxx engine sources via LinkAudio
- `b9c440c3f8` Receive remote channels via LinkAudio
- `3c0d313158` Document Ableton Link automated validation
- `942a8357d0` Fix Windows WASAPI audio preflight
- `e7db2bf636` Document cross-platform Link validation
- `29ff482703` Make Link validation commands path portable
- `2052af40b2` Polish Ableton Link integration for review
- `91766ab955` Fix Link Audio publish churn and configure default
- `2840af7704` Guard Link Audio registry and input buffer races
- `122d8f2b74` Avoid shared ownership on Link Audio callback path
- `044cf094ab` Polish Ableton Link integration for review

## Latest Verification Snapshot

Classic Link build:

- Command: `cmake --build build/x64__abletonlink --parallel 8`
- Result: passed
- Focused tests: `build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link*`
- Result: 45 passed, 2 skipped
- Expected skips:
  - Link Audio snapshot churn when Link Audio headers are unavailable
  - External peer discovery when `MIXXX_LINK_PEER_EXE` is not set

Link 4 build:

- Command: `cmake --build build/x64__abletonlink4 --parallel 8`
- Result: passed
- Focused tests: `build\x64__abletonlink4\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link*`
- Result: 46 passed, 1 skipped
- Expected skip:
  - External peer discovery when `MIXXX_LINK_PEER_EXE` is not set

Static checks:

- Command: `git diff --check`
- Result: passed, with only Windows line-ending warnings from Git

## Upstream PR Shape

The branch currently contains many development commits because it records the
whole exploration path. For upstream, this can be squashed into one coherent
feature commit or a small curated stack, depending on reviewer preference.

Suggested squashed PR summary:

> Add Ableton Link tempo/transport integration with optional Link Audio support.
> Expose Link controls and diagnostics in preferences, skins, controller
> metadata, and tests. Support classic Link builds, Link 4 Link Audio builds,
> optional dependency fetching, quantized launch, per-source Link Audio
> publishing, and default-off remote Link Audio receive.

## Things To Remember Before Opening Upstream PR

- Decide whether to include this private devlog. It is probably better kept out
  of the upstream PR.
- Re-run the focused Link test suite after the final squash.
- Treat James's macOS build and real Ableton Live verification as the final
  release gate before opening the upstream PR. The Mac pass should cover a clean
  Link-enabled build, the focused `EngineSyncTest.*Link*` tests, peer discovery
  with Ableton Live, Start/Stop Sync, selected launch quantum values, and Link
  Audio availability if using Link 4 headers.
- On Linux, verify both system-package and fetch-off behavior if possible.
- On Windows, re-check the Live audio-driver note: Live's Link button can depend
  on Live's selected driver mode, while Mixxx's own Link controls should remain
  visible when Mixxx is built with Link support.
