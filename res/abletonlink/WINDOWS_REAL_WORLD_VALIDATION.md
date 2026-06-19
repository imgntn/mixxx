# Ableton Link Windows real-world validation

Use this checklist before moving on to macOS validation. Record the result of
each section, including driver names, device names, and anything surprising.

## Optional automated audio preflight

Before the manual Live/Mixxx pass, Codex can run a bounded local audio preflight
that does not create network churn:

```powershell
.\res\abletonlink\windows-audio-preflight.ps1
```

The script plays a short generated click pattern through the default Windows
playback path, records baseline/playback captures, analyzes RMS/transients, and
opens a prefilled copy of the HTML checklist with waveform evidence. If the
Python `soundcard` package is installed, the script first uses WASAPI loopback
from the default Windows playback device:

```powershell
python -m pip install --user soundcard
```

It also falls back to each FFmpeg DirectShow capture device, which is useful for
documenting physical microphones and unreadable virtual devices. A pass means
the active capture path heard the playback signal. A check/fail result is still
useful: it records isolated microphones, unreadable devices, or missing loopback
support so the remaining audio alignment checks stay explicitly
operator-observed.

## 1. Clean build and launch

1. Check out `codex/ableton-link-upstream`.
2. Build or launch the current Windows build.
3. If you want to avoid profile noise, run Mixxx with a fresh `--settings-path`.
4. Confirm Mixxx opens normally.
5. Confirm Mixxx Link controls are visible in the skins/preferences where they
   should appear.

## 2. Ableton Live with native ASIO

1. Open Ableton Live.
2. Open Live audio preferences.
3. Select the audio interface vendor's native ASIO driver.
4. Confirm Live shows its `Link` button.
5. Enable Link in Live.
6. Start Mixxx.
7. Enable Mixxx `Link`.
8. Confirm Mixxx peer count becomes `1`.
9. Change Live tempo and confirm Mixxx `[AbletonLink],bpm` follows.
10. Change Mixxx leader/deck tempo and confirm Live follows.

## 3. Start/Stop Sync

1. In Mixxx, enable Sync on one deck.
2. Enable Mixxx `Start/Stop Sync`.
3. Press play and stop in Live.
4. Confirm synced Mixxx decks start and stop with Live.
5. Press play and stop in Mixxx.
6. Confirm Live transport follows when Start/Stop Sync is enabled.

## 4. Quantized Launch

1. Stop synced Mixxx decks.
2. Keep Mixxx Link and Start/Stop Sync enabled.
3. Set Mixxx launch quantum to `1`.
4. Press Mixxx `Launch`.
5. Confirm the deck starts on the next Link beat.
6. Repeat with launch quantum `4` and confirm the deck waits for the next
   matching Link quantum boundary.
7. Disable Link while Launch is pending.
8. Confirm pending launch clears and does not fire later.

## 5. Ableton Live with ASIO4ALL

1. Switch Live audio preferences to ASIO4ALL.
2. Confirm Live still shows its `Link` button.
3. Enable Link in Live.
4. Repeat peer count, tempo follow, Start/Stop Sync, and Launch checks.

## 6. Ableton Live with DirectX/MME

1. Switch Live audio preferences to DirectX/MME.
2. Confirm whether Live hides its own `Link` button.
3. Confirm Mixxx still shows its Link controls because Mixxx was built with
   Ableton Link support.
4. Switch Live back to ASIO.
5. Confirm Live's `Link` button appears again and the Link session works again.

Important: the DirectX/MME caveat is about Ableton Live's own Link button
visibility, not Mixxx's Link controls.

## 7. Mixxx UI and skin check

1. Check every edited skin where Link controls appear.
2. Confirm Link, peer count, BPM, Start/Stop Sync, and Launch are readable.
3. Confirm Start/Stop Sync is understandable from its tooltip.
4. Confirm Link BPM contrast is readable.
5. Confirm Link controls do not crowd the clock or nearby toolbar controls.
6. Repeat at Windows display scaling `100%`, `125%`, and `150%` if practical.

## 8. Audio churn

Run this while Mixxx and Live are linked.

1. Change Mixxx buffer size.
2. Change Mixxx sample rate.
3. Switch Mixxx audio device/API if another option is available.
4. Stop and restart Mixxx audio processing through preferences if available.
5. Confirm no crash, stale Launch, stuck playing state, or broken peer count.
6. Re-test tempo follow and Start/Stop Sync after each change.

