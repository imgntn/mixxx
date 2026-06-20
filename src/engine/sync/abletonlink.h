#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <QString>
#include <QTimer>

#ifdef __ABLETONLINK__
#if __has_include(<ableton/LinkAudio.hpp>)
#include <ableton/LinkAudio.hpp>
#define MIXXX_ABLETON_LINK_AUDIO 1
#else
#include <ableton/Link.hpp>
#endif
#include <ableton/link/HostTimeFilter.hpp>
#endif

#include "audio/types.h"
#include "control/controlpushbutton.h"
#include "engine/channels/enginechannel.h"
#include "engine/enginebuffer.h"
#include "engine/sync/syncable.h"
#include "engine/sync/synccontrol.h"

class ControlPotmeter;

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

#ifdef __ABLETONLINK__
#ifdef MIXXX_ABLETON_LINK_AUDIO
using MixxxAbletonLink = ableton::LinkAudio;
#else
using MixxxAbletonLink = ableton::Link;
#endif
using MixxxClockRef = MixxxAbletonLink::Clock;
using MixxxAbletonLinkSessionState = MixxxAbletonLink::SessionState;
using MixxxAbletonLinkHostTimeFilter = ableton::link::HostTimeFilter<MixxxClockRef>;
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
    /// - [AbletonLink],link_audio_enabled toggles LinkAudio channel discovery.
    /// - [AbletonLink],link_audio_sources_enabled toggles per-source LinkAudio
    ///   publishing. The main output is published when LinkAudio is enabled.
    /// - [AbletonLink],enabled mirrors the effective enabled state.
    /// - [AbletonLink],launch_quantum sets the quantized Launch grid in beats.
    /// - [AbletonLink],link_audio_available, link_audio_num_channels,
    ///   num_peers, bpm, beat_distance, quantum, playing, output_latency_micros,
    ///   host_time_filter_enabled, next_beat_time_micros,
    ///   next_beat_eta_micros, quantized_launch_time_micros, and
    ///   quantized_launch_eta_micros publish session diagnostics.
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
    bool isLinkAudioAvailable() const;
    bool isLinkAudioEnabled() const;
    void setLinkAudioEnabled(bool enabled);
    bool isLinkAudioSourcesEnabled() const;
    void setLinkAudioSourcesEnabled(bool enabled);
    bool isLinkAudioReceiveEnabled() const;
    void setLinkAudioReceiveEnabled(bool enabled);
    void requestStartStopSync(bool playing);
    void requestQuantizedLaunch();
    std::size_t numPeers() const;
    double getQuantum() const;
    double getLaunchQuantum() const;

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
    void onCallbackStart(mixxx::audio::SampleRate sampleRate, std::size_t bufferSize);
    void onCallbackStart(
            std::chrono::microseconds absTimeWhenPrevOutputBufferReachesDac,
            std::chrono::microseconds outputLatency = std::chrono::microseconds(0),
            bool hostTimeFilterEnabled = false);
    void onCallbackEnd(int sampleRate, size_t bufferSize);
    void registerLinkAudioOutput(const QString& group, const QString& name);
    void publishLinkAudioOutput(
            const QString& group,
            const CSAMPLE* pBuffer,
            std::size_t bufferSize,
            mixxx::audio::SampleRate sampleRate);
    void publishLinkAudioMainOutput(
            const CSAMPLE* pBuffer,
            std::size_t bufferSize,
            mixxx::audio::SampleRate sampleRate);
    void mixInboundLinkAudioMainOutput(
            CSAMPLE* pBuffer,
            std::size_t bufferSize,
            mixxx::audio::SampleRate sampleRate);

  private:
    void slotControlSyncEnabled(double value);
    void slotControlStartStopSyncEnabled(double value);
    void slotControlLinkAudioEnabled(double value);
    void slotControlLinkAudioSourcesEnabled(double value);
    void slotControlLinkAudioReceiveEnabled(double value);
    void slotControlLinkAudioReceiveMuted(double value);
    void slotControlLinkAudioReceiveGain(double value);
    void slotControlQuantizedLaunch(double value);
    void slotControlLaunchQuantum(double value);
    void slotLinkStartStopChanged(
            bool playing,
            std::chrono::microseconds timeForIsPlaying,
            uint64_t generation);
    void setNumPeers(std::size_t numPeers);
    void updateLinkAudioChannels();
    void updateLinkAudioOutputSinks();
    void updateLinkAudioOutputSinksLocked();
    void publishSessionState(mixxx::Bpm bpm, double beatDistance, bool playing);
    void publishCallbackTempo(double bpm);
    void applyScheduledStartStopSync();
    std::chrono::microseconds currentCallbackTime() const;
    void cancelPendingStartStopSync();
    void clearQuantizedLaunchTime();
