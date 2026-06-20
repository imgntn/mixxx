#include "engine/sync/abletonlink.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

#include "control/controlpotmeter.h"
#include "control/controlobject.h"
#include "engine/sync/enginesync.h"
#include "moc_abletonlink.cpp"
#include "preferences/usersettings.h"
#include "util/defs.h"
#include "util/sample.h"
#include "waveform/visualplayposition.h"

namespace {
constexpr double kDefaultLinkTempo = 120.0;
constexpr double kBeatSyncQuantum = 1.0;
constexpr double kLinkAudioReceiveLatencyBeats = 4.0;
constexpr int kDefaultLaunchQuantumBeats = 1;
constexpr std::array<int, 4> kSupportedLaunchQuantumBeats{1, 2, 4, 8};
constexpr int kNoPendingStartStopSyncState = -1;
constexpr int kPendingStop = 0;
constexpr int kPendingStart = 1;
#ifdef __ABLETONLINK__
#ifdef MIXXX_ABLETON_LINK_AUDIO
constexpr char kLinkAudioMainOutputName[] = "Mixxx Main";
std::string makeLinkAudioPeerName() {
    return QStringLiteral("Mixxx %1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
            .toStdString();
}
#endif
thread_local const AbletonLink* s_pAudioCallbackLink = nullptr;
#endif

int normalizeLaunchQuantumBeats(double requested, int fallback) {
    if (!std::isfinite(requested)) {
        return fallback;
    }
    for (const auto quantum : kSupportedLaunchQuantumBeats) {
        if (std::abs(requested - static_cast<double>(quantum)) < 1e-9) {
            return quantum;
        }
    }
    return fallback;
}

double linearInterpolate(
        double value,
        double inMin,
        double inMax,
        double outMin,
        double outMax) {
    const double inRange = inMax - inMin;
    if (std::abs(inRange) < 1e-12) {
        return outMin;
    }
    return outMin + ((value - inMin) / inRange) * (outMax - outMin);
}
} // anonymous namespace

#if defined(__ABLETONLINK__) && defined(MIXXX_ABLETON_LINK_AUDIO)
AbletonLink::LinkAudioInput::LinkAudioInput(MixxxAbletonLink::Channel channel)
        : id(channel.id),
          name(QString::fromStdString(channel.name)),
          peerName(QString::fromStdString(channel.peerName)) {
}

bool AbletonLink::LinkAudioInput::matches(const MixxxAbletonLink::Channel& channel) const {
    return id == channel.id;
}

void AbletonLink::LinkAudioInput::updateMetadata(const MixxxAbletonLink::Channel& channel) {
    name = QString::fromStdString(channel.name);
    peerName = QString::fromStdString(channel.peerName);
}

void AbletonLink::LinkAudioInput::onBuffer(
        ableton::LinkAudioSource::BufferHandle bufferHandle) {
    if (!bufferHandle.samples ||
            bufferHandle.info.numFrames == 0 ||
            bufferHandle.info.numChannels == 0 ||
            bufferHandle.info.sampleRate == 0) {
        return;
    }

    if (queued.load(std::memory_order_acquire) >= kBufferSlots) {
        return;
    }

    const std::size_t write = writeIndex.load(std::memory_order_relaxed);
    Buffer& buffer = buffers[write];
    constexpr std::size_t kOutputChannels = 2;
    const std::size_t numFrames = std::min<std::size_t>(
            bufferHandle.info.numFrames,
            kMaxEngineSamples / kOutputChannels);
    buffer.numFrames = numFrames;
    buffer.numChannels = kOutputChannels;
    buffer.sampleRate = bufferHandle.info.sampleRate;
    buffer.info = bufferHandle.info;
    buffer.info.numChannels = kOutputChannels;
    buffer.info.numFrames = numFrames;

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        const auto* inputFrame = bufferHandle.samples +
                (frame * bufferHandle.info.numChannels);
        const CSAMPLE left = static_cast<CSAMPLE>(inputFrame[0]) /
                static_cast<CSAMPLE>(-SAMPLE_MINIMUM);
        const CSAMPLE right = bufferHandle.info.numChannels > 1
                ? static_cast<CSAMPLE>(inputFrame[1]) /
                        static_cast<CSAMPLE>(-SAMPLE_MINIMUM)
                : left;
        buffer.samples[(frame * kOutputChannels)] = left;
        buffer.samples[(frame * kOutputChannels) + 1] = right;
    }

    writeIndex.store((write + 1) % kBufferSlots, std::memory_order_release);
    queued.fetch_add(1, std::memory_order_release);
}

