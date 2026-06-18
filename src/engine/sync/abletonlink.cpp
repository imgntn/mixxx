#include "engine/sync/abletonlink.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

#include "control/controlobject.h"
#include "engine/sync/enginesync.h"
#include "moc_abletonlink.cpp"
#include "preferences/usersettings.h"

namespace {
constexpr double kDefaultLinkTempo = 120.0;
constexpr int kNoPendingStartStopSyncState = -1;
constexpr int kPendingStop = 0;
constexpr int kPendingStart = 1;
#ifdef __ABLETONLINK__
thread_local const AbletonLink* s_pAudioCallbackLink = nullptr;
#endif
} // anonymous namespace

AbletonLink::AbletonLink(const QString& group, EngineSync* pEngineSync)
        : m_group(group),
          m_pEngineSync(pEngineSync),
          m_syncMode(SyncMode::None),
          m_linkEnabled(false),
          m_startStopSyncEnabled(false),
          m_pendingStartStopSyncState(kNoPendingStartStopSyncState),
          m_numPeers(0),
          m_startStopSyncGeneration(0),
          m_oldTempo(),
          m_absTimeWhenPrevOutputBufferReachesDacMicros(0),
          m_lastStartStopSyncChangeTime(0),
          m_quantizedLaunchTime(0),
#ifdef __ABLETONLINK__
          m_pLink(std::make_unique<MixxxAbletonLink>(kDefaultLinkTempo)),
#endif
          m_pLinkButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "sync_enabled"),
                  true)),
          m_pStartStopSyncButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "start_stop_sync_enabled"),
                  true)),
          m_pQuantizedLaunchButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, "quantized_launch"))),
          m_pEnabled(std::make_unique<ControlObject>(ConfigKey(group, "enabled"))),
          m_pNumLinkPeers(std::make_unique<ControlObject>(ConfigKey(group, "num_peers"))),
          m_pBpm(std::make_unique<ControlObject>(ConfigKey(group, "bpm"))),
          m_pBeatDistance(std::make_unique<ControlObject>(ConfigKey(group, "beat_distance"))),
          m_pQuantum(std::make_unique<ControlObject>(ConfigKey(group, "quantum"))),
          m_pPlaying(std::make_unique<ControlObject>(ConfigKey(group, "playing"))),
          m_pNextBeatTime(std::make_unique<ControlObject>(ConfigKey(group, "next_beat_time_micros"))),
          m_pQuantizedLaunchTime(
                  std::make_unique<ControlObject>(ConfigKey(group, "quantized_launch_time_micros"))) {
    m_pLinkButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLinkButton->setStates(2);
    m_pStartStopSyncButton->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pStartStopSyncButton->setStates(2);
    m_pQuantizedLaunchButton->setButtonMode(mixxx::control::ButtonMode::Trigger);

    connect(m_pLinkButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlSyncEnabled);
    connect(m_pStartStopSyncButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlStartStopSyncEnabled);
    connect(m_pQuantizedLaunchButton.get(),
            &ControlObject::valueChanged,
            this,
            &AbletonLink::slotControlQuantizedLaunch);

    m_pEnabled->setReadOnly();
    m_pNumLinkPeers->setReadOnly();
    m_pBpm->setReadOnly();
    m_pBeatDistance->setReadOnly();
    m_pQuantum->setReadOnly();
    m_pPlaying->setReadOnly();
    m_pNextBeatTime->setReadOnly();
    m_pQuantizedLaunchTime->setReadOnly();

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
    m_pQuantum->forceSet(getQuantum());
    setStartStopSyncEnabled(m_pStartStopSyncButton->get() > 0);
    setEnabled(m_pLinkButton->get() > 0);
    publishSessionState(mixxx::Bpm(kDefaultLinkTempo), 0.0, false);
    m_pNextBeatTime->forceSet(0.0);
    clearQuantizedLaunchTime();
}

