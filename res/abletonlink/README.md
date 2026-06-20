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
When FetchContent is used, Mixxx fetches Ableton Link 4.0 so LinkAudio headers
are available. Existing system packages may still be older; in that case Mixxx
builds without LinkAudio and reports `link_audio_available` as `0`.

## User interface

The Sync preferences page contains Ableton Link settings and live session
status. It shows whether Link support is available in the current build, whether
Link is effectively running, the number of peers, session BPM, beat phase,
beat quantum, selected launch quantum, and the session playing state.

Default skins also expose compact Link controls in the main toolbar or mixer
area:

- `Link`: enables or disables joining the Link session.
- peer count: shows the number of other Link peers.
- BPM: shows the current Link session tempo.
- `Start/Stop`: enables or disables Link Start/Stop Sync.
- `Launch`: starts Link transport and synced Mixxx decks on the selected Link
  launch quantum.

## Controls

The integration intentionally uses regular Mixxx controls. This keeps the
feature available to skins, controller mappings, scripts, tests, and future UI
or automation work without coupling those callers directly to the Link engine
implementation.

Public controls in the `[AbletonLink]` group:

- `sync_enabled`: requested Link session enable state.
- `enabled`: effective Link engine state.
- `start_stop_sync_enabled`: requested Link Start/Stop Sync state.
- `link_audio_enabled`: requested LinkAudio enable state. This is honored only
  when Mixxx is built with Ableton Link 4.0 or newer headers.
- `link_audio_receive_enabled`: requested state for mixing remote LinkAudio
  channels into Mixxx's main output. This is honored only when LinkAudio is
  available and enabled.
- `link_audio_receive_muted`: mutes received LinkAudio while keeping
  subscriptions active.
- `link_audio_receive_gain`: gain multiplier for received LinkAudio mixed into
  the main output. Supported range is `0.0` to `2.0`; default is `1.0`.
- `link_audio_available`: effective build support for LinkAudio.
- `link_audio_num_channels`: number of discovered LinkAudio channels.
- `link_audio_receive_num_channels`: number of remote LinkAudio channels Mixxx
  is currently subscribed to for receive.
- `link_audio_receive_active`: `1` while received LinkAudio is actively being
  mixed into the main output.
- `quantized_launch`: momentary command to start Link transport and synced Mixxx
  decks on the selected launch quantum. Link and Start/Stop Sync must be
  enabled.
- `launch_quantum`: requested launch grid in beats. Supported values are `1`,
  `2`, `4`, and `8`.
- `num_peers`: current number of other Link peers.
- `bpm`: current Link session tempo.
- `beat_distance`: current Link beat position.
- `quantum`: Link phase quantum used for Mixxx beat sync. This remains `1.0`
  beat so normal deck sync receives beat-phase values in the expected range.
- `playing`: Link session playing state.
- `output_latency_micros`: measured callback-to-output latency used to
  compensate Link timing.
- `host_time_filter_enabled`: `1` when Link timing is using Link's host-time
  filter for audio callback timestamps.
- `next_beat_time_micros`: Ableton Link clock time of the next beat.
- `next_beat_eta_micros`: time until the next Link beat.
- `quantized_launch_time_micros`: scheduled quantized launch time, or `0` when
  no launch is pending.
- `quantized_launch_eta_micros`: time until the scheduled quantized launch, or
  `0` when no launch is pending.

`sync_enabled`, `start_stop_sync_enabled`, `link_audio_enabled`,
`link_audio_receive_enabled`, `link_audio_receive_muted`,
`link_audio_receive_gain`, `quantized_launch`, and `launch_quantum` are
writable. The status controls are read-only observations of the active Link
session.

Mixxx uses Ableton Link's default platform clock and filters callback-entry
timestamps with Link's `HostTimeFilter` before adding measured output latency.
This follows Link's guidance for audio APIs that do not provide an exact system
timestamp for the output buffer.