## 9. Network churn

Run this while Mixxx and Live are linked.

1. Turn Wi-Fi off and on, or disconnect and reconnect Ethernet.
2. Toggle VPN off and on if one is active.
3. Temporarily block and unblock Mixxx in Windows Firewall if comfortable.
4. Confirm peer count drops and recovers.
5. Confirm tempo and phase recover.
6. Confirm pending Launch does not remain stale through network loss.

## 10. Soak test

1. Link Mixxx and Live for at least 30-60 minutes.
2. Keep one synced Mixxx deck playing for part of the run.
3. Periodically change tempo in Live.
4. Periodically change tempo in Mixxx.
5. Periodically start and stop Live transport.
6. Periodically start and stop Mixxx deck transport.
7. Trigger Launch repeatedly.
8. Watch for drift, missed starts, stuck UI, CPU growth, or memory growth.

## Minimum bar before macOS validation

1. Native ASIO pass.
2. ASIO4ALL pass if available.
3. DirectX/MME Live Link-button caveat confirmed.
4. Link UI readable.
5. One audio churn pass.
6. One network churn pass.
7. 30-minute soak clean.

## Operator-assisted automation plan

Use this plan when Codex is driving the Windows validation with James observing
the real audio/UI parts. Codex can automate process launch, isolated settings
paths, external peer harnesses, log capture, and many control changes. James
confirms anything that requires ears, exact hardware choice, or visual judgment
inside Ableton Live/Mixxx.

### Phase A: Prep and guardrails

Codex actions:

1. Confirm no old `mixxx.exe`, `mixxx-test.exe`, or `mixxx-link-peer.exe`
   processes from prior validation are running.
2. Create a timestamped validation folder under
   `X:\mixxx_test\ableton-link-peer-tools\logs\real_world_validation`.
3. Launch Mixxx with an isolated `--settings-path` unless James asks to use the
   normal profile.
4. Start passive Link peer harnesses only when useful for extra peer-count
   pressure.

Ask James before proceeding:

1. Which audio interface should be used for the native ASIO pass?
2. Is it okay to interrupt the current Ableton Live/Mixxx audio session?
3. Should validation use the normal Mixxx profile or an isolated settings path?

James observations to record:

1. Current speaker/headphone routing is safe.
2. Audio output is audible and not dangerously loud.

### Phase B: Native ASIO Live/Mixxx session

Codex actions:

1. Launch Ableton Live if it is not already open.
2. Launch Mixxx.
3. Help navigate to or document Live's selected audio driver.
4. Enable Link in Mixxx and verify Mixxx-side Link controls/logs where possible.
5. Optionally start the synthetic peer harness as a second/third peer.

Ask James for UI observation:

1. Does Ableton Live show its `Link` button with the native ASIO driver?
2. Is Live Link enabled?
3. Does Mixxx show Link controls clearly?
4. Does Mixxx peer count show Live as a peer?

Ask James for audio observation:

1. With Live playing and Mixxx synced, do the beats sound phase-aligned?
2. When tempo changes in Live, does Mixxx audibly follow without obvious glitching?
3. When tempo changes in Mixxx, does Live audibly follow without obvious glitching?

Pass condition:

1. Live Link button visible.
2. Mixxx peer count includes Live.
3. Tempo follows both directions.
4. James hears stable, aligned playback.

### Phase C: Start/Stop Sync and Quantized Launch

Codex actions:

1. Enable Sync on a Mixxx deck.
2. Enable Mixxx Start/Stop Sync.
3. Trigger or ask James to trigger Live play/stop.
4. Trigger or ask James to trigger Mixxx play/stop.
5. Arm Mixxx `Launch`, then disable Link while pending for cancellation testing.

Ask James for audio/UI observation:

1. Do synced Mixxx decks start and stop with Live transport?
2. Does Live follow Mixxx transport when Start/Stop Sync is enabled?
3. Does `Launch` start the Mixxx deck on the selected Link launch quantum?
4. When Link is disabled while Launch is pending, does the pending launch
   visibly clear and not fire later?

Pass condition:

1. Transport sync works in both expected directions.
2. Quantized Launch starts on the selected Link launch quantum.
3. Pending launch cancellation is visible and stable.

### Phase D: ASIO4ALL