AbletonLink::~AbletonLink() {
#ifdef __ABLETONLINK__
    // Stop Link activity and remove callbacks before destroying ControlObjects.
    m_pLink->setNumPeersCallback([](std::size_t) {});
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

void AbletonLink::slotControlQuantizedLaunch(double value) {
    if (value > 0) {
        requestQuantizedLaunch();
    }
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
    QTimer::singleShot(
            static_cast<int>(boundedDelay),
            this,
            [this, playing, timeForIsPlaying, generation]() {
                if (generation !=
                                m_startStopSyncGeneration.load(std::memory_order_acquire) ||
                        !isEnabled() ||
                        !isStartStopSyncEnabled() ||
                        timeForIsPlaying != m_lastStartStopSyncChangeTime) {
                    return;
                }
                m_pEngineSync->setLinkTransportPlaying(playing);
                if (!playing || timeForIsPlaying == m_quantizedLaunchTime) {
                    clearQuantizedLaunchTime();
                }
            });
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
        m_pNextBeatTime->forceSet(0.0);
    }
#ifdef __ABLETONLINK__
    m_pLink->enable(effectiveEnabled);
#endif
    if (effectiveEnabled && isStartStopSyncEnabled()) {
        requestStartStopSync(m_pEngineSync->isSynchronizedDeckPlaying());
    }
    m_pLinkButton->forceSet(effectiveEnabled ? 1.0 : 0.0);
    m_pEnabled->forceSet(effectiveEnabled ? 1.0 : 0.0);
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

#ifdef __ABLETONLINK__
    auto sessionState = m_pLink->captureAppSessionState();
    const auto now = m_pLink->clock().micros();
    const auto launchTime = timeAtNextBeat(sessionState, now);
    const auto quantum = getQuantum();
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
    m_pNextBeatTime->forceSet(static_cast<double>(launchTime.count()));
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
    // Mixxx doesn't know about bars/time-signatures yet. A one-beat quantum
    // exposes useful tempo/beat phase without pretending to support bar phase.
    return 1.0;
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
    // TODO: Check the special case of half/double BPM sync.
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
    onCallbackStart(m_pLink->clock().micros());
#endif
}

void AbletonLink::onCallbackStart(std::chrono::microseconds absTimeWhenPrevOutputBufferReachesDac) {
    m_absTimeWhenPrevOutputBufferReachesDacMicros.store(
            absTimeWhenPrevOutputBufferReachesDac.count(),
            std::memory_order_relaxed);

    if (!isEnabled()) {
        publishSessionState(mixxx::Bpm(kDefaultLinkTempo), 0.0, false);
        m_pNextBeatTime->forceSet(0.0);
        return;
    }

#ifdef __ABLETONLINK__
    s_pAudioCallbackLink = this;
    m_audioSessionState = m_pLink->captureAudioSessionState();
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
    m_pNextBeatTime->forceSet(static_cast<double>(
            timeAtNextBeat(*m_audioSessionState, absTimeWhenPrevOutputBufferReachesDac).count()));
    m_pEngineSync->notifyBeatDistanceChanged(this, beatDistance);
#else
    Q_UNUSED(absTimeWhenPrevOutputBufferReachesDac);
#endif
}

void AbletonLink::onCallbackEnd(int sampleRate, size_t bufferSize) {
    Q_UNUSED(sampleRate)
    Q_UNUSED(bufferSize)
#ifdef __ABLETONLINK__
    m_audioSessionState.reset();
    if (s_pAudioCallbackLink == this) {
        s_pAudioCallbackLink = nullptr;
    }
#endif
}

void AbletonLink::setNumPeers(std::size_t numPeers) {
    m_numPeers.store(numPeers, std::memory_order_relaxed);
    m_pNumLinkPeers->forceSet(static_cast<double>(numPeers));
}

void AbletonLink::publishSessionState(mixxx::Bpm bpm, double beatDistance, bool playing) {
    if (bpm.isValid()) {
        m_pBpm->forceSet(bpm.value());
    }
    m_pBeatDistance->forceSet(beatDistance);
    m_pQuantum->forceSet(getQuantum());
    m_pPlaying->forceSet(playing ? 1.0 : 0.0);
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
    m_lastStartStopSyncChangeTime = std::chrono::microseconds(0);
    clearQuantizedLaunchTime();
}

void AbletonLink::clearQuantizedLaunchTime() {
    m_quantizedLaunchTime = std::chrono::microseconds(0);
    m_pQuantizedLaunchTime->forceSet(0.0);
}

#ifdef __ABLETONLINK__
std::chrono::microseconds AbletonLink::timeAtNextBeat(
        const MixxxAbletonLinkSessionState& sessionState,
        std::chrono::microseconds time) const {
    const auto quantum = getQuantum();
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
