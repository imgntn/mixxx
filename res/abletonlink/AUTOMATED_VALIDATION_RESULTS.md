# Ableton Link automated validation results

Last updated: 2026-06-19 20:31 Pacific

Branch: `codex/ableton-link-upstream`

Commit: `c34418db5a60`

Primary evidence folder:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\automated_validation\20260619-201551
```

## Build validation

Both Mixxx test binaries rebuilt successfully from a clean worktree:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build\x64__abletonlink --target mixxx-test --config RelWithDebInfo --parallel 8'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build\x64__abletonlink4 --target mixxx-test --config RelWithDebInfo --parallel 8'
```

Result: both builds passed.

## Link-focused engine tests

Packaged Link build:

```powershell
build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link* --gtest_color=no
```

Result: `45 passed, 1 skipped`.

Log:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\automated_validation\20260619-201551\engine_sync_link_packaged.log
```

Link 4.0 / LinkAudio build:

```powershell
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "X:\mixxx_test\mixxx\build\x64__abletonlink\platforms"
build\x64__abletonlink4\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link* --gtest_color=no
```

Result: `45 passed, 1 skipped`.

Log:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\automated_validation\20260619-201551\engine_sync_link_link4.log
```

The skipped test in both suites was the optional external peer test, which is
run explicitly below.

## External peer validation

Packaged Link build:

```powershell
$env:MIXXX_LINK_PEER_EXE = "X:\mixxx_test\ableton-link-peer-tools\build\mixxx-link-peer.exe"
build\x64__abletonlink\mixxx-test.exe --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Result: `1 passed`.

Log:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\automated_validation\20260619-201551\external_peer_packaged_sequential.log
```

Link 4.0 / LinkAudio build:

```powershell
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "X:\mixxx_test\mixxx\build\x64__abletonlink\platforms"
$env:MIXXX_LINK_PEER_EXE = "X:\mixxx_test\ableton-link-peer-tools\build-link4\mixxx-link-peer.exe"
build\x64__abletonlink4\mixxx-test.exe --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Result: `1 passed` on clean retry.

Log:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\automated_validation\20260619-201551\external_peer_link4_retry.log
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
.\res\abletonlink\windows-audio-preflight.ps1 -RepoRoot X:\mixxx_test\mixxx -OutputRoot X:\mixxx_test\ableton-link-peer-tools\logs\audio_preflight -PlaybackVolume 20
```

Result: blocked in this environment. The latest run created only the generated
click-test WAV and timed out before capture summary generation:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\audio_preflight\20260619-202352\audio-preflight-click-test.wav
```

The earlier completed audio preflight found no Python `soundcard` WASAPI
loopback support and fell back to DirectShow microphones. Those microphones did
not capture playback from the default output device:

```text
X:\mixxx_test\ableton-link-peer-tools\logs\audio_preflight\20260619-024834\audio-preflight-summary.json
```

Summary from that completed run:

- WASAPI loopback through Python `soundcard`: unavailable.
- DirectShow capture devices detected: `Microphone (BlackShark V3 - Chat)` and
  `Microphone (Steam Streaming Microphone)`.
- Best readable capture path: `Microphone (BlackShark V3 - Chat)`.
- Playback signal detection: no clear playback signal (`0.01 dB` RMS increase,
  `0` transients).

Additional `soundcard` install/query attempts through the local Python launchers
timed out while importing or querying audio devices. The timed-out Python
processes started during the validation window were stopped.

Conclusion: automated loopback evidence is not currently available on this
Windows environment without fixing Python WASAPI loopback enumeration or adding
a known-good virtual/physical loopback capture route. This does not invalidate
the Link engine tests; it leaves real audio capture as a manual validation item.

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
X:\mixxx_test\ableton-link-peer-tools\logs\real_world_validation\20260619-222956\preflight-state.json
X:\mixxx_test\ableton-link-peer-tools\logs\real_world_validation\20260619-222956\windows-real-world-validation-checklist-20260619-222956.html
```

Captured metadata:

- Windows: `Windows 10 Pro 25H2 build 26200.8246`
- ASIO registry entries: `Ableton Move`, `Ableton Push`, `ASIO4ALL v2`,
  `Realtek ASIO`
- DirectShow capture devices: `Microphone (BlackShark V3 - Chat)`,
  `Microphone (Steam Streaming Microphone)`
- XML/UI parse preflight: OK for edited skin toolbar XML files and
  `src\preferences\dialog\dlgprefsyncdlg.ui`
