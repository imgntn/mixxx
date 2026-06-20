# Ableton Link manual verification

Use this checklist for validation that requires real audio devices, Ableton
Live, another Link-capable peer, operating system networking, or long-running
observation.

## Test environment record

Record these values for every manual run:

- Mixxx commit:
- Operating system:
- Audio interface:
- Mixxx audio backend:
- Mixxx sample rate:
- Mixxx buffer size:
- Ableton Live version:
- Ableton Live audio driver type:
- Ableton Live audio device:
- Network connection:
- Other Link peer application, if any:

## Ableton Live peer smoke test

1. Start Ableton Live.
2. On Windows, select an ASIO driver in Live's audio preferences. Use the
   interface vendor driver when available; ASIO4ALL is acceptable for a smoke
   test. On macOS, use CoreAudio. On Linux, use the normal backend for the
   package under test.
3. Confirm Live shows its own `Link` button.
4. Enable Link in Live.
5. Start Mixxx built with `ABLETONLINK=ON`.
6. Enable Mixxx `Link`.
7. Confirm Mixxx peer count becomes at least `1`.
8. Change Live's tempo and confirm Mixxx `[AbletonLink],bpm` follows.
9. Change Mixxx leader tempo and confirm Live follows.
10. Enable Sync on a Mixxx deck and press play.
11. Confirm deck tempo and beat phase align with the Link session.
12. Enable Mixxx Start/Stop Sync.
13. Start and stop Live transport.
14. Confirm synced Mixxx decks start and stop with Live transport.
15. Stop Mixxx decks, keep Link and Start/Stop Sync enabled, then press
    `Launch`.
16. Set launch quantum to `1`, then confirm synced Mixxx decks start on the next
    Link beat.
17. Repeat with launch quantum `4`, then confirm synced Mixxx decks wait for a
    later matching Link quantum boundary instead of starting immediately.

Expected result: tempo, phase, peer count, Start/Stop Sync, and quantized launch
all behave consistently, with no stale launch indicator after cancellation.

## Link Audio publish and receive

Run this only for builds where `[AbletonLink],link_audio_available` is `1`.

1. Enable Mixxx Link and Link Audio.
2. Confirm Mixxx publishes `Mixxx Main` to another Link Audio-capable peer.
3. Enable `Publish source channels separately`.
4. Confirm active decks, samplers, microphones, auxiliary inputs, or preview
   decks appear as separate source channels when they are active.
5. Disable `Publish source channels separately`.
6. Confirm `Mixxx Main` remains available and per-source channels are removed.
7. Enable Link Audio receive in Mixxx.
8. Publish audio from a different Link Audio peer.
9. Confirm received audio is mixed into Mixxx's main output.
10. Toggle receive mute and adjust receive gain.

Expected result: main-output publishing is controlled by Link Audio enable,
per-source publishing is controlled separately, and received audio remains
timeline-aligned with the Link session.

## Windows DirectX/MME Ableton Live check

1. In Ableton Live on Windows, switch Live's audio preferences to DirectX/MME.
2. Confirm whether Live hides its own `Link` button.
3. Start Mixxx built with `ABLETONLINK=ON`.
4. Confirm Mixxx still exposes its Link controls in preferences and skins.
5. Switch Live back to ASIO.
6. Confirm Live's `Link` button appears again and a Link session can be joined.

Expected result: the DirectX/MME caveat applies to Ableton Live's own Link
button visibility. Mixxx controls remain available when Mixxx was built with
Ableton Link support.

## Second-peer smoke test

Use Ableton Live, another DAW, a mobile Link application, or Ableton's LinkHut
sample utility if available in the local Ableton Link source package.

1. Start the second Link peer.
2. Enable Link on that peer.
3. Enable Mixxx `Link`.
4. Confirm Mixxx peer count increases.
5. Change tempo on the peer and confirm Mixxx follows.
6. Change tempo in Mixxx and confirm the peer follows.
7. Toggle peer transport while Mixxx Start/Stop Sync is enabled.
8. Confirm synced Mixxx decks follow peer transport.

Expected result: Mixxx behaves the same with non-Live Link peers as it does with
Ableton Live.

## Audio-engine alignment

1. Start LinkHut or another click-like Link reference peer.
2. Enable Link in the reference peer and start playback.
3. Enable Link in Mixxx.
4. Play a short, click-like sample from a synced Mixxx deck on the same beats as
   the reference peer.
5. Record both outputs through physical loopback or a reliable software
   loopback.
6. Measure onset alignment between the reference click and Mixxx output.

Expected result: Mixxx and the reference peer align within 3 ms. Record the
measured offset, audio interface, driver, sample rate, and buffer size.

## Network churn

Run this with Mixxx linked to at least one external peer.

1. Block Mixxx in the platform firewall, then unblock it.
2. Turn Wi-Fi off, then on.
3. Switch between Wi-Fi and Ethernet if both are available.
4. Disable and re-enable the active network adapter.
5. Sleep and wake the machine.
6. Repeat peer discovery after each change.
7. Trigger a Mixxx quantized launch before and after network recovery.

Expected result: Mixxx does not crash, peer count recovers, tempo/phase recover,
and no stale launch remains armed after network loss.

## Audio device churn

Run this with Mixxx linked to at least one external peer.

1. Change Mixxx audio buffer size.
2. Change Mixxx sample rate.
3. Switch Mixxx between available audio devices. On Windows this should include
   ASIO where available; on macOS this should include CoreAudio devices; on
   Linux this should include the active ALSA, JACK, PulseAudio, or PipeWire
   route used by the package.
4. Stop and restart Mixxx audio processing through preferences if available.
5. Disconnect and reconnect the audio interface if the setup allows it.
6. After each change, confirm Link status controls remain finite and responsive.
7. Trigger Start/Stop Sync and quantized launch again.

Expected result: Mixxx remains stable, Link recovers, and no stale scheduled
launch fires after audio-device changes.

## Long-run soak

1. Link Mixxx and Ableton Live for at least one hour.
2. Keep one synced Mixxx deck playing for part of the run.
3. Start and stop Ableton Live transport periodically.
4. Start and stop Mixxx deck transport periodically.
5. Trigger quantized launch repeatedly.
6. Watch for drift, stale UI, missed starts, CPU growth, and memory growth.
7. Extend to at least two hours if the first hour is clean.

Expected result: no observable drift beyond normal Link correction behavior, no
stale UI, no missed transport events, and no sustained CPU or memory growth.