bool AbletonLink::LinkAudioInput::mixInto(
        CSAMPLE* pBuffer,
        std::size_t bufferSize,
        mixxx::audio::SampleRate sampleRate,
        const MixxxAbletonLinkSessionState& sessionState,
        std::chrono::microseconds callbackTime,
        double quantum,
        CSAMPLE_GAIN gain) {
    if (!pBuffer || bufferSize == 0 || !sampleRate.isValid() || gain <= 0) {
        return false;
    }

    constexpr std::size_t kOutputChannels = 2;
    const std::size_t outputFrames = bufferSize / kOutputChannels;
    if (outputFrames == 0) {
        return false;
    }

    const auto outputDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::duration<double>(
                    static_cast<double>(outputFrames) /
                    static_cast<double>(sampleRate.value())));
    const double targetBeginBeat =
            sessionState.beatAtTime(callbackTime, quantum) - kLinkAudioReceiveLatencyBeats;
    const double targetEndBeat =
            sessionState.beatAtTime(callbackTime + outputDuration, quantum) -
            kLinkAudioReceiveLatencyBeats;
    bool mixed = false;

    while (queued.load(std::memory_order_acquire) > 0) {
        const std::size_t read = readIndex.load(std::memory_order_relaxed);
        const Buffer& buffer = buffers[read];
        const auto endBeat = buffer.info.endBeats(sessionState, quantum);
        if (!endBeat || *endBeat <= targetBeginBeat) {
            readIndex.store((read + 1) % kBufferSlots, std::memory_order_release);
            queued.fetch_sub(1, std::memory_order_release);
            continue;
        }
        break;
    }

    const std::size_t queuedBuffers = queued.load(std::memory_order_acquire);
    if (queuedBuffers == 0) {
        return false;
    }

    auto bufferAt = [this](std::size_t offset) -> const Buffer& {
        return buffers[(readIndex.load(std::memory_order_relaxed) + offset) % kBufferSlots];
    };

    const Buffer& firstBuffer = bufferAt(0);
    const auto firstBeginBeat = firstBuffer.info.beginBeats(sessionState, quantum);
    const auto firstEndBeat = firstBuffer.info.endBeats(sessionState, quantum);
    if (!firstBeginBeat || !firstEndBeat || *firstBeginBeat > targetBeginBeat) {
        return false;
    }

    double startFramePos = linearInterpolate(
            targetBeginBeat,
            *firstBeginBeat,
            *firstEndBeat,
            0.0,
            static_cast<double>(firstBuffer.numFrames));
    startFramePos = std::clamp(
            startFramePos,
            0.0,
            static_cast<double>(firstBuffer.numFrames));

    double totalSourceFrames = 0.0;
    bool foundEnd = false;
    for (std::size_t offset = 0; offset < queuedBuffers; ++offset) {
        const Buffer& buffer = bufferAt(offset);
        if (buffer.numFrames == 0 || buffer.sampleRate == 0) {
            continue;
        }
        const auto beginBeat = buffer.info.beginBeats(sessionState, quantum);
        const auto endBeat = buffer.info.endBeats(sessionState, quantum);
        if (!beginBeat || !endBeat) {
            break;
        }

        if (targetEndBeat >= *beginBeat && targetEndBeat < *endBeat) {
            totalSourceFrames += linearInterpolate(
                    targetEndBeat,
                    *beginBeat,
                    *endBeat,
                    0.0,
                    static_cast<double>(buffer.numFrames));
            foundEnd = true;
            break;
        }
        totalSourceFrames += static_cast<double>(buffer.numFrames);
    }

    totalSourceFrames -= startFramePos;
    if (!foundEnd || totalSourceFrames <= 0.0) {
        return false;
    }

    auto sampleAt = [&bufferAt, queuedBuffers](std::size_t absoluteFrame, std::size_t channel) {
        for (std::size_t offset = 0; offset < queuedBuffers; ++offset) {
            const Buffer& buffer = bufferAt(offset);
            if (absoluteFrame < buffer.numFrames) {
                return buffer.samples[(absoluteFrame * kOutputChannels) + channel];
            }
            absoluteFrame -= buffer.numFrames;
        }
        return CSAMPLE(0);
    };

    const double frameIncrement = totalSourceFrames / static_cast<double>(outputFrames);
    double readPos = startFramePos;
    for (std::size_t outputFrame = 0; outputFrame < outputFrames; ++outputFrame) {
        const std::size_t inputFrame = static_cast<std::size_t>(
                std::max(0.0, std::floor(readPos)));
        pBuffer[(outputFrame * kOutputChannels)] +=
                sampleAt(inputFrame, 0) * gain;
        pBuffer[(outputFrame * kOutputChannels) + 1] +=
                sampleAt(inputFrame, 1) * gain;
        mixed = true;
        readPos += frameIncrement;
    }

    while (queued.load(std::memory_order_acquire) > 0) {
        const std::size_t read = readIndex.load(std::memory_order_relaxed);
        const Buffer& buffer = buffers[read];
        const auto endBeat = buffer.info.endBeats(sessionState, quantum);
        if (!endBeat || *endBeat <= targetEndBeat) {
            readIndex.store((read + 1) % kBufferSlots, std::memory_order_release);
            queued.fetch_sub(1, std::memory_order_release);
            continue;
        }
        break;
    }

    return mixed;
}
#endif

AbletonLink::AbletonLink(const QString& group, EngineSync* pEngineSync)
        : m_group(group),
          m_pEngineSync(pEngineSync),
          m_syncMode(SyncMode::None),
          m_linkEnabled(false),
          m_startStopSyncEnabled(false),
          m_linkAudioEnabled(false),
          m_linkAudioSourcesEnabled(false),
          m_linkAudioReceiveEnabled(false),
          m_linkAudioReceiveMuted(false),
          m_linkAudioReceiveGain(1.0),
          m_numLinkAudioChannels(0),
          m_numLinkAudioReceiveChannels(0),
          m_launchQuantumBeats(kDefaultLaunchQuantumBeats),
          m_pendingStartStopSyncState(kNoPendingStartStopSyncState),
          m_numPeers(0),
          m_startStopSyncGeneration(0),
          m_oldTempo(),
          m_absTimeWhenPrevOutputBufferReachesDacMicros(0),
          m_lastStartStopSyncChangeTime(0),
          m_quantizedLaunchTime(0),
          m_startStopSyncTimer(this),
          m_scheduledStartStopSyncPlaying(false),
          m_scheduledStartStopSyncTime(0),
          m_scheduledStartStopSyncGeneration(0),
#ifdef __ABLETONLINK__
#ifdef MIXXX_ABLETON_LINK_AUDIO
          m_linkAudioPeerName(makeLinkAudioPeerName()),
          m_pLink(std::make_unique<MixxxAbletonLink>(
                  kDefaultLinkTempo,
                  m_linkAudioPeerName)),
#else
          m_pLink(std::make_unique<MixxxAbletonLink>(kDefaultLinkTempo)),
#endif
          m_hostTimeFilter(),
          m_audioCallbackSampleTime(0.0),
          m_audioSessionState(),
#ifdef MIXXX_ABLETON_LINK_AUDIO
          m_linkAudioOutputs(),
          m_linkAudioInputs(std::make_shared<std::vector<std::shared_ptr<LinkAudioInput>>>()),
