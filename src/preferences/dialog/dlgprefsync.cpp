#include "preferences/dialog/dlgprefsync.h"

#include <QComboBox>
#include <QSignalBlocker>
#include <cmath>

#include "moc_dlgprefsync.cpp"

namespace {
constexpr char kAbletonLinkGroup[] = "[AbletonLink]";

QString formatDouble(double value, int precision) {
    return QString::number(value, 'f', precision);
}

QString formatMicrosAsMillis(double value) {
    if (!std::isfinite(value)) {
        return QObject::tr("n/a");
    }
    return QObject::tr("%1 ms").arg(QString::number(value / 1000.0, 'f', 1));
}
} // anonymous namespace

DlgPrefSync::DlgPrefSync(QWidget* pParent)
        : DlgPreferencePage(pParent),
          m_linkEnabled(kAbletonLinkGroup,
                  "sync_enabled",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_effectiveEnabled(kAbletonLinkGroup,
                  "enabled",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_startStopSyncEnabled(kAbletonLinkGroup,
                  "start_stop_sync_enabled",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_linkAudioEnabled(kAbletonLinkGroup,
                  "link_audio_enabled",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_linkAudioAvailable(kAbletonLinkGroup,
                  "link_audio_available",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_linkAudioNumChannels(kAbletonLinkGroup,
                  "link_audio_num_channels",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_launchQuantum(kAbletonLinkGroup,
                  "launch_quantum",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_numPeers(kAbletonLinkGroup,
                  "num_peers",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_bpm(kAbletonLinkGroup,
                  "bpm",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_beatDistance(kAbletonLinkGroup,
                  "beat_distance",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_quantum(kAbletonLinkGroup,
                  "quantum",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_playing(kAbletonLinkGroup,
                  "playing",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_outputLatency(kAbletonLinkGroup,
                  "output_latency_micros",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_hostTimeFilterEnabled(kAbletonLinkGroup,
                  "host_time_filter_enabled",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_nextBeatEta(kAbletonLinkGroup,
                  "next_beat_eta_micros",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_quantizedLaunchEta(kAbletonLinkGroup,
                  "quantized_launch_eta_micros",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_pendingLinkEnabled(false),
          m_pendingStartStopSyncEnabled(false),
          m_pendingLinkAudioEnabled(false),
          m_pendingLaunchQuantum(1.0) {
    setupUi(this);

    ComboBoxLaunchQuantum->addItem(tr("1 beat"), 1.0);
    ComboBoxLaunchQuantum->addItem(tr("2 beats"), 2.0);
    ComboBoxLaunchQuantum->addItem(tr("4 beats"), 4.0);
    ComboBoxLaunchQuantum->addItem(tr("8 beats"), 8.0);

    connect(CheckBoxEnableLink,
            &QCheckBox::toggled,
            this,
            &DlgPrefSync::slotSetLinkEnabled);
    connect(CheckBoxStartStopSync,
            &QCheckBox::toggled,
            this,
            &DlgPrefSync::slotSetStartStopSyncEnabled);
    connect(CheckBoxLinkAudio,
            &QCheckBox::toggled,
            this,
            &DlgPrefSync::slotSetLinkAudioEnabled);
    connect(ComboBoxLaunchQuantum,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefSync::slotSetLaunchQuantum);

    m_effectiveEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_startStopSyncEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_linkAudioEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_linkAudioAvailable.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_linkAudioNumChannels.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_launchQuantum.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_numPeers.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_bpm.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_beatDistance.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_quantum.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_playing.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_outputLatency.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_hostTimeFilterEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_nextBeatEta.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_quantizedLaunchEta.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);

    setScrollSafeGuardForAllInputWidgets(this);
    slotUpdate();
}

void DlgPrefSync::slotUpdate() {
    setPendingState(m_linkEnabled.valid() && m_linkEnabled.toBool(),
            m_startStopSyncEnabled.valid() && m_startStopSyncEnabled.toBool(),
            m_linkAudioEnabled.valid() && m_linkAudioEnabled.toBool(),
            m_launchQuantum.valid() ? m_launchQuantum.get() : 1.0);
    updateStatusLabels();
}

void DlgPrefSync::slotApply() {
    if (!controlsAvailable()) {
        return;
    }

    // These proxies write to the engine-owned AbletonLink controls. The engine
    // confirms or rejects the request, then publishes the effective state on
    // [AbletonLink],enabled and [AbletonLink],start_stop_sync_enabled.
    m_linkEnabled.set(m_pendingLinkEnabled ? 1.0 : 0.0);
    m_startStopSyncEnabled.set(m_pendingStartStopSyncEnabled ? 1.0 : 0.0);
    m_linkAudioEnabled.set(m_pendingLinkAudioEnabled ? 1.0 : 0.0);
    m_launchQuantum.set(m_pendingLaunchQuantum);
}

void DlgPrefSync::slotResetToDefaults() {
    // Link should be opt-in. Start/Stop Sync is deliberately off by default for
    // DJ workflows where transport control is usually local to Mixxx.
    setPendingState(false, false, false, 1.0);
    slotApply();
    updateStatusLabels();
}

void DlgPrefSync::slotSetLinkEnabled(bool enabled) {
    m_pendingLinkEnabled = enabled;
}

void DlgPrefSync::slotSetStartStopSyncEnabled(bool enabled) {
    m_pendingStartStopSyncEnabled = enabled;
}

void DlgPrefSync::slotSetLinkAudioEnabled(bool enabled) {
    m_pendingLinkAudioEnabled = enabled;
}

void DlgPrefSync::slotSetLaunchQuantum(int index) {
    if (index < 0) {
        return;
    }
    m_pendingLaunchQuantum = ComboBoxLaunchQuantum->itemData(index).toDouble();
}

void DlgPrefSync::slotLinkStatusChanged(double value) {
    Q_UNUSED(value);
    updateStatusLabels();
}

bool DlgPrefSync::controlsAvailable() const {
#ifdef __ABLETONLINK__
    return m_linkEnabled.valid() && m_startStopSyncEnabled.valid() && m_launchQuantum.valid();
#else
    return false;
#endif
}

void DlgPrefSync::setPendingState(
        bool linkEnabled,
        bool startStopSyncEnabled,
        bool linkAudioEnabled,
        double launchQuantum) {
    m_pendingLinkEnabled = linkEnabled;
    m_pendingStartStopSyncEnabled = startStopSyncEnabled;
    m_pendingLinkAudioEnabled = linkAudioEnabled;
    m_pendingLaunchQuantum = launchQuantum;

    const QSignalBlocker linkBlocker(CheckBoxEnableLink);
    const QSignalBlocker startStopBlocker(CheckBoxStartStopSync);
    const QSignalBlocker linkAudioBlocker(CheckBoxLinkAudio);
    CheckBoxEnableLink->setChecked(m_pendingLinkEnabled);
    CheckBoxStartStopSync->setChecked(m_pendingStartStopSyncEnabled);
    CheckBoxLinkAudio->setChecked(m_pendingLinkAudioEnabled);
    selectLaunchQuantum(m_pendingLaunchQuantum);

    const bool available = controlsAvailable();
    const bool linkAudioAvailable = available && m_linkAudioAvailable.valid() &&
            m_linkAudioAvailable.toBool();
    CheckBoxEnableLink->setEnabled(available);
    CheckBoxStartStopSync->setEnabled(available);
    CheckBoxLinkAudio->setEnabled(linkAudioAvailable);
    ComboBoxLaunchQuantum->setEnabled(available);
}

void DlgPrefSync::selectLaunchQuantum(double launchQuantum) {
    const QSignalBlocker launchQuantumBlocker(ComboBoxLaunchQuantum);
    int index = ComboBoxLaunchQuantum->findData(launchQuantum);
    if (index < 0) {
        index = ComboBoxLaunchQuantum->findData(1.0);
    }
    ComboBoxLaunchQuantum->setCurrentIndex(index);
    if (index >= 0) {
        m_pendingLaunchQuantum = ComboBoxLaunchQuantum->itemData(index).toDouble();
    }
}

void DlgPrefSync::updateStatusLabels() {
    const bool available = controlsAvailable();
    LabelBuildStatusValue->setText(available ? tr("Available") : tr("Unavailable"));
    LabelEffectiveStateValue->setText(
            available ? enabledText(m_effectiveEnabled.toBool()) : tr("Disabled"));
    LabelPeersValue->setText(
            m_numPeers.valid() ? QString::number(static_cast<int>(m_numPeers.get())) : tr("n/a"));
    LabelBpmValue->setText(m_bpm.valid() ? formatDouble(m_bpm.get(), 2) : tr("n/a"));
    LabelBeatPhaseValue->setText(
            m_beatDistance.valid() ? formatDouble(m_beatDistance.get(), 3) : tr("n/a"));
    LabelQuantumValue->setText(
            m_quantum.valid() ? formatDouble(m_quantum.get(), 1) : tr("n/a"));
    LabelLaunchQuantumValue->setText(
            m_launchQuantum.valid() ? formatDouble(m_launchQuantum.get(), 0) : tr("n/a"));
    LabelPlayingValue->setText(
            m_playing.valid() ? playingText(m_playing.toBool()) : tr("n/a"));
    LabelLinkAudioAvailableValue->setText(
            m_linkAudioAvailable.valid()
                    ? enabledText(m_linkAudioAvailable.toBool())
                    : tr("n/a"));
    LabelLinkAudioChannelsValue->setText(
            m_linkAudioNumChannels.valid()
                    ? QString::number(static_cast<int>(m_linkAudioNumChannels.get()))
                    : tr("n/a"));
    LabelOutputLatencyValue->setText(
            m_outputLatency.valid() ? formatMicrosAsMillis(m_outputLatency.get()) : tr("n/a"));
    LabelHostTimeFilterValue->setText(
            m_hostTimeFilterEnabled.valid()
                    ? enabledText(m_hostTimeFilterEnabled.toBool())
                    : tr("n/a"));
    LabelNextBeatEtaValue->setText(
            m_nextBeatEta.valid() ? formatMicrosAsMillis(m_nextBeatEta.get()) : tr("n/a"));
    LabelQueuedLaunchEtaValue->setText(
            m_quantizedLaunchEta.valid() ? formatMicrosAsMillis(m_quantizedLaunchEta.get()) : tr("n/a"));
}

QString DlgPrefSync::enabledText(bool enabled) const {
    return enabled ? tr("Enabled") : tr("Disabled");
}

QString DlgPrefSync::playingText(bool playing) const {
    return playing ? tr("Playing") : tr("Stopped");
}