Codex actions:

1. Pause automation while James selects ASIO4ALL in Ableton Live, because exact
   driver/device names and dialogs are machine-specific.
2. Resume Link, tempo, transport, and Launch checks once Live is configured.

Ask James for UI observation:

1. Does Live show its `Link` button with ASIO4ALL?
2. Did Live accept ASIO4ALL without device errors?

Ask James for audio observation:

1. Is Live audio audible through the expected output?
2. Does linked playback remain aligned?
3. Are there glitches/dropouts after switching to ASIO4ALL?

Pass condition:

1. Live Link button visible.
2. Peer count and tempo follow work.
3. James hears stable linked playback.

### Phase E: DirectX/MME caveat

Codex actions:

1. Pause automation while James switches Ableton Live to DirectX/MME.
2. Verify Mixxx remains built with and exposing Ableton Link controls.
3. Ask James to switch Live back to ASIO afterward.

Ask James for UI observation:

1. Does Live hide its own `Link` button under DirectX/MME?
2. Do Mixxx Link controls remain visible?
3. After switching Live back to ASIO, does Live's Link button reappear?

Pass condition:

1. The DirectX/MME behavior is recorded accurately as an Ableton Live UI/driver
   caveat.
2. Mixxx Link controls remain available in the Link-enabled build.

### Phase F: Mixxx UI polish and scaling

Codex actions:

1. Help cycle skins if automation can do so safely.
2. Capture screenshots if useful.
3. Record skin/display-scaling combinations checked.

Ask James for visual observation:

1. Are Link, peer count, BPM, Start/Stop Sync, and Launch readable?
2. Is Start/Stop Sync understandable from the tooltip?
3. Is Link BPM contrast acceptable?
4. Do Link controls crowd or overlap the clock/toolbar?
5. At 100%, 125%, and 150% scaling, does the Link cluster still fit?

Pass condition:

1. No unreadable Link text.
2. No overlapping controls.
3. Tooltips clarify ambiguous controls.

### Phase G: Audio churn

Codex actions:

1. Pause before each potentially disruptive audio-device change.
2. Change Mixxx buffer size/sample rate/device only after James confirms the
   target setting is safe.
3. After each change, re-check Link controls, peer count, and synthetic peer
   logs if active.

Ask James before each change:

1. Which device/API/sample rate/buffer should be selected?
2. Is it safe to briefly interrupt audio?

Ask James for audio observation after each change:

1. Did audio resume?
2. Did linked playback remain aligned?
3. Were there glitches beyond the expected restart interruption?
4. Did any pending Launch fire unexpectedly?

Pass condition:

1. Mixxx does not crash.
2. Link peer count recovers.
3. Tempo/transport still work.
4. No stale Launch or stuck playing state.

### Phase H: Network churn

Codex actions:

1. Use synthetic peers and/or Live as Link peers.
2. Pause before disruptive network changes.
3. If James approves, toggle firewall/VPN/network adapter state in bounded
   steps and restore it afterward.

Ask James before each change:

1. Is it okay to interrupt network connectivity now?
2. Which adapter/VPN/firewall rule should be changed?

Ask James for UI/audio observation:

1. Did peer count drop as expected?
2. Did peer count recover after restoring network?
3. Did tempo/phase recover?
4. Did audio continue or recover acceptably?

Pass condition:

1. No crash.
2. Peer count recovers.
3. No stale pending Launch survives network loss.

### Phase I: Soak

Codex actions:

1. Start a timestamped soak log.
2. Keep optional synthetic peers running if desired.
3. Periodically prompt James for observations.
4. Record CPU/memory/process state at intervals where practical.

Ask James at start:

1. Should soak run for 30, 60, or 120 minutes?
2. Should synthetic peers be included?

Ask James every 10-15 minutes:

1. Is playback still aligned?
2. Any audible drift, flams, dropouts, or missed starts?
3. Any stuck UI indicators?
4. Any unexpected CPU/fan/memory behavior?

Pass condition:

1. No crash.
2. No sustained drift.
3. No missed transport events.
4. No stuck Link/Launch UI.
5. No sustained CPU or memory growth.

### Final record

At the end, record:

1. Mixxx commit.
2. Ableton Live version.
3. Windows version.
4. Audio interface and driver names.
5. Which phases passed/failed/skipped.
6. Exact failure notes and screenshots/log paths.