#endif
#endif
          m_pLinkButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "sync_enabled"),
                  true)),
          m_pStartStopSyncButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "start_stop_sync_enabled"),
                  true)),
          m_pLinkAudioButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "link_audio_enabled"),
                  true)),
          m_pLinkAudioSourcesButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "link_audio_sources_enabled"),
                  true)),
          m_pLinkAudioReceiveButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "link_audio_receive_enabled"),
                  true)),
          m_pLinkAudioReceiveMuteButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "link_audio_receive_muted"),
                  true)),
          m_pLinkAudioReceiveGain(std::make_unique<ControlPotmeter>(
                  ConfigKey(group, "link_audio_receive_gain"),
                  0.0,
                  2.0,
                  false,
                  true,
                  false,
                  true,
                  1.0)),
          m_pQuantizedLaunchButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "quantized_launch"))),
          m_pEnabled(std::make_unique<ControlObject>(ConfigKey(group, "enabled"))),
          m_pLinkAudioAvailable(std::make_unique<ControlObject>(
                  ConfigKey(group, "link_audio_available"))),
          m_pLinkAudioNumChannels(std::make_unique<ControlObject>(
                  ConfigKey(group, "link_audio_num_channels"))),
          m_pLinkAudioReceiveNumChannels(std::make_unique<ControlObject>(
                  ConfigKey(group, "link_audio_receive_num_channels"))),
          m_pLinkAudioReceiveActive(std::make_unique<ControlObject>(
                  ConfigKey(group, "link_audio_receive_active"))),
          m_pNumLinkPeers(std::make_unique<ControlObject>(ConfigKey(group, "num_peers"))),
          m_pBpm(std::make_unique<ControlObject>(ConfigKey(group, "bpm"))),
          m_pBeatDistance(std::make_unique<ControlObject>(ConfigKey(group, "beat_distance"))),
          m_pQuantum(std::make_unique<ControlObject>(ConfigKey(group, "quantum"))),
          m_pLaunchQuantum(std::make_unique<ControlObject>(
                  ConfigKey(group, "launch_quantum"),
                  true,
                  false,
                  true,
                  kDefaultLaunchQuantumBeats)),
          m_pPlaying(std::make_unique<ControlObject>(ConfigKey(group, "playing"))),
          m_pOutputLatency(std::make_unique<ControlObject>(
                  ConfigKey(group, "output_latency_micros"))),
          m_pHostTimeFilterEnabled(std::make_unique<ControlObject>(
                  ConfigKey(group, "host_time_filter_enabled"))),
          m_pNextBeatTime(std::make_unique<ControlObject>(ConfigKey(group, "next_beat_time_micros"))),
          m_pNextBeatEta(std::make_unique<ControlObject>(
                  ConfigKey(group, "next_beat_eta_micros"))),
          m_pQuantizedLaunchTime(
                  std::make_unique<ControlObject>(ConfigKey(group, "quantized_launch_time_micros"))),
          m_pQuantizedLaunchEta(std::make_unique<ControlObject>(
                  ConfigKey(group, "quantized_launch_eta_micros"))) {
    m_pLinkButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkButton->setStates(2);
    m_pStartStopSyncButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pStartStopSyncButton->setStates(2);
    m_pLinkAudioButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkAudioButton->setStates(2);
    m_pLinkAudioSourcesButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkAudioSourcesButton->setStates(2);
    m_pLinkAudioReceiveButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkAudioReceiveButton->setStates(2);
    m_pLinkAudioReceiveMuteButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkAudioReceiveMuteButton->setStates(2);
    m_pQuantizedLaunchButton->setButtonMode(mixxx::control::ButtonMode::Trigger);

    connect(m_pLinkButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlSyncEnabled);
    connect(m_pStartStopSyncButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlStartStopSyncEnabled);
    connect(m_pLinkAudioButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlLinkAudioEnabled);
    connect(m_pLinkAudioSourcesButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlLinkAudioSourcesEnabled);
    connect(m_pLinkAudioReceiveButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlLinkAudioReceiveEnabled);
    connect(m_pLinkAudioReceiveMuteButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlLinkAudioReceiveMuted);
    connect(m_pLinkAudioReceiveGain.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlLinkAudioReceiveGain);
    connect(m_pQuantizedLaunchButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlQuantizedLaunch);
    m_pLaunchQuantum->connectValueChangeRequest(
            this,
            &AbletonLink::slotControlLaunchQuantum,
            Qt::DirectConnection);
    m_startStopSyncTimer.setSingleShot(true);
    connect(&m_startStopSyncTimer,
            &QTimer::timeout,
            this,
            &AbletonLink::applyScheduledStartStopSync);

    m_pEnabled->setReadOnly();
    m_pLinkAudioAvailable->setReadOnly();
    m_pLinkAudioNumChannels->setReadOnly();
    m_pLinkAudioReceiveNumChannels->setReadOnly();
    m_pLinkAudioReceiveActive->setReadOnly();
    m_pNumLinkPeers->setReadOnly();
    m_pBpm->setReadOnly();
    m_pBeatDistance->setReadOnly();
    m_pQuantum->setReadOnly();
    m_pPlaying->setReadOnly();
    m_pOutputLatency->setReadOnly();
    m_pHostTimeFilterEnabled->setReadOnly();
    m_pNextBeatTime->setReadOnly();
    m_pNextBeatEta->setReadOnly();
    m_pQuantizedLaunchTime->setReadOnly();
    m_pQuantizedLaunchEta->setReadOnly();

#ifdef __ABLETONLINK__
    // The callback is invoked on a Link - managed thread.
    QPointer<AbletonLink> pThis(this);
    m_pLink->setNumPeersCallback([pThis](std::size_t numPeers) {
        if (!pThis) {
            return;
        }
        QMetaObject::invokeMethod(
                pThis.data(),
                [pThis, numPeers]() {
                    if (pThis) {
                        pThis->setNumPeers(numPeers);
                    }
                },
                Qt::QueuedConnection);
    });
    m_pLink->setTempoCallback([pThis](double bpm) {
        if (!pThis) {
            return;
        }
        QMetaObject::invokeMethod(
                pThis.data(),
                [pThis, bpm]() {
                    if (pThis) {
                        pThis->publishCallbackTempo(bpm);
                    }
                },
                Qt::QueuedConnection);
    });
#ifdef MIXXX_ABLETON_LINK_AUDIO
    m_pLink->setChannelsChangedCallback([pThis]() {
        if (!pThis) {
            return;
        }
        QMetaObject::invokeMethod(
                pThis.data(),
                [pThis]() {
                    if (pThis) {
                        pThis->updateLinkAudioChannels();
                    }
                },
                Qt::QueuedConnection);
    });
#endif
    m_pLink->setStartStopCallback([pThis](bool playing) {
        if (!pThis) {
            return;
        }
        const auto sessionState = pThis->m_pLink->captureAppSessionState();
        const auto timeForIsPlaying = sessionState.timeForIsPlaying();
        const uint64_t generation = pThis->m_startStopSyncGeneration.load(
                std::memory_order_acquire);
        QMetaObject::invokeMethod(
                pThis.data(),
                [pThis, playing, timeForIsPlaying, generation]() {
                    if (pThis) {
                        pThis->slotLinkStartStopChanged(
                                playing,
                                timeForIsPlaying,
                                generation);
                    }
                },
                Qt::QueuedConnection);
    });
#endif

    setNumPeers(0);
    updateLinkAudioChannels();
#ifdef MIXXX_ABLETON_LINK_AUDIO
    registerLinkAudioOutput(
            QStringLiteral("[Main]"),
            QStringLiteral("Mixxx Main"));
#endif
    m_pQuantum->forceSet(getQuantum());
    slotControlLaunchQuantum(m_pLaunchQuantum->get());
    setLinkAudioEnabled(m_pLinkAudioButton->get() > 0);
    setLinkAudioSourcesEnabled(m_pLinkAudioSourcesButton->get() > 0);
    setLinkAudioReceiveEnabled(m_pLinkAudioReceiveButton->get() > 0);
    slotControlLinkAudioReceiveMuted(m_pLinkAudioReceiveMuteButton->get());
    slotControlLinkAudioReceiveGain(m_pLinkAudioReceiveGain->get());
    setStartStopSyncEnabled(m_pStartStopSyncButton->get() > 0);
    setEnabled(m_pLinkButton->get() > 0);
    publishSessionState(mixxx::Bpm(kDefaultLinkTempo), 0.0, false);
    m_pOutputLatency->forceSet(0.0);
    m_pHostTimeFilterEnabled->forceSet(0.0);
    m_pNextBeatTime->forceSet(0.0);
    m_pNextBeatEta->forceSet(0.0);
    m_pLinkAudioReceiveNumChannels->forceSet(0.0);
    m_pLinkAudioReceiveActive->forceSet(0.0);
    clearQuantizedLaunchTime();
}

