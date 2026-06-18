# Ableton Link integration

Mixxx can join an Ableton Link session so decks with Sync enabled follow the
shared session tempo and beat phase. Link support is optional at build time and
is exposed through regular Mixxx controls so skins, controller mappings, tests,
and automation can observe and control the session.

## Build options

Ableton Link support is controlled by these CMake options:

- `ABLETONLINK`: build Mixxx with Ableton Link support. Defaults to `ON`.
- `FETCH_ABLETONLINK`: download Ableton Link during configure if no system
  package is found. Defaults to `ON` when `ABLETONLINK` is enabled.

Configure without Link support with:

```shell
cmake -DABLETONLINK=OFF ..
```

Configure for distribution builds that must not download dependencies during
configure with:

```shell
cmake -DABLETONLINK=ON -DFETCH_ABLETONLINK=OFF ..
```

In that mode, CMake expects a system package exposing `Ableton::Link` through
`find_package(AbletonLink)`.

## User interface

The Sync preferences page contains Ableton Link settings and live session
status. It shows whether Link support is available in the current build, whether
Link is effectively running, the number of peers, session BPM, beat phase,
quantum, and the session playing state.

Default skins also expose compact Link controls in the main toolbar or mixer
area:

- `Link`: enables or disables joining the Link session.
- peer count: shows the number of other Link peers.
- BPM: shows the current Link session tempo.
- `Start/Stop`: enables or disables Link Start/Stop Sync.
- `Launch`: starts Link transport and synced Mixxx decks on the next Link beat.

## Controls

The integration intentionally uses regular Mixxx controls. This keeps the
feature available to skins, controller mappings, scripts, tests, and future UI
or automation work without coupling those callers directly to the Link engine
implementation.

Public controls in the `[AbletonLink]` group:

- `sync_enabled`: requested Link session enable state.
- `enabled`: effective Link engine state.
- `start_stop_sync_enabled`: requested Link Start/Stop Sync state.
- `quantized_launch`: momentary command to start Link transport and synced Mixxx
  decks on the next Link beat. Link and Start/Stop Sync must be enabled.
- `num_peers`: current number of other Link peers.
- `bpm`: current Link session tempo.
- `beat_distance`: current Link beat position.
- `quantum`: Link phase quantum.
- `playing`: Link session playing state.
- `next_beat_time_micros`: Ableton Link clock time of the next beat.
- `quantized_launch_time_micros`: scheduled quantized launch time, or `0` when
  no launch is pending.

`sync_enabled`, `start_stop_sync_enabled`, and `quantized_launch` are writable.
The status controls are read-only observations of the active Link session.

## Recommended manual test

1. Build Mixxx with `ABLETONLINK=ON`.
2. Start Ableton Live or another Link-capable application.
3. Enable Link in the other application.
4. Start Mixxx and enable `Link` from either the skin or Sync preferences.
5. Confirm the peer count is at least `1`.
6. Load a track with a reliable beatgrid.
7. Enable deck Sync and press play.
8. Change the session tempo from the other Link application.

On Windows, Mixxx should expose its Ableton Link controls whenever it was built
with Ableton Link support. If Ableton Live does not show its own Link button,
check Live's audio preferences: Live may hide Link when Live is using DirectX or
MME. Switch Live to an ASIO device from your audio interface driver, or install
and select ASIO4ALL.

Expected result: Mixxx follows the Link session tempo and keeps synced decks
phase-aligned while deck Sync is enabled. If Start/Stop Sync is enabled, Link
session transport state should also be reflected through the `playing` control.
Pressing `Launch` with Link and Start/Stop Sync enabled should schedule synced
Mixxx decks to start on the next Link beat instead of starting immediately.

An Ableton Live smoke-test set is included at:

```text
res/abletonlink/templates/example_ableton_mixxx_link_template_set.als
```

Manual release verification steps for Ableton Live, alternate Link peers,
Windows audio backend behavior, network churn, audio device churn, and soak
testing are documented in:

```text
res/abletonlink/MANUAL_VERIFICATION.md
```

## Test environment notes

The Ableton Link code does not require QML. Some existing Mixxx controller
screen mapping tests do require Qt QML runtime modules. If local `ctest` runs
fail only in controller-screen mapping tests with missing modules such as
`Qt5Compat.GraphicalEffects` or `QtQuick.Controls.macOS`, install the complete
Mixxx build environment or the corresponding Qt QML runtime packages for the
platform.

Those QML runtime failures are separate from Ableton Link and should be fixed in
the build environment or packaging layer, not by vendoring QML modules into the
Link feature patch.
