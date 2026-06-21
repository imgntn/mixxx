# Ableton Link Agent Handoff

This is a private fork handoff note for continuing the Ableton Link integration
work. It is not intended to be included in the upstream Mixxx PR unless James
explicitly decides otherwise.

## Repository State

- Repo: `X:\mixxx_test\mixxx`
- Branch: `codex/ableton-link-upstream`
- Push remote: `origin` (`git@github.com:imgntn/mixxx.git`)
- Upstream remote: `upstream` (`https://github.com/mixxxdj/mixxx.git`)
- Latest pushed implementation commit: `ce3fb5e3f0 Fix Ableton Link Linux include dependency`
- Do not open a PR against `mixxxdj/mixxx` yet. James wants to run Mac
  validation first.

Expected dirty files at handoff time:

- `res/abletonlink/PRIVATE_DEVLOG.md`: private note updated to make James's
  macOS build plus real Ableton Live verification the final release gate.
- `res/abletonlink/HANDOFF.md`: this file.

Start the next session with:

```bash
git status --short
git log --oneline -8
git remote -v
```

## What Has Been Built

The Ableton Link integration now includes:

- Core Ableton Link tempo and phase session participation.
- Start/Stop Sync support.
- Quantized launch UX with selectable launch quantum.
- Link diagnostics controls.
- Preferences and skin controls/tooltips for Link state.
- Optional Ableton Link dependency fetching through CMake.
- Classic Link support when LinkAudio headers are absent.
- Link 4 / LinkAudio support when `LinkAudio.hpp` is available.
- Main-output LinkAudio publishing.
- Optional per-source LinkAudio publishing.
- Optional inbound LinkAudio receive, default off.
- Real-time callback path hardening for snapshots/sinks/inputs.
- Focused unit and stress tests around transport, launch, churn, and LinkAudio
  callback behavior.

Private/reference docs currently live in `res/abletonlink`:

- `README.md`: user/reviewer-facing integration overview.
- `MANUAL_VERIFICATION.md`: manual validation steps.
- `DEVELOPER_NOTES.md`: implementation and review notes.
- `PRIVATE_DEVLOG.md`: private chronological summary and PR shape notes.
- `HANDOFF.md`: this continuation note.

## Licensing Note

Ableton Link can be downloaded and built without an Ableton Live license. The
public Link SDK is dual licensed under GPLv2+ and proprietary terms. Mixxx's GPL
build path can use the GPLv2+ route. If this comes up during review, cite the
official Ableton Link repo and license file rather than relying on memory.

## Validation Completed

Windows validation was exercised earlier on this branch with Ableton Live and
Mixxx. Important Windows note: if Ableton Live is using DirectX/MME audio
drivers, Live may hide its own Link button. That is a Live driver-mode issue;
Mixxx's Link controls should still be visible whenever Mixxx is built with Link
support.

Linux container validation was completed from Windows using Docker Desktop:

- Docker image pulled: `ubuntu:24.04`
- CMake options:
  - `-DABLETONLINK=ON`
  - `-DFETCH_ABLETONLINK=ON`
  - `-DBUILD_TESTING=ON`
  - `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
- Result: `mixxx-test` target built successfully with Ableton Link fetched from
  GitHub and LinkAudio enabled.
- Focused Link test subset:

```bash
ctest --output-on-failure -R 'EngineSyncTest\.(Link|AbletonLink)'
```

Result:

- 47 tests selected.
- 46 passed.
- 1 skipped: `EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured`, because
  it requires explicit external peer configuration via `MIXXX_LINK_PEER_EXE`.

Linux caveat: Docker Desktop Linux containers expose a WSL2 kernel string, so
Mixxx's top-level WSL guard fails configure. For the Linux validation, the guard
was bypassed only in a temporary container clone, not in the repo.

Local Docker artifact that may still exist:

- `mixxx-linux-link-check:latest`
- Approx size: 11.3 GB
- It contains the successful Linux build tree and can be deleted if space is
  needed.

## Bug Found And Fixed During Linux Check

GCC caught that `src/engine/sync/abletonlink.h` used `kMaxEngineSamples`
without directly including the header that defines it. MSVC had tolerated the
transitive include.

Fix committed and pushed:

```text
ce3fb5e3f0 Fix Ableton Link Linux include dependency
```

The fix is a direct include of `util/defs.h` in `abletonlink.h`.

## Remaining Release Gate

James plans to build and test on macOS. Treat this as the final gate before
opening any upstream PR.

Mac validation should cover:

- Clean configure/build with Ableton Link enabled.
- Either fetch Link during configure or point at the intended local/system Link
  source.
- Focused `EngineSyncTest.*Link*` or equivalent CTest Link subset.
- Launch Mixxx with Ableton Live open and Link enabled.
- Verify Mixxx Link controls appear.
- Verify peer discovery with Ableton Live.
- Verify BPM follows Live and remains readable in the UI.
- Verify Start/Stop Sync behavior.
- Verify quantized launch using a few launch quantum values.
- Verify LinkAudio availability if building with Link 4 headers.
- Try a short churn pass: Link toggle, SS toggle, deck sync toggles,
  per-source publishing, receive controls, and audio-device changes.

If macOS passes, the branch should be ready for upstream PR preparation.

## Suggested Next-Agent Flow

1. Check working tree status.
2. Commit the private documentation updates if James wants them preserved in
   the private fork.
3. Wait for or help with James's macOS validation results.
4. If Mac reveals issues, fix narrowly and rerun focused tests.
5. After Mac passes, do one last review pass:
   - `git diff upstream/main...HEAD`
   - `git diff --check`
   - focused Link tests
   - docs/control naming review
   - scan for private-only artifacts that should not be in the upstream PR
6. Prepare the upstream branch shape:
   - likely squash development commits into one coherent feature commit, or
     into a small curated stack if reviewers prefer.
   - keep private notes out of upstream unless explicitly requested.
7. Do not create or open the upstream PR until James explicitly asks.

## Suggested Squashed Commit Message

```text
Add Ableton Link tempo, transport, and Link Audio integration

Add Ableton Link session support for tempo/phase synchronization, Start/Stop
Sync, quantized launch, diagnostics, preferences, skin controls, and controller
metadata. Support classic Link builds as well as Link 4 LinkAudio builds, with
optional dependency fetching for validation builds.

Add optional LinkAudio main-output and per-source publishing, default-off inbound
LinkAudio receive, and real-time-safe callback snapshots. Cover Link transport,
launch, diagnostics, LinkAudio churn, and callback behavior with focused tests.
```

## Useful Commands

Focused Windows-style test command from this repo:

```powershell
build\x64__abletonlink4\mixxx-test.exe --gtest_filter=EngineSyncTest.*Link*
```

Focused CTest command from a CMake build directory:

```bash
ctest --output-on-failure -R 'EngineSyncTest\.(Link|AbletonLink)'
```

External peer stress test setup:

```bash
export MIXXX_LINK_PEER_EXE=/path/to/link/peer/helper
ctest --output-on-failure -R 'EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured'
```

Before touching code, inspect:

```bash
rg -n "AbletonLink|LinkAudio|launch_quantum|quantized_launch" src res
```
