# Ableton Link integration

Mixxx can join an Ableton Link session so synced decks follow the shared
session tempo and beat phase. The integration is exposed through regular Mixxx
controls so skins, controller mappings, scripts, tests, and future UI work can
observe and control the session.

## Build options

Ableton Link support is controlled by these CMake options:

- `ABLETONLINK`: build Mixxx with Ableton Link support. Defaults to `OFF`.
- `FETCH_ABLETONLINK`: download Ableton Link during configure if no system
  package is found. Defaults to `OFF`.

Distribution builds should normally set `FETCH_ABLETONLINK=OFF` and provide
Ableton Link through the package manager:

```shell
cmake -DABLETONLINK=ON -DFETCH_ABLETONLINK=OFF ..
```

Local validation builds may explicitly use the fetch path for Link 4.0 headers:

```shell
cmake -DABLETONLINK=ON -DFETCH_ABLETONLINK=ON ..
```

Mixxx detects Link Audio support with `__has_include(<ableton/LinkAudio.hpp>)`.
Older system packages still build Link tempo/phase support, but Link Audio
controls report unavailable. Some existing build environments package Ableton
Link 3.x; Link Audio requires Link 4.0 or newer headers.

## User interface

The Sync preferences page contains Ableton Link settings and diagnostics:
effective Link state, peer count, BPM, beat phase, beat quantum, selected launch
quantum, transport state, output latency, host-time filter state, and Link Audio
availability/channel counts.

Default skins expose compact Link controls in the main toolbar or mixer area:

- `Link`: joins or leaves the Link session.
- peer count: shows the number of other Link peers.
- BPM: shows the current Link session tempo.
- `Start/Stop`: enables or disables Link Start/Stop Sync.
- `Launch`: starts Link transport and synced Mixxx decks on the selected launch
  quantum.

## Controls

Public controls in the `[AbletonLink]` group:

| Control | Writable | Meaning |
| --- | --- | --- |
| `sync_enabled` | yes | Requested Link session enable state. |
| `enabled` | no | Effective Link engine state. |
| `start_stop_sync_enabled` | yes | Requested Link Start/Stop Sync state. |
| `link_audio_enabled` | yes | Requested Link Audio enable state. When available, Mixxx publishes the main output as `Mixxx Main`. |
| `link_audio_sources_enabled` | yes | Optional per-source Link Audio publishing for decks, samplers, microphones, auxiliary inputs, and preview decks. |
| `link_audio_receive_enabled` | yes | Mix remote Link Audio channels from other peers into Mixxx's main output. |
| `link_audio_receive_muted` | yes | Mute received Link Audio while subscriptions remain active. |
| `link_audio_receive_gain` | yes | Gain for received Link Audio, clamped to `0.0` through `2.0`. |
| `link_audio_available` | no | Build capability for Link Audio. |
| `link_audio_num_channels` | no | Number of discovered Link Audio channels. |
| `link_audio_receive_num_channels` | no | Number of remote Link Audio channels Mixxx is subscribed to. |
| `link_audio_receive_active` | no | `1` while received Link Audio is mixed in the current callback. |
| `quantized_launch` | yes | Momentary command to start Link transport and synced Mixxx decks on the selected launch quantum. |
| `launch_quantum` | yes | Launch grid in beats. Supported values are `1`, `2`, `4`, and `8`. |
| `num_peers` | no | Number of other Link peers. |
| `bpm` | no | Link session tempo. |
| `beat_distance` | no | Current Link beat phase within `quantum`. |
| `quantum` | no | Beat-sync quantum. This remains `1.0` beat for Mixxx deck sync. |
| `playing` | no | Link session playing state. |
| `output_latency_micros` | no | Measured callback-to-output latency used for Link timing. |
| `host_time_filter_enabled` | no | `1` when Link's host-time filter is used for callback timestamps. |
| `next_beat_time_micros` | no | Ableton Link clock time of the next beat. |
| `next_beat_eta_micros` | no | Time until the next Link beat. |
| `quantized_launch_time_micros` | no | Scheduled launch time, or `0` when no launch is pending. |
| `quantized_launch_eta_micros` | no | Time until the scheduled launch, or `0` when no launch is pending. |

Status controls are read-only observations. External writes to them are
ignored.

## Timing model

Mixxx captures and commits Link session state from the audio callback. It uses
Ableton Link's default platform clock, filters callback-entry timestamps with
Link's `HostTimeFilter`, then adds Mixxx's measured callback-to-output latency
before calling Link beat/time APIs.

Quantized Launch uses Link's beat/time APIs on the selected `launch_quantum`.
The normal deck beat-sync quantum remains one beat so Mixxx receives phase
values in the range expected by `EngineSync`.

## Link Audio

When built with Link Audio headers, Mixxx uses `ableton::LinkAudio`.

`link_audio_enabled` enables Link Audio discovery and publishes the final stereo
main output as a sink named `Mixxx Main`. `link_audio_sources_enabled` is a
separate opt-in preference that also publishes active local engine sources as
pre-fader sinks with stable names such as `Mixxx Deck 1`, `Mixxx Sampler 1`,
`Mixxx Microphone 1`, `Mixxx Auxiliary 1`, and `Mixxx Preview Deck 1`.

Receiving remote Link Audio is default-off. When enabled, Mixxx subscribes only
to channels from other peers, uses each buffer's Link beat metadata to align
incoming audio to the local output timeline, applies the receive gain/mute
controls, and mixes the result into the local main output. Mixxx publishes its
own main output before inbound receive mixing so received audio is not
immediately re-advertised as `Mixxx Main`.

## Recommended manual smoke test

1. Build Mixxx with `ABLETONLINK=ON`.
2. Start Ableton Live or another Link-capable application.
3. Enable Link in the other application.
4. Start Mixxx and enable `Link`.
5. Confirm the peer count is at least `1`.
6. Load a track with a reliable beatgrid.
7. Enable deck Sync and press play.
8. Change the session tempo from the other Link application.
9. Enable Start/Stop Sync and verify transport follows between peers.
10. Press `Launch` with Link and Start/Stop Sync enabled and confirm synced
    Mixxx decks start on the selected launch quantum.

Expected result: Mixxx follows the Link session tempo, keeps synced decks
phase-aligned, and does not leave stale launch state after cancellation or Link
disable.

On Windows, Mixxx should expose its Ableton Link controls whenever it was built
with Ableton Link support. If Ableton Live does not show its own Link button,
check Live's audio preferences: Live may hide Link when Live is using DirectX or
MME. Switch Live to an ASIO device from your audio interface driver, or install
and select ASIO4ALL.

Additional release validation guidance is in `MANUAL_VERIFICATION.md`.