AbletonLink::~AbletonLink() {
#ifdef __ABLETONLINK__
    // Stop Link activity and remove callbacks before destroying ControlObjects.
    m_pLink->setNumPeersCallback([](std::size_t) {});
    m_pLink->setTempoCallback([](double) {});
#ifdef MIXXX_ABLETON_LINK_AUDIO
    m_pLink->setChannelsChangedCallback([]() {});
#endif
    m_pLink->setStartStopCallback([](bool) {});
    m_pLink->enable(false);

    // Destroy Link first to ensure all Link-managed threads are stopped before
    // automatically destroying m_pNumLinkPeers and m_pLinkButton afterwards,
    // which may be accessed by in-flight callbacks otherwise.
    m_pLink.reset();
#endif
}

void AbletonLink::slotControlSyncEnabled(double controlButtonValue) {
    setEnabled(controlButtonValue > 0);
}

void AbletonLink::slotControlStartStopSyncEnabled(double value) {
    setStartStopSyncEnabled(value > 0);
}

void AbletonLink::slotControlLinkAudioEnabled(double value) {
    setLinkAudioEnabled(value > 0);
}

void AbletonLink::slotControlLinkAudioSourcesEnabled(double value) {
    setLinkAudioSourcesEnabled(value > 0);
}

void AbletonLink::slotControlLinkAudioReceiveEnabled(double value) {
    setLinkAudioReceiveEnabled(value > 0);
}

void AbletonLink::slotControlLinkAudioReceiveMuted(double value) {
    m_linkAudioReceiveMuted.store(value > 0, std::memory_order_relaxed);
    m_pLinkAudioReceiveMuteButton->forceSet(value > 0 ? 1.0 : 0.0);
}

void AbletonLink::slotControlLinkAudioReceiveGain(double value) {
    const double gain = std::clamp(value, 0.0, 2.0);
    m_linkAudioReceiveGain.store(gain, std::memory_order_relaxed);
    m_pLinkAudioReceiveGain->forceSet(gain);
}

void AbletonLink::slotControlQuantizedLaunch(double value) {
    if (value > 0) {
        requestQuantizedLaunch();
    }
}

void AbletonLink::slotControlLaunchQuantum(double value) {
    const auto quantum = normalizeLaunchQuantumBeats(
            value,
            m_launchQuantumBeats.load(std::memory_order_relaxed));
    m_launchQuantumBeats.store(quantum, std::memory_order_relaxed);
    m_pLaunchQuantum->forceSet(static_cast<double>(quantum));
}

void AbletonLink::slotLinkStartStopChanged(
        bool playing,
        std::chrono::microseconds timeForIsPlaying,
        uint64_t generation) {
    if (generation != m_startStopSyncGeneration.load(std::memory_order_acquire)) {
        return;
    }
    if (!isEnabled() || !isStartStopSyncEnabled()) {
        return;
    }
    if (timeForIsPlaying <= m_lastStartStopSyncChangeTime) {
        return;
    }
    m_lastStartStopSyncChangeTime = timeForIsPlaying;

#ifdef __ABLETONLINK__
    const auto now = m_pLink->clock().micros();
    if (timeForIsPlaying <= now) {
        m_pEngineSync->setLinkTransportPlaying(playing);
        if (!playing || timeForIsPlaying == m_quantizedLaunchTime) {
            clearQuantizedLaunchTime();
        }
        return;
    }

    const auto delayMicros = std::chrono::duration_cast<std::chrono::microseconds>(
            timeForIsPlaying - now);
    const auto delayMillis = (delayMicros.count() + 999) / 1000;
    const auto boundedDelay = std::min<long long>(
            delayMillis,
            std::numeric_limits<int>::max());
    m_scheduledStartStopSyncPlaying = playing;
    m_scheduledStartStopSyncTime = timeForIsPlaying;
    m_scheduledStartStopSyncGeneration = generation;
    m_startStopSyncTimer.start(static_cast<int>(boundedDelay));
#else
    Q_UNUSED(playing);
    Q_UNUSED(timeForIsPlaying);
#endif
}

void AbletonLink::setSyncMode(SyncMode syncMode) {
    m_syncMode = syncMode;
}

void AbletonLink::notifyUniquePlaying() {
}

void AbletonLink::requestSync() {
}

SyncMode AbletonLink::getSyncMode() const {
    return m_syncMode;
}

bool AbletonLink::isPlaying() const {
    if (!isEnabled()) {
        return false;
    }
    if (numPeers() < 1) {
        return false;
    }

    // Note, that ableton::Link::SessionState.isPlaying() is an optional Ableton
    // Link feature which is unrelated in functionality to the same named isPlaying()
    // state of Mixxx syncables.

    // If no Mixxx deck is playing, but Link is enabled and has peers,
    // we sync the next deck with Sync enabled to BPM and phase
    // of the running Link session when play button is pressed,
    // to achieve this we consider the syncable AbletonLink as playing in this case.

    // If Link is disabled or there is no peer connected,
    // and a Mixxx deck is started, the deck will play with original BPM,
    // to achieve this, we consider the syncable AbletonLink as not playing in this case
    return true;
}
bool AbletonLink::isAudible() const {
    return false;
}
bool AbletonLink::isQuantized() const {
    return true;
}

