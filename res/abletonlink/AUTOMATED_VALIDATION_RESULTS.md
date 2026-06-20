# Ableton Link automated validation results

Last updated: 2026-06-19 22:50 Pacific

Branch: `codex/ableton-link-upstream`

Commit: `c34418db5a60`

Primary evidence folder:

```text
<validation-artifact-root>/logs/automated_validation/20260619-201551
```

## Build validation

Both Mixxx test binaries rebuilt successfully from a clean worktree:

```powershell
$Vcvars64 = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"
cmd /c "call `"$Vcvars64`" && cmake --build `"build/x64__abletonlink`" --target mixxx-test --config RelWithDebInfo --parallel 8"
cmd /c "call `"$Vcvars64`" && cmake --build `"build/x64__abletonlink4`" --target mixxx-test --config RelWithDebInfo --parallel 8"
```

Result: both builds passed.

## Link-focused engine tests

Packaged Link build:

```powershell
$MixxxTest = Join-Path "build/x64__abletonlink" "mixxx-test.exe"
& $MixxxTest --gtest_filter=EngineSyncTest.*Link* --gtest_color=no
```

Result: `45 passed, 1 skipped`.

Log:

```text
<validation-artifact-root>/logs/automated_validation/20260619-201551/engine_sync_link_packaged.log
```

Link 4.0 / LinkAudio build:

```powershell
$RepoRoot = Resolve-Path "."
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $RepoRoot "build/x64__abletonlink/platforms"
$MixxxTest = Join-Path "build/x64__abletonlink4" "mixxx-test.exe"
& $MixxxTest --gtest_filter=EngineSyncTest.*Link* --gtest_color=no
```

Result: `45 passed, 1 skipped`.

Log:

```text
<validation-artifact-root>/logs/automated_validation/20260619-201551/engine_sync_link_link4.log
```

The skipped test in both suites was the optional external peer test, which is
run explicitly below.

## External peer validation

Packaged Link build:

```powershell
$PeerToolsRoot = "<absolute path to ableton-link-peer-tools>"
$env:MIXXX_LINK_PEER_EXE = Join-Path $PeerToolsRoot "build/mixxx-link-peer.exe"
$MixxxTest = Join-Path "build/x64__abletonlink" "mixxx-test.exe"
& $MixxxTest --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Result: `1 passed`.

Log:

```text
<validation-artifact-root>/logs/automated_validation/20260619-201551/external_peer_packaged_sequential.log
```

Link 4.0 / LinkAudio build:

```powershell
$RepoRoot = Resolve-Path "."
$PeerToolsRoot = "<absolute path to ableton-link-peer-tools>"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $RepoRoot "build/x64__abletonlink/platforms"
$env:MIXXX_LINK_PEER_EXE = Join-Path $PeerToolsRoot "build-link4/mixxx-link-peer.exe"
$MixxxTest = Join-Path "build/x64__abletonlink4" "mixxx-test.exe"
& $MixxxTest --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Result: `1 passed` on clean retry.

Log:

```text
<validation-artifact-root>/logs/automated_validation/20260619-201551/external_peer_link4_retry.log
```

### External peer caveat

Running the packaged-Link and Link 4.0 external peer tests in parallel caused
expected network-domain interference: each test saw the other test's peer group,
then the departure assertion did not converge. Sequential runs passed. Do not
run multiple external peer discovery tests against the same local network domain
at the same time unless the test harness is extended to isolate sessions.

## Audio loopback automation

Attempted:

```powershell
$RepoRoot = Resolve-Path "."
$PeerToolsRoot = "<absolute path to ableton-link-peer-tools>"
$PythonExe = "<absolute path to python.exe>"
$env:PYTHONPATH = Join-Path $PeerToolsRoot "vendor/python"
& (Join-Path $RepoRoot "res/abletonlink/windows-audio-preflight.ps1") `
    -RepoRoot $RepoRoot `
    -OutputRoot (Join-Path $PeerToolsRoot "logs/audio_preflight") `
    -PlaybackVolume 20 `
    -PythonExe $PythonExe
```

Result: passed with Python `soundcard` WASAPI loopback.

```text
<validation-artifact-root>/logs/audio_preflight/20260619-224738/audio-preflight-summary.json
<validation-artifact-root>/logs/audio_preflight/20260619-224738/audio-preflight-checklist-20260619-224738.html
<validation-artifact-root>/logs/audio_preflight/20260619-224738/WASAPI_loopback/audio-preflight-waveforms.png
```

Summary from the successful run:

- WASAPI loopback through Python `soundcard`: available.
- Loopback device: `DELL S3422DWG (NVIDIA High Definition Audio)`.
- Result: `signal-and-transients-detected`.
- RMS increase: `195.49 dB`.
- Detected transients: `15`.
- Playback volume: `20`.

DirectShow fallback results in the same run:

- `Microphone (BlackShark V3 - Chat)`: readable but did not capture default
  playback (`0.01 dB` RMS increase, `0` transients).
- `Microphone (Steam Streaming Microphone)`: FFmpeg DirectShow recording
  failed during baseline capture.

### Python WASAPI root cause

The earlier audio preflight failures were caused by Python tooling, not by
Chrome, Spotify, Ableton Live, or another app playing audio. `soundcard`'s
Windows backend calls `platform.win32_ver()` at import time only to special-case
Windows 8. On this machine, Python's `platform.win32_ver()` hung in a WMI query.
The validation helper now patches `platform.win32_ver()` before importing
`soundcard`, and `windows-audio-preflight.ps1` accepts an explicit `-PythonExe`
so the preflight can avoid problematic default Python launchers.

For the successful run, `soundcard` was loaded from a locally unpacked pure
Python wheel under:

```text
<validation-artifact-root>/vendor/python
```

## Automated coverage still missing

- Ableton Live UI behavior with ASIO, ASIO4ALL, DirectX, and MME.
- Audible phase/alignment confirmation between Live and Mixxx.
- Visual inspection of the live Mixxx skin toolbar and Sync preferences dialog.
- LinkAudio payload interoperability with a non-Mixxx LinkAudio peer that
  actually publishes audio frames.
- Long soak with real audio device changes and sleep/wake.

Those remain in `MANUAL_VERIFICATION.md` and `WINDOWS_REAL_WORLD_VALIDATION.md`.

## Windows checklist preflight

The Windows real-world validation checklist preflight now avoids unbounded CIM
queries for process, OS, and audio-device metadata. It uses `Get-Process` for
running validation processes, the Windows version registry key for OS metadata,
and FFmpeg DirectShow enumeration for capture devices.

Latest generated checklist artifacts:

```text
<validation-artifact-root>/logs/real_world_validation/20260619-222956/preflight-state.json
<validation-artifact-root>/logs/real_world_validation/20260619-222956/windows-real-world-validation-checklist-20260619-222956.html
```

Captured metadata:

- Windows: `Windows 10 Pro 25H2 build 26200.8246`
- ASIO registry entries: `Ableton Move`, `Ableton Push`, `ASIO4ALL v2`,
  `Realtek ASIO`
- DirectShow capture devices: `Microphone (BlackShark V3 - Chat)`,
  `Microphone (Steam Streaming Microphone)`
- XML/UI parse preflight: OK for edited skin toolbar XML files and
  `src/preferences/dialog/dlgprefsyncdlg.ui`
