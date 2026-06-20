# Mixxx Ableton Link smoke-test template

This template describes a minimal Ableton Live set for checking Mixxx Ableton
Link integration without relying on any Mixxx-specific controller, Codex
bridge device, or custom audio routing.

For build options, public controls, UI behavior, and test environment notes, see
`res/abletonlink/README.md`.

For release verification across Windows audio backends, alternate Link peers,
network churn, audio device churn, and long-running soak tests, see
`res/abletonlink/MANUAL_VERIFICATION.md`.

An example Ableton Live 12 set is included at:

`res/abletonlink/templates/example_ableton_mixxx_link_template_set.als`

The set is configured at `124 BPM` with these tracks:

- `Mixxx Link Pulse - visual clock`
- `Mixxx Audio Monitor placeholder`
- `Resample or External Monitor placeholder`

## Ableton Live setup

1. Open `example_ableton_mixxx_link_template_set.als`.
2. Enable `Link` in Live's top bar.
3. Launch the pulse clip or enable Live's metronome for a visual timing
   reference.
4. Add an instrument or route audio into the monitor tracks if you need audible
   confirmation.

On Windows, if Live does not show the `Link` button, check Live's audio
preferences. Live may hide Link when Live is using DirectX or MME; switch Live
to an ASIO device from your audio interface driver, or install and select
ASIO4ALL.

## Mixxx setup

1. Start Mixxx built with `ABLETONLINK=ON`.
2. Enable the `Link` button in the active skin.
3. Confirm `[AbletonLink],num_peers` is at least `1`.
4. Load a track with a reliable beatgrid.
5. Enable Sync on the deck, then press play.

## Expected result

Mixxx should adopt the Link session tempo, expose the current Link state through
the `[AbletonLink]` controls, and keep deck phase aligned when Sync is enabled.

Default skins expose Link status in the main toolbar or mixer area with:

- a `Link` session toggle
- the current peer count
- the current Link session BPM
- a `Start/Stop` Sync toggle
- a `Launch` button for starting synced Mixxx decks on the selected Link launch
  quantum

Useful controls for scripts, mappings, tests, and automation:

- `[AbletonLink],sync_enabled`
- `[AbletonLink],enabled`
- `[AbletonLink],start_stop_sync_enabled`
- `[AbletonLink],link_audio_enabled`
- `[AbletonLink],link_audio_receive_enabled`
- `[AbletonLink],link_audio_receive_muted`
- `[AbletonLink],link_audio_receive_gain`
- `[AbletonLink],link_audio_available`
- `[AbletonLink],link_audio_num_channels`
- `[AbletonLink],link_audio_receive_num_channels`
- `[AbletonLink],link_audio_receive_active`
- `[AbletonLink],quantized_launch`
- `[AbletonLink],launch_quantum`
- `[AbletonLink],num_peers`
- `[AbletonLink],bpm`
- `[AbletonLink],beat_distance`
- `[AbletonLink],quantum`
- `[AbletonLink],playing`
- `[AbletonLink],output_latency_micros`
- `[AbletonLink],host_time_filter_enabled`
- `[AbletonLink],next_beat_time_micros`
- `[AbletonLink],next_beat_eta_micros`
- `[AbletonLink],quantized_launch_time_micros`
- `[AbletonLink],quantized_launch_eta_micros`