bool AbletonLink::isAvailable() const {
#ifdef __ABLETONLINK__
    return true;
#else
    return false;
#endif
}

bool AbletonLink::isEnabled() const {
    return m_linkEnabled.load(std::memory_order_relaxed);
}

void AbletonLink::setEnabled(bool enabled) {
    const bool effectiveEnabled = enabled && isAvailable();
    m_linkEnabled.store(effectiveEnabled, std::memory_order_relaxed);
    if (!effectiveEnabled) {
        cancelPendingStartStopSync();
        publishSessionState(mixxx::Bpm(kDefaultLinkTempo), 0.0, false);
        m_pNextBeatTime->forceSet(0.0);
        m_pNextBeatEta->forceSet(0.0);
    }
#ifdef __ABLETONLINK__
    m_pLink->enable(effectiveEnabled);
#endif
    if (effectiveEnabled && isStartStopSyncEnabled()) {
        requestStartStopSync(m_pEngineSync->isSynchronizedDeckPlaying());
    }
    m_pLinkButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    m_pEnabled->forceSet(effectiveEnabled ? 1.0 : 0.0);
    updateLinkAudioOutputSinks();
}

bool AbletonLink::isStartStopSyncEnabled() const {
    return m_startStopSyncEnabled.load(std::memory_order_relaxed);
}

void AbletonLink::setStartStopSyncEnabled(bool enabled) {
    const bool effectiveEnabled = enabled && isAvailable();
    m_startStopSyncEnabled.store(effectiveEnabled, std::memory_order_relaxed);
    if (!effectiveEnabled) {
        cancelPendingStartStopSync();
    }
#ifdef __ABLETONLINK__
    m_pLink->enableStartStopSync(effectiveEnabled);
#endif
    m_pStartStopSyncButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    if (effectiveEnabled) {
        requestStartStopSync(m_pEngineSync->isSynchronizedDeckPlaying());
    }
}

bool AbletonLink::isLinkAudioAvailable() const {
#ifdef MIXXX_ABLETON_LINK_AUDIO
    return true;
#else
    return false;
#endif
}

bool AbletonLink::isLinkAudioEnabled() const {
    return m_linkAudioEnabled.load(std::memory_order_relaxed);
}

void AbletonLink::setLinkAudioEnabled(bool enabled) {
    const bool effectiveEnabled = enabled && isLinkAudioAvailable();
    m_linkAudioEnabled.store(effectiveEnabled, std::memory_order_relaxed);
#ifdef MIXXX_ABLETON_LINK_AUDIO
    m_pLink->enableLinkAudio(effectiveEnabled);
#endif
    if (!effectiveEnabled) {
        setLinkAudioReceiveEnabled(false);
    }
    m_pLinkAudioButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    m_pLinkAudioAvailable->forceSet(isLinkAudioAvailable() ? 1.0 : 0.0);
    updateLinkAudioOutputSinks();
    updateLinkAudioChannels();
}

bool AbletonLink::isLinkAudioSourcesEnabled() const {
    return m_linkAudioSourcesEnabled.load(std::memory_order_relaxed);
}

void AbletonLink::setLinkAudioSourcesEnabled(bool enabled) {
    const bool effectiveEnabled = enabled && isLinkAudioAvailable();
    m_linkAudioSourcesEnabled.store(effectiveEnabled, std::memory_order_relaxed);
    m_pLinkAudioSourcesButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    updateLinkAudioOutputSinks();
    updateLinkAudioChannels();
}

bool AbletonLink::isLinkAudioReceiveEnabled() const {
    return m_linkAudioReceiveEnabled.load(std::memory_order_relaxed);
}

void AbletonLink::setLinkAudioReceiveEnabled(bool enabled) {
    const bool effectiveEnabled = enabled && isLinkAudioAvailable() && isLinkAudioEnabled();
    m_linkAudioReceiveEnabled.store(effectiveEnabled, std::memory_order_relaxed);
    m_pLinkAudioReceiveButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    updateLinkAudioChannels();
}

void AbletonLink::requestStartStopSync(bool playing) {
    if (!isEnabled() || !isStartStopSyncEnabled()) {
        return;
    }
    if (!playing) {
        clearQuantizedLaunchTime();
    }
    m_pendingStartStopSyncState.store(
            playing ? kPendingStart : kPendingStop,
            std::memory_order_release);
}

void AbletonLink::requestQuantizedLaunch() {
    if (!isEnabled() || !isStartStopSyncEnabled()) {
        return;
    }
    if (!m_pEngineSync->hasSynchronizedDeck()) {
        return;
    }

#ifdef __ABLETONLINK__
    auto sessionState = m_pLink->captureAppSessionState();
    const auto now = m_pLink->clock().micros();
    const auto quantum = getLaunchQuantum();
    const auto launchTime = timeAtNextQuantum(sessionState, now, quantum);
    const auto launchBeat = sessionState.beatAtTime(launchTime, quantum);

    if (sessionState.isPlaying()) {
        sessionState.requestBeatAtTime(launchBeat, launchTime, quantum);
    } else {
        sessionState.setIsPlayingAndRequestBeatAtTime(
                true,
                launchTime,
                launchBeat,
                quantum);
    }
    m_pLink->commitAppSessionState(sessionState);

    m_quantizedLaunchTime = launchTime;
    m_pQuantizedLaunchTime->forceSet(static_cast<double>(launchTime.count()));
    m_pQuantizedLaunchEta->forceSet(static_cast<double>(
            std::max<int64_t>(0, (launchTime - now).count())));
    const auto nextBeatTime = timeAtNextQuantum(sessionState, now, getQuantum());
    m_pNextBeatTime->forceSet(static_cast<double>(nextBeatTime.count()));
    m_pNextBeatEta->forceSet(static_cast<double>(
            std::max<int64_t>(0, (nextBeatTime - now).count())));
    slotLinkStartStopChanged(
            true,
            launchTime,
            m_startStopSyncGeneration.load(std::memory_order_acquire));
#endif
}

std::size_t AbletonLink::numPeers() const {
    return m_numPeers.load(std::memory_order_relaxed);
}

double AbletonLink::getQuantum() const {
    return kBeatSyncQuantum;
}

double AbletonLink::getLaunchQuantum() const {
    return static_cast<double>(m_launchQuantumBeats.load(std::memory_order_relaxed));
}

mixxx::Bpm AbletonLink::getBpm() const {
    return getBaseBpm();
}