#ifdef __ABLETONLINK__
    std::chrono::microseconds timeAtNextQuantum(
            const MixxxAbletonLinkSessionState& sessionState,
            std::chrono::microseconds time,
            double quantum) const;
    MixxxAbletonLinkSessionState captureSessionState() const;
    void commitSessionState(MixxxAbletonLinkSessionState sessionState);
#endif

    QString m_group;
    EngineSync* m_pEngineSync; // borrowed, must outlive this.
    SyncMode m_syncMode;
    std::atomic_bool m_linkEnabled;
    std::atomic_bool m_startStopSyncEnabled;
    std::atomic_bool m_linkAudioEnabled;
    std::atomic_bool m_linkAudioSourcesEnabled;
    std::atomic_bool m_linkAudioReceiveEnabled;
    std::atomic_bool m_linkAudioReceiveMuted;
    std::atomic<double> m_linkAudioReceiveGain;
    std::atomic_size_t m_numLinkAudioChannels;
    std::atomic_size_t m_numLinkAudioReceiveChannels;
    std::atomic<int> m_launchQuantumBeats;
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
#ifdef MIXXX_ABLETON_LINK_AUDIO
    std::string m_linkAudioPeerName;
#endif
    std::unique_ptr<MixxxAbletonLink> m_pLink;
    MixxxAbletonLinkHostTimeFilter m_hostTimeFilter;
    double m_audioCallbackSampleTime;
    std::optional<MixxxAbletonLinkSessionState> m_audioSessionState;
