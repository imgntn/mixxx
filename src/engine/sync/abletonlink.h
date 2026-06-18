#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <QTimer>

#ifdef __ABLETONLINK__
#include <ableton/Link.hpp>
#include <ableton/platforms/stl/Clock.hpp>
#endif

#include "control/controlpushbutton.h"
#include "engine/channels/enginechannel.h"
#include "engine/enginebuffer.h"
#include "engine/sync/syncable.h"
#include "engine/sync/synccontrol.h"

/// This class manages a link session.
/// Read & update (get & set) this session for Mixxx to be a synced Link
/// participant (bpm & phase)
///
/// Ableton Link Readme (lib/ableton-link/README.md)
/// Documentation in the header (lib/ableton-link/include/ableton/Link.hpp)
/// Ableton provides a command line tool (LinkHut) for debugging Link programs
/// (instructions in the Readme)
///
/// Ableton recommends getting/setting the link session from the audio thread
/// for maximum timing accuracy. Call the appropriate, realtime-safe functions
/// from the audio callback to do this.

// std::chrono::steady_clock
// -> selected by keyword 'stl' in ableton-link
// Note that the resolution of std::chrono::steady_clock is not guaranteed
// to be high resolution, but it is guaranteed to be monotonic.
// However, on all major platforms, it is high resolution enough.
#ifdef __ABLETONLINK__
using MixxxClockRef = ableton::platforms::stl::Clock;
using MixxxAbletonLink = ableton::BasicLink<MixxxClockRef>;
using MixxxAbletonLinkSessionState = MixxxAbletonLink::SessionState;
#endif

class AbletonLink : public QObject, public Syncable {
    Q_OBJECT
  public:
    AbletonLink(const QString& group, EngineSync* pEngineSync);
    ~AbletonLink() override;

    const QString& getGroup() const override {
        return m_group;
    }
    EngineChannel* getChannel() const override {
        return nullptr;
    }

    /// Public Link controls exposed to skins, controller mappings, scripts, and
    /// tests:
    /// - [AbletonLink],sync_enabled toggles session participation.
    /// - [AbletonLink],start_stop_sync_enabled toggles Link transport sync.
    /// - [AbletonLink],enabled mirrors the effective enabled state.
    /// - [AbletonLink],num_peers, bpm, beat_distance, quantum, and playing
    ///   publish read-only session status.
    ///
    /// Keep these controls stable. They are the integration surface for skins
    /// and downstream automation, not just implementation details of this
    /// Syncable.

    /// Notify a Syncable that their mode has changed. The Syncable must record
    /// this mode and return the latest mode in response to getMode().
    void setSyncMode(SyncMode mode) override;

    /// Notify a Syncable that it is now the only currently-playing syncable.
    void notifyUniquePlaying() override;

    /// Notify a Syncable that they should sync phase.
    void requestSync() override;

    /// Must NEVER return a mode that was not set directly via
    /// notifySyncModeChanged.
    SyncMode getSyncMode() const override;

    /// Only relevant for player Syncables.
    bool isPlaying() const override;
    bool isAudible() const override;
    bool isQuantized() const override;

    bool isAvailable() const;
    bool isEnabled() const;
    void setEnabled(bool enabled);
    bool isStartStopSyncEnabled() const;
    void setStartStopSyncEnabled(bool enabled);
    void requestStartStopSync(bool playing);
    void requestQuantizedLaunch();
    std::size_t numPeers() const;
    double getQuantum() const;

    /// Gets the current speed of the syncable in bpm (bpm * rate slider), doesn't
    /// include scratch or FF/REW values.
    mixxx::Bpm getBpm() const override;

    /// Gets the beat distance as a fraction from 0 to 1
    double getBeatDistance() const override;

    /// Gets the speed of the syncable if it was playing at 1.0 rate.
    mixxx::Bpm getBaseBpm() const override;

    /// The following functions are used to tell syncables about the state of the
    /// current Sync Master.
    /// Must never result in a call to
    /// SyncableListener::notifyBeatDistanceChanged or signal loops could occur.
    void updateLeaderBeatDistance(double beatDistance) override;