double AbletonLink::getBeatDistance() const {
#ifdef __ABLETONLINK__
    const auto sessionState = captureSessionState();
    return sessionState.phaseAtTime(
            currentCallbackTime(), getQuantum());
#else
    return 0.0;
#endif
}

mixxx::Bpm AbletonLink::getBaseBpm() const {
#ifdef __ABLETONLINK__
    const auto sessionState = captureSessionState();
    return mixxx::Bpm(sessionState.tempo());
#else
    return mixxx::Bpm(kDefaultLinkTempo);
#endif
}

void AbletonLink::updateLeaderBeatDistance(double beatDistance) {
#ifdef __ABLETONLINK__
    auto sessionState = captureSessionState();
    const auto currentTime = currentCallbackTime();
    const auto currentBeat = sessionState.beatAtTime(
            currentTime, getQuantum());
    const auto newBeat =
            currentBeat -
            sessionState.phaseAtTime(currentTime, getQuantum()) +
            beatDistance;

    sessionState.requestBeatAtTime(newBeat, currentTime, getQuantum());
    commitSessionState(sessionState);
#else
    Q_UNUSED(beatDistance);
#endif
}

void AbletonLink::forceUpdateLeaderBeatDistance(double beatDistance) {
#ifdef __ABLETONLINK__
    auto sessionState = captureSessionState();
    const auto currentTime = currentCallbackTime();
    const auto currentBeat = sessionState.beatAtTime(
            currentTime, getQuantum());
    const auto newBeat =
            currentBeat -
            sessionState.phaseAtTime(currentTime, getQuantum()) +
            beatDistance;

    sessionState.forceBeatAtTime(newBeat, currentTime, getQuantum());
    commitSessionState(sessionState);
#else
    Q_UNUSED(beatDistance);
#endif
}

void AbletonLink::updateLeaderBpm(mixxx::Bpm bpm) {
#ifdef __ABLETONLINK__
    if (!bpm.isValid()) {
        return;
    }
    auto sessionState = captureSessionState();
    sessionState.setTempo(bpm.value(), currentCallbackTime());
    commitSessionState(sessionState);
#else
    Q_UNUSED(bpm);
#endif
}

void AbletonLink::notifyLeaderParamSource() {
    // In Ableton Link all peers are equal. Therefore nothing differs,
    // if AbletonLink becomes SyncLeader.
    // Half/double BPM handling is resolved by EngineSync before leader
    // parameters are published to Link.
}

void AbletonLink::reinitLeaderParams(double beatDistance, mixxx::Bpm, mixxx::Bpm bpm) {
    updateLeaderBeatDistance(beatDistance);
    updateLeaderBpm(bpm);
}
void AbletonLink::updateInstantaneousBpm(mixxx::Bpm) {
}

/// This method is called at the start of the audio callback.
/// It captures the current time and updates the audio buffer time.
/// If Ableton Link is enabled, it captures the session state and notifies
/// the engine sync about any changes in tempo and beat distance.
void AbletonLink::onCallbackStart() {
#ifdef __ABLETONLINK__
    const auto outputLatency = VisualPlayPosition::callbackEntryToDac();
    onCallbackStart(m_pLink->clock().micros() + outputLatency, outputLatency);
#endif
}

void AbletonLink::onCallbackStart(
        mixxx::audio::SampleRate sampleRate,
        std::size_t bufferSize) {
    Q_UNUSED(sampleRate)
#ifdef __ABLETONLINK__
    // Mixxx engine buffers are interleaved stereo samples. Link's host-time
    // filter sample time follows audio frame count, as in the Link examples.
    constexpr std::size_t kNumChannels = 2;
    const auto outputLatency = VisualPlayPosition::callbackEntryToDac();
    const auto filteredCallbackTime =
            m_hostTimeFilter.sampleTimeToHostTime(m_audioCallbackSampleTime);
    m_audioCallbackSampleTime += static_cast<double>(bufferSize / kNumChannels);
    onCallbackStart(
            filteredCallbackTime + outputLatency,
            outputLatency,
            true);
#else
    Q_UNUSED(bufferSize)
#endif
}

void AbletonLink::onCallbackStart(
        std::chrono::microseconds absTimeWhenPrevOutputBufferReachesDac,
        std::chrono::microseconds outputLatency,
        bool hostTimeFilterEnabled) {
    m_absTimeWhenPrevOutputBufferReachesDacMicros.store(
            absTimeWhenPrevOutputBufferReachesDac.count(),
            std::memory_order_relaxed);
    m_pOutputLatency->forceSet(static_cast<double>(outputLatency.count()));
    m_pHostTimeFilterEnabled->forceSet(hostTimeFilterEnabled ? 1.0 : 0.0);

    if (!isEnabled()) {
        publishSessionState(mixxx::Bpm(kDefaultLinkTempo), 0.0, false);
        m_pNextBeatTime->forceSet(0.0);
        m_pNextBeatEta->forceSet(0.0);
        return;
    }

#ifdef __ABLETONLINK__
    s_pAudioCallbackLink = this;
    m_audioSessionState = m_pLink->captureAudioSessionState();
    setNumPeers(m_pLink->numPeers());
    const int pendingStartStopSyncState = m_pendingStartStopSyncState.exchange(
            kNoPendingStartStopSyncState,
            std::memory_order_acq_rel);
    if (isStartStopSyncEnabled() &&
            (pendingStartStopSyncState == kPendingStart ||
                    pendingStartStopSyncState == kPendingStop)) {
        m_audioSessionState->setIsPlaying(
                pendingStartStopSyncState == kPendingStart,
                absTimeWhenPrevOutputBufferReachesDac);
        m_pLink->commitAudioSessionState(*m_audioSessionState);
    }

    const mixxx::Bpm tempo(m_audioSessionState->tempo());
    if (m_oldTempo != tempo) {
        m_oldTempo = tempo;
        m_pEngineSync->notifyRateChanged(this, tempo);
    }

    const auto beatDistance = m_audioSessionState->phaseAtTime(
            absTimeWhenPrevOutputBufferReachesDac,
            getQuantum());
    publishSessionState(tempo, beatDistance, m_audioSessionState->isPlaying());
    const auto nextBeatTime = timeAtNextQuantum(
            *m_audioSessionState,
            absTimeWhenPrevOutputBufferReachesDac,
            getQuantum());
    m_pNextBeatTime->forceSet(static_cast<double>(nextBeatTime.count()));
    m_pNextBeatEta->forceSet(static_cast<double>(
            std::max<int64_t>(
                    0,
                    (nextBeatTime - absTimeWhenPrevOutputBufferReachesDac).count())));
    if (m_quantizedLaunchTime.count() > 0) {
        m_pQuantizedLaunchEta->forceSet(static_cast<double>(
                std::max<int64_t>(
                        0,
                        (m_quantizedLaunchTime -
                                absTimeWhenPrevOutputBufferReachesDac)
                                .count())));
    }
    m_pEngineSync->notifyBeatDistanceChanged(this, beatDistance);
#else
    Q_UNUSED(absTimeWhenPrevOutputBufferReachesDac);
#endif
}

