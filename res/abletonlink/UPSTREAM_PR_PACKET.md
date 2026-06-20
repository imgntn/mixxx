# Ableton Link upstream PR packet

This file collects the reviewer-facing notes that should be copied or adapted
for the upstream pull request.

## Summary

This change adds Ableton Link support to Mixxx. When built with Link support,
Mixxx can join a Link session, follow and publish session tempo/beat phase, show
peer/session status through normal Mixxx controls, optionally participate in
Link Start/Stop Sync, and launch synced decks on a selectable Link launch
quantum.

## User-Visible Behavior

- New `[AbletonLink]` controls expose Link enable state, peer count, BPM, beat
  phase, beat-sync quantum, launch quantum, LinkAudio availability/channel
  discovery, LinkAudio receive enable/mute/gain/status, playing state, output
  latency compensation, host-time-filter status, next beat timing, and pending
  launch timing.
- Default skins expose compact Link controls for enabling Link, peer/BPM
  status, Start/Stop Sync, and Launch.
- Sync preferences expose Link availability, status, and launch quantum.
- Sync preferences expose LinkAudio publish/receive controls when LinkAudio is
  available.
- On Windows, Mixxx Link controls remain available whenever Mixxx is built with
  Link support, even if Ableton Live hides its own Link button for DirectX/MME
  driver configurations.

## Build Notes

Relevant CMake options:

- `ABLETONLINK=ON|OFF`
- `FETCH_ABLETONLINK=ON|OFF`

When `FETCH_ABLETONLINK=OFF`, the build expects a system package exposing
`Ableton::Link` through `find_package(AbletonLink)`.

## Reviewer Notes

- Link is modeled as a `Syncable`, but it is not a deck and is not audible.
- `EngineSync` remains responsible for choosing and controlling synced decks.
- Link audio session state is captured/committed from the audio callback path.
- Link timing uses Ableton Link's default platform clock, Link's
  `HostTimeFilter` for callback-entry timestamps, and Mixxx's measured
  callback-to-output latency so session state is evaluated at output time rather
  than raw callback entry time.
- Link-managed callbacks are marshaled to Qt with guarded object lifetime.
- Start/Stop Sync and Launch are intentionally separate:
  - Start/Stop Sync opts Mixxx into Link transport.
  - Launch is a momentary transport/deck start request quantized to the selected
    Link launch quantum.
- Launch is ignored unless a synchronized primary deck target exists.
- Deck beat sync keeps a one-beat Link quantum; selectable Launch quantum is a
  separate control so larger launch grids do not leak bar-phase values into
  `EngineSync`.
- When built with Ableton Link 4.0 headers, Mixxx uses `ableton::LinkAudio`,
  can enable LinkAudio channel discovery, publishes the final stereo main output
  as `Mixxx Main`, and publishes active local engine sources as pre-fader
  per-source sinks such as `Mixxx Deck 1`, `Mixxx Sampler 1`, and
  `Mixxx Microphone 1`. It can also receive remote LinkAudio channels into the
  local main output with explicit enable/mute/gain controls, skipping Mixxx's
  own advertised channels to avoid local feedback. Older Link headers remain
  supported and report LinkAudio unavailable.

Detailed architecture notes are in:

```text
res/abletonlink/DEVELOPER_NOTES.md
```

## Test Commands

Build:

```powershell
cmd /c "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake --build build\x64__abletonlink --target mixxx-test --config RelWithDebInfo --parallel 8"
```

Link-focused engine tests:

```powershell
build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link* --gtest_color=no
```

Observed local result:

```text
43 passed, 1 skipped
```

The skipped test is the optional external peer test when
`MIXXX_LINK_PEER_EXE` is not set.

External peer test:

```powershell
$env:MIXXX_LINK_PEER_EXE = "X:\mixxx_test\ableton-link-peer-tools\build\mixxx-link-peer.exe"
build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Observed local result:

```text
1 passed
```

For other machines, set `MIXXX_LINK_PEER_EXE` to the local path of LinkHut or a
compatible local Link peer harness.

## Windows Audio Preflight

Optional Windows audio capture preflight:

```powershell
python -m pip install --user soundcard
.\res\abletonlink\windows-audio-preflight.ps1
```

Observed local result with WASAPI loopback:

```text
WASAPI loopback: DELL S3422DWG (NVIDIA High Definition Audio)
signal-and-transients-detected
```

This validates that the local Windows output path can be captured for evidence.
It does not replace musical alignment checks for ASIO paths that bypass Windows
loopback.

## Manual Validation Areas

Manual verification remains useful for:

- Ableton Live ASIO driver Link button visibility.
- Ableton Live ASIO4ALL behavior.
- Ableton Live DirectX/MME Link-button caveat.
- Real audible phase alignment between Live and Mixxx.
- UI readability across edited skins and display scaling.
- Network churn and sleep/wake recovery.

Checklist:

```text
res/abletonlink/MANUAL_VERIFICATION.md
res/abletonlink/WINDOWS_REAL_WORLD_VALIDATION.md
```

## Known Limitations

- Mixxx uses a one-beat Link quantum for deck beat sync. Selectable Launch
  quantum is implemented separately for `1`, `2`, `4`, and `8` beat launch
  grids.
- Network discovery depends on local firewall, VPN, multicast, and adapter
  behavior.
- Ableton Live may hide its own Link button with DirectX/MME on Windows.
- Audio preflight capture quality depends on local routing; ASIO may bypass
  Windows loopback.

These are documented in more detail in `DEVELOPER_NOTES.md`.

## Suggested PR Description

```markdown
## Summary

Adds Ableton Link support to Mixxx for tempo/beat phase synchronization with
Link-capable applications. The integration exposes Link state through regular
Mixxx controls, adds Start/Stop Sync support, and provides a quantized Launch
action for starting synced decks on a selectable Link launch quantum.

## User-visible behavior

- Link can be enabled from skins/preferences.
- Peer count, Link BPM, beat phase, playing state, and next-beat timing are
  observable.
- Start/Stop Sync can publish/follow Link transport.
- Launch starts synced Mixxx decks on the selected Link launch quantum.
- LinkAudio can publish Mixxx main/per-source channels and can receive remote
  LinkAudio into Mixxx's main output when explicitly enabled.

## Implementation notes

- Link is implemented as an `EngineSync` sync participant.
- Link audio session state is captured from the audio callback path.
- Link-managed callbacks are queued back to Qt with guarded object lifetime.
- Launch is ignored when no synchronized primary deck target exists.
- Launch quantum is separate from the one-beat quantum used for normal deck
  beat sync.

## Testing

- Built `mixxx-test` with Ableton Link enabled.
- Ran `EngineSyncTest.*Link*`: 43 passed; optional external-peer test skipped
  when `MIXXX_LINK_PEER_EXE` was unset.
- Ran external peer test with local peer harness: passed.
- Ran Windows WASAPI loopback audio preflight: signal and click transients
  detected.

## Known limitations

- Deck beat sync uses a one-beat Link quantum; Launch supports selectable
  `1`, `2`, `4`, and `8` beat quantum values.
- Network peer discovery depends on local firewall/VPN/adapter behavior.
- Ableton Live may hide its own Link button with DirectX/MME on Windows; Mixxx
  controls remain available when Mixxx is built with Link support.
```