    /// Enforces the immediate change of the beat distance of all Link peers
    void forceUpdateLeaderBeatDistance(double beatDistance);

    /// Must never result in a call to SyncableListener::notifyBpmChanged or
    /// signal loops could occur.
    void updateLeaderBpm(mixxx::Bpm bpm) override;

    void notifyLeaderParamSource() override;

    /// Combines the above three calls into one, since they are often set
    /// simultaneously.  Avoids redundant recalculation that would occur by
    /// using the three calls separately.
    void reinitLeaderParams(double beatDistance, mixxx::Bpm baseBpm, mixxx::Bpm bpm) override;

    /// Must never result in a call to
    /// SyncableListener::notifyInstantaneousBpmChanged or signal loops could
    /// occur.
    void updateInstantaneousBpm(mixxx::Bpm bpm) override;

    void onCallbackStart();
    void onCallbackStart(std::chrono::microseconds absTimeWhenPrevOutputBufferReachesDac);
    void onCallbackEnd(int sampleRate, size_t bufferSize);

  private:
    void slotControlSyncEnabled(double value);
    void slotControlStartStopSyncEnabled(double value);
    void slotControlQuantizedLaunch(double value);
    void slotLinkStartStopChanged(
            bool playing,
            std::chrono::microseconds timeForIsPlaying,
            uint64_t generation);
    void setNumPeers(std::size_t numPeers);
    void publishSessionState(mixxx::Bpm bpm, double beatDistance, bool playing);
    void applyScheduledStartStopSync();
    std::chrono::microseconds currentCallbackTime() const;
    void cancelPendingStartStopSync();
    void clearQuantizedLaunchTime();
#ifdef __ABLETONLINK__
    std::chrono::microseconds timeAtNextBeat(
            const MixxxAbletonLinkSessionState& sessionState,
            std::chrono::microseconds time) const;
    MixxxAbletonLinkSessionState captureSessionState() const;
    void commitSessionState(MixxxAbletonLinkSessionState sessionState);
#endif

    QString m_group;
    EngineSync* m_pEngineSync; // borrowed, must outlive this.
    SyncMode m_syncMode;
    std::atomic_bool m_linkEnabled;
    std::atomic_bool m_startStopSyncEnabled;
    std::atomic<int> m_pendingStartStopSyncState;
    std::atomic_size_t m_numPeers;
    std::atomic<uint64_t> m_startStopSyncGeneration;

    mixxx::Bpm m_oldTempo;

    std::atomic<int64_t> m_absTimeWhenPrevOutputBufferReachesDacMicros;
    std::chrono::microseconds m_lastStartStopSyncChangeTime;
    std::chrono::microseconds m_quantizedLaunchTime;
    QTimer m_startStopSyncTimer;
    bool m_scheduledStartStopSyncPlaying;
    std::chrono::microseconds m_scheduledStartStopSyncTime;
    uint64_t m_scheduledStartStopSyncGeneration;

#ifdef __ABLETONLINK__
    std::unique_ptr<MixxxAbletonLink> m_pLink;
    std::optional<MixxxAbletonLinkSessionState> m_audioSessionState;
#endif
    std::unique_ptr<ControlPushButton> m_pLinkButton;
    std::unique_ptr<ControlPushButton> m_pStartStopSyncButton;
    std::unique_ptr<ControlPushButton> m_pQuantizedLaunchButton;
    std::unique_ptr<ControlObject> m_pEnabled;
    std::unique_ptr<ControlObject> m_pNumLinkPeers;
    std::unique_ptr<ControlObject> m_pBpm;
    std::unique_ptr<ControlObject> m_pBeatDistance;
    std::unique_ptr<ControlObject> m_pQuantum;
    std::unique_ptr<ControlObject> m_pPlaying;
    std::unique_ptr<ControlObject> m_pNextBeatTime;
    std::unique_ptr<ControlObject> m_pQuantizedLaunchTime;
};