void AbletonLink::onCallbackEnd(int sampleRate, size_t bufferSize) {
    Q_UNUSED(sampleRate)
    Q_UNUSED(bufferSize)
#ifdef __ABLETONLINK__
    if (s_pAudioCallbackLink == this) {
        s_pAudioCallbackLink = nullptr;
    }
#endif
}

void AbletonLink::publishLinkAudioMainOutput(
        const CSAMPLE* pBuffer,
        std::size_t bufferSize,
        mixxx::audio::SampleRate sampleRate) {
    publishLinkAudioOutput(
            QStringLiteral("[Main]"),
            pBuffer,
            bufferSize,
            sampleRate);
}

void AbletonLink::registerLinkAudioOutput(const QString& group, const QString& name) {
#if defined(__ABLETONLINK__) && defined(MIXXX_ABLETON_LINK_AUDIO)
    if (group.isEmpty() || name.isEmpty()) {
        return;
    }

    for (auto& output : m_linkAudioOutputs) {
        if (output.group == group) {
            if (output.name != name) {
                output.name = name;
                if (output.pSink) {
                    output.pSink->setName(name.toStdString());
                }
            }
            updateLinkAudioOutputSinks();
            return;
        }
    }

    m_linkAudioOutputs.push_back(LinkAudioOutput{
            group,
            name,
            nullptr});
    updateLinkAudioOutputSinks();
#else
    Q_UNUSED(group)
    Q_UNUSED(name)
#endif
}

void AbletonLink::publishLinkAudioOutput(
        const QString& group,
        const CSAMPLE* pBuffer,
        std::size_t bufferSize,
        mixxx::audio::SampleRate sampleRate) {
#if defined(__ABLETONLINK__) && defined(MIXXX_ABLETON_LINK_AUDIO)
    if (!isEnabled() || !isLinkAudioEnabled() || !pBuffer || !sampleRate.isValid()) {
        return;
    }
    constexpr std::size_t kNumChannels = 2;
    if (group.isEmpty() || bufferSize == 0 || bufferSize % kNumChannels != 0) {
        return;
    }

    ableton::LinkAudioSink* pSink = nullptr;
    for (auto& output : m_linkAudioOutputs) {
        if (output.group == group) {
            pSink = output.pSink.get();
            break;
        }
    }
    if (!pSink) {
        return;
    }

    pSink->requestMaxNumSamples(bufferSize);
    ableton::LinkAudioSink::BufferHandle buffer(*pSink);
    if (!buffer || buffer.maxNumSamples < bufferSize) {
        return;
    }

    static_assert(std::is_same_v<SAMPLE, int16_t>);
    SampleUtil::convertFloat32ToS16(
            reinterpret_cast<SAMPLE*>(buffer.samples),
            pBuffer,
            static_cast<SINT>(bufferSize));

    const auto sessionState = m_audioSessionState
            ? *m_audioSessionState
            : m_pLink->captureAudioSessionState();
    const auto callbackTime = currentCallbackTime();
    const auto beatsAtBufferBegin = sessionState.beatAtTime(
            callbackTime,
            getQuantum());
    buffer.commit(
            sessionState,
            beatsAtBufferBegin,
            getQuantum(),
            bufferSize / kNumChannels,
            kNumChannels,
            sampleRate.value());
#else
    Q_UNUSED(group)
    Q_UNUSED(pBuffer)
    Q_UNUSED(bufferSize)
    Q_UNUSED(sampleRate)
#endif
}

void AbletonLink::mixInboundLinkAudioMainOutput(
        CSAMPLE* pBuffer,
        std::size_t bufferSize,
        mixxx::audio::SampleRate sampleRate) {
#if defined(__ABLETONLINK__) && defined(MIXXX_ABLETON_LINK_AUDIO)
    if (!isEnabled() ||
            !isLinkAudioEnabled() ||
            !isLinkAudioReceiveEnabled() ||
            m_linkAudioReceiveMuted.load(std::memory_order_relaxed) ||
            !pBuffer ||
            bufferSize == 0 ||
            !sampleRate.isValid()) {
        m_pLinkAudioReceiveActive->forceSet(0.0);
        return;
    }

    const auto inputs = m_linkAudioInputs.load(std::memory_order_acquire);
    if (!inputs || inputs->empty()) {
        m_pLinkAudioReceiveActive->forceSet(0.0);
        return;
    }

    const CSAMPLE_GAIN gain = static_cast<CSAMPLE_GAIN>(
            m_linkAudioReceiveGain.load(std::memory_order_relaxed));
    const auto sessionState = m_audioSessionState
            ? *m_audioSessionState
            : m_pLink->captureAudioSessionState();
    const auto callbackTime = currentCallbackTime();
    bool mixed = false;
    for (const auto& input : *inputs) {
        if (input) {
            mixed = input->mixInto(
                            pBuffer,
                            bufferSize,
                            sampleRate,
                            sessionState,
                            callbackTime,
                            getQuantum(),
                            gain) ||
                    mixed;
        }
    }
    m_pLinkAudioReceiveActive->forceSet(mixed ? 1.0 : 0.0);
#else
    Q_UNUSED(pBuffer)
    Q_UNUSED(bufferSize)
    Q_UNUSED(sampleRate)
#endif
}

void AbletonLink::updateLinkAudioOutputSinks() {
#if defined(__ABLETONLINK__) && defined(MIXXX_ABLETON_LINK_AUDIO)
    const bool canPublish = isEnabled() && isLinkAudioEnabled();
    const bool publishSources = isLinkAudioSourcesEnabled();
    for (auto& output : m_linkAudioOutputs) {
        const bool isMainOutput = output.group == QStringLiteral("[Main]");
        const bool shouldPublish = canPublish && (isMainOutput || publishSources);
        if (shouldPublish && !output.pSink) {
            output.pSink = std::make_unique<ableton::LinkAudioSink>(
                    *m_pLink,
                    output.name.toStdString(),
                    kMaxEngineSamples);
        } else if (!shouldPublish && output.pSink) {
            output.pSink.reset();
        } else if (shouldPublish && output.pSink) {
            output.pSink->setName(output.name.toStdString());
        }
    }
#endif
}

