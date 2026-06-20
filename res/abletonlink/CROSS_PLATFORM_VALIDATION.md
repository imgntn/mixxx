# Ableton Link macOS and Linux validation

Use this checklist after the Windows validation when preparing the branch for
review on macOS and Linux. The core Link implementation is platform-neutral:
Mixxx uses Ableton Link's platform clock, Link's host-time filter, Qt controls,
and the normal Mixxx audio callback path. Platform-specific risk is mostly in
packaging, firewall/network discovery, audio routing, and available Link header
versions.

## Build matrix

Record the exact commit, compiler, Qt version, CMake options, and Link source
for each run.

| Platform | Required build | LinkAudio expectation |
| --- | --- | --- |
| macOS | `-DABLETONLINK=ON` | `link_audio_available=1` when using fetched Ableton Link 4.0 or newer system headers. |
| Linux distro package | `-DABLETONLINK=ON -DFETCH_ABLETONLINK=OFF` | May report `link_audio_available=0` if the distro package predates Link 4.0. |
| Linux fetched Link | `-DABLETONLINK=ON -DFETCH_ABLETONLINK=ON` | Should report `link_audio_available=1` because Mixxx fetches Ableton Link 4.0. |
| Link disabled control | `-DABLETONLINK=OFF` | Link tests skip and UI reports Link unavailable. |

The LinkAudio code is guarded by `__has_include(<ableton/LinkAudio.hpp>)`.
Older system Link packages should still build classic tempo/phase Link support;
they simply leave LinkAudio controls unavailable.

## Automated tests

Run the focused Link tests first:

```shell
./mixxx-test --gtest_filter='EngineSyncTest.*Link*' --gtest_color=no
```

Run the optional external peer test with a local LinkHut or peer harness:

```shell
export MIXXX_LINK_PEER_EXE=/path/to/mixxx-link-peer
./mixxx-test --gtest_filter=EngineSyncTest.LinkDiscoversExternalPeersWhenConfigured --gtest_color=no
```

Expected result: the focused tests pass, or the external peer test skips only
when `MIXXX_LINK_PEER_EXE` is unset.

## macOS validation

1. Build and run Mixxx with `ABLETONLINK=ON`.
2. Start Ableton Live or another macOS Link-capable peer.
3. Allow local network access if macOS prompts for it.
4. If peer discovery fails, check the macOS firewall and any VPN or network
   filtering software before changing Mixxx code.
5. Enable Link in both applications and confirm Mixxx peer count rises.
6. Change tempo in Live and Mixxx and confirm both directions follow.
7. Enable Start/Stop Sync and verify Live transport starts/stops synced Mixxx
   decks.
8. Test Launch with `1`, `2`, `4`, and `8` beat launch quantum values.
9. If validating audio alignment, route both outputs through a trusted loopback
   such as BlackHole, Loopback, or a physical interface loopback and measure
   click onset alignment.
10. If validating LinkAudio, enable LinkAudio publish in Mixxx and confirm a
    compatible peer can discover `Mixxx Main` and per-source channels. Enable
    inbound receive only intentionally because it mixes remote LinkAudio into
    Mixxx's local main output.

Expected result: tempo, phase, peer discovery, Start/Stop Sync, Launch, and
LinkAudio controls behave the same as on Windows, with only audio-routing setup
varying by machine.

## Linux validation

1. Build and run Mixxx with `ABLETONLINK=ON`.
2. If testing a distro package, record the Ableton Link package version and
   whether `link_audio_available` is `0` or `1`.
3. Start a Linux Link-capable peer such as LinkHut, a DAW, or a local peer
   harness.
4. Confirm UDP multicast/broadcast traffic is allowed on the active network.
   Firewalls, VPNs, containers, and isolated Wi-Fi networks can prevent Link
   discovery.
5. Enable Link in both peers and confirm Mixxx peer count rises.
6. Change tempo in both directions and confirm convergence.
7. Enable Start/Stop Sync and verify remote transport starts/stops synced Mixxx
   decks.
8. Test Launch with `1`, `2`, `4`, and `8` beat launch quantum values.
9. Exercise the audio backend used by the package: ALSA, JACK, PulseAudio, or
   PipeWire. Change sample rate and buffer size where the backend allows it.
10. If validating audio alignment, capture both signals through a PipeWire or
    PulseAudio monitor source, JACK routing, or physical loopback and measure
    click onset alignment.

Expected result: Mixxx survives backend and network churn, keeps Link status
controls finite, and recovers peer discovery after temporary network loss.

## Code portability review notes

- The engine implementation has no Windows audio API dependency. WASAPI and
  DirectX/MME references live in Windows validation scripts and documentation.
- `WIN32_LEAN_AND_MEAN` is only added for Windows builds because Ableton Link
  pulls in Asio headers there.
- Android still defines `LINK_PLATFORM_LINUX=1` for Ableton Link headers.
- LinkAudio publish and receive use fixed-size buffers and C++20
  `std::atomic<std::shared_ptr<...>>`; macOS and Linux compilers therefore need
  the same C++20-capable standard library required by the rest of this build.
- QProcess-based external peer tests use `QProcess::nullDevice()` and should be
  portable as long as `MIXXX_LINK_PEER_EXE` points to an executable for the
  local platform.

## Evidence to attach upstream

For each platform, keep:

- build command and CMake option summary;
- `EngineSyncTest.*Link*` output;
- external peer test output, if run;
- screenshots of Sync preferences and skin controls with Link enabled;
- peer count, BPM, Start/Stop Sync, and Launch observations;
- LinkAudio availability and channel-count observations;
- loopback alignment measurement, if available;
- notes about firewall, VPN, audio backend, and loopback routing.