Ableton Link 4.0 adds LinkAudio for peer audio-channel sharing. When Mixxx is
built with headers that provide `LinkAudio.hpp`, it uses `ableton::LinkAudio`,
can enable/disable LinkAudio, publishes the final stereo main output as a
LinkAudio sink named `Mixxx Main`, publishes active local engine sources as
pre-fader LinkAudio sinks, can receive remote LinkAudio channels into the main
output with explicit enable/mute/gain controls, and publishes discovered
LinkAudio channel counts.
Per-source channels use stable names such as `Mixxx Deck 1`,
`Mixxx Sampler 1`, `Mixxx Microphone 1`, `Mixxx Auxiliary 1`, and
`Mixxx Preview Deck 1`. When built with older Link headers, the LinkAudio
controls remain available but report unavailable/disabled. Receiving remote
LinkAudio streams is intentionally disabled by default. Mixxx subscribes only to
channels from other LinkAudio peers and publishes its own main output before
inbound receive mixing so received audio is not immediately re-advertised as
`Mixxx Main`.

## Recommended manual test

1. Build Mixxx with `ABLETONLINK=ON`.
2. Start Ableton Live or another Link-capable application.
3. Enable Link in the other application.
4. Start Mixxx and enable `Link` from either the skin or Sync preferences.
5. Confirm the peer count is at least `1`.
6. Load a track with a reliable beatgrid.
7. Enable deck Sync and press play.
8. Change the session tempo from the other Link application.
9. For release validation, record Mixxx and LinkHut click-like output through
   loopback and confirm beat onsets align within 3 ms.

On Windows, Mixxx should expose its Ableton Link controls whenever it was built
with Ableton Link support. If Ableton Live does not show its own Link button,
check Live's audio preferences: Live may hide Link when Live is using DirectX or
MME. Switch Live to an ASIO device from your audio interface driver, or install
and select ASIO4ALL.

Expected result: Mixxx follows the Link session tempo and keeps synced decks
phase-aligned while deck Sync is enabled. If Start/Stop Sync is enabled, Link
session transport state should also be reflected through the `playing` control.
Pressing `Launch` with Link and Start/Stop Sync enabled should schedule synced
Mixxx decks to start on the selected Link launch quantum instead of starting
immediately.

An Ableton Live smoke-test set is included at:

```text
res/abletonlink/templates/example_ableton_mixxx_link_template_set.als
```

Manual release verification steps for Ableton Live, alternate Link peers,
Windows audio backend behavior, macOS/Linux validation, network churn, audio
device churn, and soak testing are documented in:

```text
res/abletonlink/CROSS_PLATFORM_VALIDATION.md
res/abletonlink/MANUAL_VERIFICATION.md
```

Developer/reviewer notes and an upstream PR validation packet are documented in:

```text
res/abletonlink/AUTOMATED_VALIDATION_RESULTS.md
res/abletonlink/DEVELOPER_NOTES.md
res/abletonlink/UPSTREAM_PR_PACKET.md
```

## External peer test

Most Link tests are self-contained. The optional external-peer test requires a
local Link peer executable:

```powershell
$MixxxTest = Join-Path "build/x64__abletonlink" "mixxx-test.exe"
$env:MIXXX_LINK_PEER_EXE = "<absolute path to mixxx-link-peer.exe>"
& $MixxxTest --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured
```

On macOS and Linux:

```shell
MIXXX_TEST="/absolute/path/to/mixxx-test"
MIXXX_LINK_PEER_EXE="/absolute/path/to/mixxx-link-peer"
export MIXXX_LINK_PEER_EXE
"$MIXXX_TEST" --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured
```

If `MIXXX_LINK_PEER_EXE` is not set, that test skips. Other Link tests still
run normally.

## Current known limitations

- Mixxx uses a one-beat Link quantum for deck beat sync. Selectable Launch
  quantum is implemented separately for `1`, `2`, `4`, and `8` beat launch
  grids.
- LinkAudio-enabled builds publish Mixxx's final stereo main output as
  `Mixxx Main` and active local engine sources as pre-fader per-source channels.
  Inbound LinkAudio receive is implemented but disabled by default and mixed
  only into Mixxx's local main output when explicitly enabled.
- Link peer discovery depends on local firewall, VPN, multicast, and network
  adapter behavior.
- On Windows, Ableton Live may hide its own Link button with DirectX/MME. Mixxx
  should still expose Link controls whenever Mixxx is built with Link support.
- Automated audio capture evidence depends on local routing. ASIO paths may
  bypass Windows loopback capture.

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
