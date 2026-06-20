# Ableton Link developer notes

These notes are for reviewers and future maintainers of Mixxx's Ableton Link
integration.

## Architecture

`AbletonLink` is a `Syncable` owned by `EngineSync`. It represents the shared
Link session as a Mixxx sync participant, but it is not an audio deck and it is
not audible.

Primary responsibilities:

1. Join or leave the Link session.
2. Publish Link session state through `[AbletonLink]` controls.
3. Exchange tempo and beat phase with `EngineSync`.
4. Optionally publish and follow Link Start/Stop Sync transport state.
5. Schedule quantized launch on the selected Link launch quantum.

`EngineSync` remains the owner of deck sync behavior. Link transport actions go
through `EngineSync::setLinkTransportPlaying()`, which only starts or stops
synchronized primary decks. Manual/unsynced decks are intentionally left alone.

## Threading Model

Ableton recommends capturing and committing session state from the audio
callback for best timing accuracy. The Mixxx integration follows that pattern:

- `AbletonLink::onCallbackStart()` captures Link audio session state.
- Tempo and beat phase are published to `EngineSync` from the callback path.
- Pending Start/Stop Sync state is stored atomically and consumed from the
  callback.
- The captured audio session state remains available through final main-output
  publishing, then is overwritten on the next callback.

The Link callbacks for peer count and Start/Stop Sync arrive from Link-managed
threads. They use `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` and a
guarded `QPointer<AbletonLink>` so object lifetime stays on the Qt side.

The audio callback path must stay real-time friendly. Avoid adding allocations,
blocking calls, locks, or Qt event-loop dependencies there.

## Public Controls

The public integration surface is the `[AbletonLink]` control group:

| Control | Writable | Meaning |
| --- | --- | --- |
| `sync_enabled` | yes | Requested Link session enable state. |
| `enabled` | no | Effective Link engine state. |
| `start_stop_sync_enabled` | yes | Requested Link Start/Stop Sync state. |
| `link_audio_enabled` | yes | Requested Link Audio enable state, honored when built with Link Audio headers. Publishes `Mixxx Main` when active. |
| `link_audio_sources_enabled` | yes | Optional per-source Link Audio publishing for decks, samplers, microphones, auxiliary inputs, and preview decks. |
| `link_audio_receive_enabled` | yes | Requested state for mixing remote Link Audio into the local main output. |
| `link_audio_receive_muted` | yes | Mutes received Link Audio while keeping subscriptions active. |
| `link_audio_receive_gain` | yes | Gain multiplier for received Link Audio, clamped to `0.0` through `2.0`. |
| `link_audio_available` | no | Build capability for Link Audio. |
| `link_audio_num_channels` | no | Number of discovered Link Audio channels. |
| `link_audio_receive_num_channels` | no | Number of remote Link Audio channels currently subscribed for receive. |
| `link_audio_receive_active` | no | `1` while received Link Audio was mixed into the main output in the current callback. |
| `quantized_launch` | yes | Momentary command to start Link transport and synced Mixxx decks on the selected launch quantum. |
| `launch_quantum` | yes | Launch grid in beats. Supported values are `1`, `2`, `4`, and `8`. |
| `num_peers` | no | Number of other Link peers. |
| `bpm` | no | Link session tempo. |
| `beat_distance` | no | Current Link beat phase within `quantum`. |
| `quantum` | no | Link phase quantum used for Mixxx deck beat sync. Currently `1.0` beat. |
| `playing` | no | Link session playing state. |
| `output_latency_micros` | no | Measured callback-to-output latency used for Link timing compensation. |
| `host_time_filter_enabled` | no | `1` when Link timing is using Link's host-time filter for callback timestamps. |
| `next_beat_time_micros` | no | Link clock time for the next beat. |
| `next_beat_eta_micros` | no | Time until the next Link beat. |
| `quantized_launch_time_micros` | no | Scheduled launch time, or `0` when no launch is pending. |
| `quantized_launch_eta_micros` | no | Time until the scheduled launch, or `0` when no launch is pending. |

Status controls are read-only and intentionally reject external writes. Tests
cover this so controller mappings, skins, and scripts can rely on these values
being observations, not competing command inputs.

## Start/Stop Sync Versus Launch

`start_stop_sync_enabled` and `quantized_launch` are intentionally separate.

Start/Stop Sync opts Mixxx into the optional Link transport feature. When
enabled, Mixxx publishes synced deck play/stop state to Link and follows Link
transport changes from peers.

Launch is a momentary action. It schedules Link transport and synced Mixxx decks
to start on the selected Link launch quantum. Launch is ignored unless Link is
enabled, Start/Stop Sync is enabled, and at least one synchronized primary deck
exists. This avoids hidden pending launch state when there is no valid Mixxx
deck target.

Launch quantum is deliberately separate from `quantum`. `quantum` remains `1.0`
for normal deck beat sync because `EngineSync` expects one-beat phase values.
`launch_quantum` is a persistent writable control and may be set to `1`, `2`,
`4`, or `8` beats. Unsupported values are rejected and the previously confirmed
value remains active.

## External Peer Test

Most Link tests are self-contained. One optional test validates real Link peer
discovery and peer churn:

```powershell
$MixxxTest = Join-Path "build/x64__abletonlink" "mixxx-test.exe"
$env:MIXXX_LINK_PEER_EXE = "<absolute path to mixxx-link-peer.exe>"
& $MixxxTest --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured
```

If `MIXXX_LINK_PEER_EXE` is not set, the test skips. This keeps normal test runs
portable while still allowing maintainers to exercise real Link discovery with
LinkHut, a local peer harness, or another compatible helper.

On macOS and Linux the same test is enabled by exporting a local executable
path:

```shell
MIXXX_TEST="/absolute/path/to/mixxx-test"
MIXXX_LINK_PEER_EXE="/absolute/path/to/mixxx-link-peer"
export MIXXX_LINK_PEER_EXE
"$MIXXX_TEST" --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured
```

## Platform Portability

The core implementation is intended to be shared across Windows, macOS, and
Linux:

- Mixxx uses Ableton Link's default platform clock and `HostTimeFilter`, not a
  Windows-only timing source.
- Link Audio support is detected from headers with
  `__has_include(<ableton/LinkAudio.hpp>)`, so classic Link builds can still
  compile against older system packages.
- `ABLETONLINK` and `FETCH_ABLETONLINK` default to `OFF` so normal configure
  runs do not add a new required package or fetch from the network. Local
  developer builds that need Link 4.0 headers can set both to `ON`;
  distribution builds should keep fetching `OFF` and provide Ableton Link
  through the package manager.
- Engine publishing and receiving paths depend on Mixxx's normal audio callback
  buffers, not WASAPI, CoreAudio, ALSA, JACK, PulseAudio, or PipeWire APIs.
- QProcess-based external peer tests redirect output through
  `QProcess::nullDevice()`, which is portable.
- Platform-specific validation guidance is in `MANUAL_VERIFICATION.md`. Local
  helper scripts, generated checklists, and captured validation results should
  live outside the Mixxx source tree.

## Current Known Limitations

### Beat Sync Quantum

Mixxx keeps the Link quantum used for deck beat sync at one beat. This supports
tempo and beat phase sync reliably without sending bar-phase values into
`EngineSync`.

Selectable launch quantum is implemented through the separate `launch_quantum`
control. Larger launch quantum values use Link's quantum-aware beat/time APIs
for Launch scheduling, but they do not change normal deck beat phase sync.

### Timing Source

Mixxx uses Ableton Link's default platform clock, not `BasicLink` with the STL
clock. On Windows this selects Link's platform clock implementation. For the
normal audio callback path, Mixxx filters callback-entry timestamps with Link's
`HostTimeFilter`, then adds Mixxx's measured callback-to-output latency before
calling Link beat/time APIs. If a future backend supplies an exact output-buffer
system timestamp, the explicit timestamp overload can bypass the host-time
filter.

### Link Audio

Ableton Link 4.0 adds Link Audio for audio-channel sharing between peers. Mixxx
detects `LinkAudio.hpp` at compile time. When present, `AbletonLink` uses
`ableton::LinkAudio`, exposes Link Audio enable/availability controls, and
publishes the discovered channel count from Link's channels-changed callback.
It owns persistent `ableton::LinkAudioSink` instances for advertised channels.
`link_audio_enabled` publishes the final stereo main output as `Mixxx Main`.
`link_audio_sources_enabled` separately opts into publishing registered local
engine sources. Active deck, sampler, microphone, auxiliary, and preview-deck
buffers are published as pre-fader Link Audio channels with stable names such as
`Mixxx Deck 1` and `Mixxx Sampler 1`. Sink creation/destruction is handled from
the Qt/control side when controls or registered outputs change; the engine
callback only publishes to already-created sinks. The callback observes
immutable raw-pointer snapshots so it does not lock or copy shared ownership.
Replaced snapshots, sinks, and receive subscriptions are retired on the
Qt/control side after the active callback completes. When Link Audio headers
are absent, Mixxx still builds against older Link headers and the Link Audio
controls report unavailable.

Receiving Link Audio streams into Mixxx is implemented as an explicit,
default-off main-output input. `AbletonLink` subscribes to discovered channels
whose `peerName` does not match Mixxx's generated local peer name, buffers
incoming one- or two-channel int16 audio into fixed-size ring slots, and uses
each buffer's `beginBeats()`/`endBeats()` metadata to align receive mixing to
the local output timeline. Mixxx currently targets a fixed four-beat receive
latency, following Ableton's example renderer model, and mixes the result into
the main output with the user-selected receive gain unless muted. `EngineMixer`
publishes `Mixxx Main` before inbound receive mixing so received remote audio is
audible locally but is not immediately re-advertised through Mixxx's own main
Link Audio sink.

The current receive path is intentionally a mixer input, not a full routing
matrix. It does not expose per-remote-channel solo/monitor routing yet; those
controls can be layered above the existing subscription and buffer model if the
UX is accepted upstream.

### Network Discovery Is Environment Dependent

Ableton Link peer discovery depends on local network multicast/broadcast
behavior. Firewalls, VPNs, multiple adapters, sleep/wake, and network isolation
can affect peer visibility. Mixxx should recover cleanly, but it cannot
guarantee discovery through hostile network policy.

### Ableton Live DirectX/MME Button Visibility

On Windows, Ableton Live may hide its own Link button when Live is using
DirectX/MME audio drivers. This is an Ableton Live UI/driver behavior, not a
Mixxx limitation. Mixxx should still expose its Link controls whenever Mixxx was
built with Ableton Link support.

### Audio Capture Validation Depends On Routing

The Windows audio preflight can use Python `soundcard` WASAPI loopback to
capture the default Windows playback device. Some ASIO paths may bypass Windows
loopback. That affects validation evidence, not Link synchronization itself.

## Recently Fixed Edge Case

Quantized Launch used to be able to arm even when no synchronized primary deck
could be targeted. The current implementation guards against this through
`EngineSync::hasSynchronizedDeck()`.

Regression coverage:

- `AbletonLinkQuantizedLaunchIgnoredWhenNoDeckIsSynced`
- `AbletonLinkQuantizedLaunchIgnoresManualUnsyncedDecks`
- `AbletonLinkPendingLaunchClearsWhenNoDeckRemainsSynced`
