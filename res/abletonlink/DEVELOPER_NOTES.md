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
5. Schedule quantized launch on the next Link beat.

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
- `AbletonLink::onCallbackEnd()` releases the cached audio session state.

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
| `quantized_launch` | yes | Momentary command to start Link transport and synced Mixxx decks on the next Link beat. |
| `num_peers` | no | Number of other Link peers. |
| `bpm` | no | Link session tempo. |
| `beat_distance` | no | Current Link beat phase within `quantum`. |
| `quantum` | no | Link phase quantum used by Mixxx. Currently `1.0` beat. |
| `playing` | no | Link session playing state. |
| `next_beat_time_micros` | no | Link clock time for the next beat. |
| `quantized_launch_time_micros` | no | Scheduled launch time, or `0` when no launch is pending. |

Status controls are read-only and intentionally reject external writes. Tests
cover this so controller mappings, skins, and scripts can rely on these values
being observations, not competing command inputs.

## Start/Stop Sync Versus Launch

`start_stop_sync_enabled` and `quantized_launch` are intentionally separate.

Start/Stop Sync opts Mixxx into the optional Link transport feature. When
enabled, Mixxx publishes synced deck play/stop state to Link and follows Link
transport changes from peers.

Launch is a momentary action. It schedules Link transport and synced Mixxx decks
to start on the next Link beat. Launch is ignored unless Link is enabled,
Start/Stop Sync is enabled, and at least one synchronized primary deck exists.
This avoids hidden pending launch state when there is no valid Mixxx deck target.

## External Peer Test

Most Link tests are self-contained. One optional test validates real Link peer
discovery and peer churn:

```powershell
$env:MIXXX_LINK_PEER_EXE = "C:\path\to\mixxx-link-peer.exe"
build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured
```

If `MIXXX_LINK_PEER_EXE` is not set, the test skips. This keeps normal test runs
portable while still allowing maintainers to exercise real Link discovery with
LinkHut, a local peer harness, or another compatible helper.

## Current Known Limitations

### One-Beat Quantum

Mixxx currently exposes a one-beat Link quantum. This supports tempo and beat
phase sync reliably, and it makes Launch start on the next beat. It does not
claim bar-level or phrase-level launch.

This should not be "fixed" by hard-coding a four-beat quantum. Mixxx does not
currently model a shared bar phase or time signature in the sync engine. A
future feature could add a user-selectable launch quantum, but it should be a
deliberate UI/control design with tests for downbeat expectations.

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
