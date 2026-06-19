# Ableton Link release plan

This plan tracks the remaining work to take the Ableton Link integration from
feature-complete to upstream-ready release quality on Windows and other
supported platforms.

## Phase 1: Clean baseline

1. Confirm the branch contains the intended Ableton Link integration as one
   upstream-ready commit.
2. Confirm the working tree is clean before starting follow-up changes.
3. Build `mixxx` and `mixxx-test`.
4. Run the Link-focused EngineSync tests.
5. Run the full test suite once before additional changes, so later failures
   have a known comparison point.

## Phase 2: Missing regression tests

1. Cover stale quantized launch cancellation when Link is disabled and
   re-enabled before the original launch time.
2. Cover external Link stop while a quantized launch is pending.
3. Cover Start/Stop Sync enablement while Link is already enabled and synced
   decks are already playing.
4. Cover Start/Stop Sync disablement while Link transport is playing.
5. Cover decks leaving sync while a Link transport action is scheduled.
6. Cover nonsynced playing decks when Link starts synced decks.
7. Cover Link transport start/stop with no synced decks.
8. Cover repeated quantized launch presses before launch.
9. Cover disabled Link state publishing for visible time and transport controls.
10. Cover Start/Stop Sync disablement without disabling normal Link tempo and
    phase participation.

## Phase 3: Stronger chaos coverage

1. Expand Link chaos operations around pending launch cancellation.
2. Expand Link chaos operations around Start/Stop Sync boundaries.
3. Expand Link chaos operations around deck sync and play changes between
   scheduling and callback processing.
4. Track invariants for finite Link BPM, phase, quantum, playing state, next
   beat time, and launch time.
5. Add focused chaos coverage for launch scheduling and cancellation.
6. Add focused chaos coverage for Start/Stop Sync enable/disable boundaries.
7. Preserve deterministic seed/iteration traces for failures.

## Phase 4: Real Link peer harness

1. Use Ableton Live, another DAW, a mobile Link application, or Ableton's
   LinkHut sample utility when available from the local Ableton Link source
   package.
2. Document how to run a second Link peer beside Mixxx on Windows.
3. Document a second-peer smoke test for peer count, tempo following, tempo
   publishing, and Start/Stop Sync.
4. Add automation for second-peer smoke testing if it is stable enough for local
   developer use.

## Phase 5: Windows driver verification

1. Verify Ableton Live exposes its Link button when Live uses an ASIO driver.
2. Verify Ableton Live exposes its Link button when Live uses ASIO4ALL.
3. Verify Ableton Live may hide its own Link button when Live uses DirectX/MME.
4. Confirm Mixxx still exposes its Link controls whenever Mixxx is built with
   Ableton Link support.
5. Document the Windows Ableton Live audio backend behavior clearly.
6. Document firewall, VPN, and multi-adapter discovery checks.

## Phase 6: Network churn testing

1. Check peer recovery after firewall block/unblock.
2. Check peer recovery after Wi-Fi off/on.
3. Check peer recovery after Ethernet/Wi-Fi adapter switching.
4. Check peer recovery after sleep/wake.
5. Check peer recovery after disabling and re-enabling a network adapter.
6. Verify no stale quantized launch survives network loss.

## Phase 7: Audio device churn testing

1. Change audio buffer size while Link is enabled.
2. Change sample rate while Link is enabled.
3. Switch between ASIO devices while Link is enabled.
4. Stop and restart the audio engine while Link is enabled.
5. Disconnect and reconnect an audio interface if available.
6. Verify finite Link state and no stale scheduled launch after audio restart.

## Phase 8: Long-run soak

1. Run Mixxx and Ableton Live linked for at least one hour.
2. Repeat with one synced Mixxx deck playing.
3. Periodically start and stop Ableton Live transport.
4. Periodically start and stop Mixxx deck transport.
5. Trigger quantized launch repeatedly.
6. Watch for drift, stale UI, missed starts, CPU growth, or memory growth.
7. Repeat for at least two hours if the first hour is clean.

## Phase 9: UI naming and clarity

1. Replace or clarify ambiguous Start/Stop Sync labeling.
2. Keep Start/Stop Sync tooltip text explicit.
3. Keep Quantized Launch tooltip text explicit.
4. Ensure all Link controls have tooltips in every edited skin.
5. Ensure tooltip and control-picker strings match the actual behavior.

## Phase 10: UI layout and visual polish

1. Group Link controls in a subtle shared visual band in each edited skin.
2. Keep Link enable, peer count, session BPM, Start/Stop Sync, and Launch
   visually connected.
3. Improve spacing around Start/Stop Sync, session BPM, peer count, and nearby
   clock controls.
4. Improve Link BPM contrast.
5. Make peer count readable without making it visually dominant.
6. Show pending quantized launch state if the skin system supports it cleanly.
7. Verify the Link group fits common desktop widths.

## Phase 11: UI behavior verification

1. Parse all edited XML skins.
2. Launch Mixxx and inspect every edited skin.
3. Confirm Link enable is understandable.
4. Confirm Start/Stop Sync is distinguishable from Link enable.
5. Confirm Launch reads as a momentary action.
6. Confirm BPM and peer count are readable.
7. Confirm Link controls do not overlap adjacent clock or toolbar content.
8. Check Windows display scaling at 100%, 125%, and 150%.

## Phase 12: Documentation finalization

1. Explain what Link syncs: tempo and beat phase.
2. Explain what Start/Stop Sync does.
3. Explain what Quantized Launch does.
4. Explain how to use Mixxx with Ableton Live.
5. Explain the Windows Ableton Live DirectX/MME Link button behavior.
6. Explain firewall, VPN, and network discovery issues.
7. Document all public `[AbletonLink]` controls.
8. Mark time controls as primarily useful for skins, scripts, mappings, tests,
   and diagnostics.

## Phase 13: Final code review pass

1. Re-read Link code for thread assumptions.
2. Confirm the audio callback path avoids avoidable locks and allocations.
3. Confirm app-thread callbacks use guarded Qt object lifetimes.
4. Confirm pending transport state is atomic.
5. Confirm generation invalidation covers stale queued callbacks and timers.
6. Confirm disabling Link clears user-visible pending state.
7. Confirm disabling Start/Stop Sync does not disable tempo and phase sync.
8. Confirm every skin uses the correct ConfigKeys.
9. Confirm docs match actual control names.

## Phase 14: Final test matrix

1. Build `mixxx` and `mixxx-test`.
2. Run Link-focused EngineSync tests.
3. Run repeated deterministic chaos tests.
4. Run full `EngineSyncTest.*`.
5. Run full CTest.
6. Parse edited skin XML.
7. Run manual Ableton Live smoke testing on Windows with ASIO.
8. Run manual Ableton Live smoke testing on Windows with ASIO4ALL.
9. Run manual Ableton Live DirectX/MME visibility check.
10. Run manual LinkHut or second-peer smoke testing.
11. Run manual UI checks across edited skins.

## Phase 15: Upstream submission prep

1. Keep the final branch history clean.
2. Keep behavior, UI, docs, and test changes explainable from the commit
   message and PR description.
3. Include test evidence in the PR description.
4. Include manual Windows Ableton Live results in the PR description.
5. State known limitations clearly, including the split between one-beat deck
   sync quantum, selectable launch quantum, and environment-dependent network
   discovery behavior.