#ifdef MIXXX_ABLETON_LINK_AUDIO
    struct LinkAudioOutput {
        QString group;
        QString name;
        std::shared_ptr<ableton::LinkAudioSink> pSink;
    };
    struct LinkAudioOutputSnapshotEntry {
        QString group;
        ableton::LinkAudioSink* pSink;
    };
    using LinkAudioOutputSnapshot = std::vector<LinkAudioOutputSnapshotEntry>;
    struct LinkAudioInput {
        struct Buffer {
            std::array<CSAMPLE, kMaxEngineSamples> samples{};
            std::size_t numFrames{0};
            std::size_t numChannels{0};
            uint32_t sampleRate{0};
            ableton::LinkAudioSource::BufferHandle::Info info{};
        };

        static constexpr std::size_t kBufferSlots = 32;

        explicit LinkAudioInput(MixxxAbletonLink::Channel channel);

        bool matches(const MixxxAbletonLink::Channel& channel) const;
        void updateMetadata(const MixxxAbletonLink::Channel& channel);
        void onBuffer(ableton::LinkAudioSource::BufferHandle bufferHandle);
        bool mixInto(
                CSAMPLE* pBuffer,
                std::size_t bufferSize,
                mixxx::audio::SampleRate sampleRate,
                const MixxxAbletonLinkSessionState& sessionState,
                std::chrono::microseconds callbackTime,
                double quantum,
                CSAMPLE_GAIN gain);

        ableton::ChannelId id;
        QString name;
        QString peerName;
        std::unique_ptr<ableton::LinkAudioSource> pSource;
        std::array<Buffer, kBufferSlots> buffers;
        std::atomic_size_t writeIndex{0};
        std::atomic_size_t readIndex{0};
        std::atomic_size_t queued{0};
        std::atomic_flag writing = ATOMIC_FLAG_INIT;
    };
    using LinkAudioInputSnapshot = std::vector<std::shared_ptr<LinkAudioInput>>;
    // The audio callback reads immutable snapshots through raw atomic pointers.
    // Replaced snapshots and sinks are retired on the control side after the
    // active callback completes, avoiding locks and shared_ptr refcount traffic
    // on the callback path.
    struct RetiredLinkAudioOutputSnapshot {
        uint64_t callbackGeneration;
        std::unique_ptr<LinkAudioOutputSnapshot> pSnapshot;
    };
    struct RetiredLinkAudioInputSnapshot {
        uint64_t callbackGeneration;
        std::unique_ptr<LinkAudioInputSnapshot> pSnapshot;
    };
    struct RetiredLinkAudioSink {
        uint64_t callbackGeneration;
        std::shared_ptr<ableton::LinkAudioSink> pSink;
    };
    void retireLinkAudioSinkLocked(std::shared_ptr<ableton::LinkAudioSink> pSink);
    void retireLinkAudioOutputSnapshotLocked(std::unique_ptr<LinkAudioOutputSnapshot> pSnapshot);
    void retireLinkAudioInputSnapshotLocked(std::unique_ptr<LinkAudioInputSnapshot> pSnapshot);
    uint64_t currentLinkAudioCallbackGeneration() const;
    void pruneLinkAudioRetiredObjects();
    void pruneLinkAudioRetiredOutputsLocked();
    void pruneLinkAudioRetiredInputsLocked();
    std::mutex m_linkAudioOutputsMutex;
    std::mutex m_linkAudioInputsMutex;
    std::vector<LinkAudioOutput> m_linkAudioOutputs;
    std::unique_ptr<LinkAudioOutputSnapshot> m_pLinkAudioCurrentOutputSnapshot;
    std::unique_ptr<LinkAudioInputSnapshot> m_pLinkAudioCurrentInputSnapshot;
    std::vector<RetiredLinkAudioOutputSnapshot> m_linkAudioRetiredOutputSnapshots;
    std::vector<RetiredLinkAudioInputSnapshot> m_linkAudioRetiredInputSnapshots;
    std::vector<RetiredLinkAudioSink> m_linkAudioRetiredSinks;
    std::atomic<const LinkAudioOutputSnapshot*> m_pLinkAudioOutputSnapshot;
    std::atomic<const LinkAudioInputSnapshot*> m_pLinkAudioInputSnapshot;
    std::atomic<uint64_t> m_linkAudioCallbackGeneration;
    std::atomic_bool m_linkAudioCallbackActive;
    QTimer m_linkAudioRetireTimer;
#endif
#endif
    std::unique_ptr<ControlPushButton> m_pLinkButton;
    std::unique_ptr<ControlPushButton> m_pStartStopSyncButton;
    std::unique_ptr<ControlPushButton> m_pLinkAudioButton;
    std::unique_ptr<ControlPushButton> m_pLinkAudioSourcesButton;
    std::unique_ptr<ControlPushButton> m_pLinkAudioReceiveButton;
    std::unique_ptr<ControlPushButton> m_pLinkAudioReceiveMuteButton;
    std::unique_ptr<ControlPotmeter> m_pLinkAudioReceiveGain;
    std::unique_ptr<ControlPushButton> m_pQuantizedLaunchButton;
    std::unique_ptr<ControlObject> m_pEnabled;
    std::unique_ptr<ControlObject> m_pLinkAudioAvailable;
    std::unique_ptr<ControlObject> m_pLinkAudioNumChannels;
    std::unique_ptr<ControlObject> m_pLinkAudioReceiveNumChannels;
    std::unique_ptr<ControlObject> m_pLinkAudioReceiveActive;
    std::unique_ptr<ControlObject> m_pNumLinkPeers;
    std::unique_ptr<ControlObject> m_pBpm;
    std::unique_ptr<ControlObject> m_pBeatDistance;
    std::unique_ptr<ControlObject> m_pQuantum;
    std::unique_ptr<ControlObject> m_pLaunchQuantum;
    std::unique_ptr<ControlObject> m_pPlaying;
    std::unique_ptr<ControlObject> m_pOutputLatency;
    std::unique_ptr<ControlObject> m_pHostTimeFilterEnabled;
    std::unique_ptr<ControlObject> m_pNextBeatTime;
    std::unique_ptr<ControlObject> m_pNextBeatEta;
    std::unique_ptr<ControlObject> m_pQuantizedLaunchTime;
    std::unique_ptr<ControlObject> m_pQuantizedLaunchEta;
};