void AbletonLink::setNumPeers(std::size_t numPeers) {
    m_numPeers.store(numPeers, std::memory_order_relaxed);
    m_pNumLinkPeers->forceSet(static_cast<double>(numPeers));
}

void AbletonLink::updateLinkAudioChannels() {
#ifdef MIXXX_ABLETON_LINK_AUDIO
    const auto channels = m_pLink->channels();
    m_numLinkAudioChannels.store(channels.size(), std::memory_order_relaxed);
    m_pLinkAudioNumChannels->forceSet(static_cast<double>(channels.size()));

    auto nextInputs = std::make_shared<std::vector<std::shared_ptr<LinkAudioInput>>>();
    if (isLinkAudioReceiveEnabled() && isLinkAudioEnabled()) {
        const auto currentInputs = m_linkAudioInputs.load(std::memory_order_acquire);
        const QString localPeerName = QString::fromStdString(m_linkAudioPeerName);
        for (const auto& channel : channels) {
            if (QString::fromStdString(channel.peerName) == localPeerName) {
                continue;
            }

            std::shared_ptr<LinkAudioInput> input;
            if (currentInputs) {
                const auto it = std::find_if(
                        currentInputs->begin(),
                        currentInputs->end(),
                        [&channel](const auto& existingInput) {
                            return existingInput && existingInput->matches(channel);
                        });
                if (it != currentInputs->end()) {
                    input = *it;
                    input->updateMetadata(channel);
                }
            }

            if (!input) {
                input = std::make_shared<LinkAudioInput>(channel);
                const std::weak_ptr<LinkAudioInput> weakInput(input);
                input->pSource = std::make_unique<ableton::LinkAudioSource>(
                        *m_pLink,
                        channel.id,
                        [weakInput](ableton::LinkAudioSource::BufferHandle bufferHandle) {
                            if (auto pInput = weakInput.lock()) {
                                pInput->onBuffer(bufferHandle);
                            }
                        });
            }
            nextInputs->push_back(std::move(input));
        }
    }
    m_linkAudioInputs.store(nextInputs, std::memory_order_release);
    m_numLinkAudioReceiveChannels.store(nextInputs->size(), std::memory_order_relaxed);
    m_pLinkAudioReceiveNumChannels->forceSet(static_cast<double>(nextInputs->size()));
#else
    m_numLinkAudioChannels.store(0, std::memory_order_relaxed);
    m_pLinkAudioNumChannels->forceSet(0.0);
    m_numLinkAudioReceiveChannels.store(0, std::memory_order_relaxed);
    m_pLinkAudioReceiveNumChannels->forceSet(0.0);
#endif
    m_pLinkAudioAvailable->forceSet(isLinkAudioAvailable() ? 1.0 : 0.0);
}

void AbletonLink::publishSessionState(mixxx::Bpm bpm, double beatDistance, bool playing) {
    if (bpm.isValid()) {
        m_pBpm->forceSet(bpm.value());
    }
    m_pBeatDistance->forceSet(beatDistance);
    m_pQuantum->forceSet(getQuantum());
    m_pPlaying->forceSet(playing ? 1.0 : 0.0);
}

void AbletonLink::publishCallbackTempo(double bpm) {
    const mixxx::Bpm tempo(bpm);
    if (tempo.isValid()) {
        m_pBpm->forceSet(tempo.value());
    }
}

void AbletonLink::applyScheduledStartStopSync() {
    if (m_scheduledStartStopSyncGeneration !=
                    m_startStopSyncGeneration.load(std::memory_order_acquire) ||
            !isEnabled() ||
            !isStartStopSyncEnabled() ||
            m_scheduledStartStopSyncTime != m_lastStartStopSyncChangeTime) {
        return;
    }
    m_pEngineSync->setLinkTransportPlaying(m_scheduledStartStopSyncPlaying);
    if (!m_scheduledStartStopSyncPlaying ||
            m_scheduledStartStopSyncTime == m_quantizedLaunchTime) {
        clearQuantizedLaunchTime();
    }
}

std::chrono::microseconds AbletonLink::currentCallbackTime() const {
    return std::chrono::microseconds(
            m_absTimeWhenPrevOutputBufferReachesDacMicros.load(std::memory_order_relaxed));
}

void AbletonLink::cancelPendingStartStopSync() {
    m_startStopSyncGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_pendingStartStopSyncState.store(
            kNoPendingStartStopSyncState,
            std::memory_order_release);
    m_startStopSyncTimer.stop();
    m_lastStartStopSyncChangeTime = std::chrono::microseconds(0);
    m_scheduledStartStopSyncPlaying = false;
    m_scheduledStartStopSyncTime = std::chrono::microseconds(0);
    m_scheduledStartStopSyncGeneration =
            m_startStopSyncGeneration.load(std::memory_order_acquire);
    clearQuantizedLaunchTime();
}

void AbletonLink::clearQuantizedLaunchTime() {
    m_quantizedLaunchTime = std::chrono::microseconds(0);
    m_pQuantizedLaunchTime->forceSet(0.0);
    m_pQuantizedLaunchEta->forceSet(0.0);
}

#ifdef __ABLETONLINK__
std::chrono::microseconds AbletonLink::timeAtNextQuantum(
        const MixxxAbletonLinkSessionState& sessionState,
        std::chrono::microseconds time,
        double quantum) const {
    const auto currentBeat = sessionState.beatAtTime(time, quantum);
    const auto currentPhase = sessionState.phaseAtTime(time, quantum);
    const auto nextBeat = currentBeat - currentPhase + quantum;
    return sessionState.timeAtBeat(nextBeat, quantum);
}

MixxxAbletonLinkSessionState AbletonLink::captureSessionState() const {
    if (s_pAudioCallbackLink == this && m_audioSessionState) {
        return *m_audioSessionState;
    }
    return m_pLink->captureAppSessionState();
}

void AbletonLink::commitSessionState(MixxxAbletonLinkSessionState sessionState) {
    if (s_pAudioCallbackLink == this && m_audioSessionState) {
        m_audioSessionState = sessionState;
        m_pLink->commitAudioSessionState(sessionState);
        return;
    }
    m_pLink->commitAppSessionState(sessionState);
}
#endif
