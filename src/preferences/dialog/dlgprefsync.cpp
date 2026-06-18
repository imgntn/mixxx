#include "preferences/dialog/dlgprefsync.h"

#include <QSignalBlocker>

#include "moc_dlgprefsync.cpp"

namespace {
constexpr char kAbletonLinkGroup[] = "[AbletonLink]";

QString formatDouble(double value, int precision) {
    return QString::number(value, 'f', precision);
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
          m_pendingLinkEnabled(false),
          m_pendingStartStopSyncEnabled(false) {
    setupUi(this);

    connect(CheckBoxEnableLink,
            &QCheckBox::toggled,
            this,
            &DlgPrefSync::slotSetLinkEnabled);
    connect(CheckBoxStartStopSync,
            &QCheckBox::toggled,
            this,
            &DlgPrefSync::slotSetStartStopSyncEnabled);

    m_effectiveEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_startStopSyncEnabled.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_numPeers.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_bpm.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_beatDistance.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_quantum.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);
    m_playing.connectValueChanged(this, &DlgPrefSync::slotLinkStatusChanged);

    setScrollSafeGuardForAllInputWidgets(this);
    slotUpdate();
}

void DlgPrefSync::slotUpdate() {
    setPendingState(m_linkEnabled.valid() && m_linkEnabled.toBool(),
            m_startStopSyncEnabled.valid() && m_startStopSyncEnabled.toBool());
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
}

void DlgPrefSync::slotResetToDefaults() {
    // Link should be opt-in. Start/Stop Sync is deliberately off by default for
    // DJ workflows where transport control is usually local to Mixxx.
    setPendingState(false, false);
    slotApply();
    updateStatusLabels();
}

void DlgPrefSync::slotSetLinkEnabled(bool enabled) {
    m_pendingLinkEnabled = enabled;
}

void DlgPrefSync::slotSetStartStopSyncEnabled(bool enabled) {
    m_pendingStartStopSyncEnabled = enabled;
}

void DlgPrefSync::slotLinkStatusChanged(double value) {
    Q_UNUSED(value);
    updateStatusLabels();
}

bool DlgPrefSync::controlsAvailable() const {
#ifdef __ABLETONLINK__
    return m_linkEnabled.valid() && m_startStopSyncEnabled.valid();
#else
    return false;
#endif
}

void DlgPrefSync::setPendingState(bool linkEnabled, bool startStopSyncEnabled) {
    m_pendingLinkEnabled = linkEnabled;
    m_pendingStartStopSyncEnabled = startStopSyncEnabled;

    const QSignalBlocker linkBlocker(CheckBoxEnableLink);
    const QSignalBlocker startStopBlocker(CheckBoxStartStopSync);
    CheckBoxEnableLink->setChecked(m_pendingLinkEnabled);
    CheckBoxStartStopSync->setChecked(m_pendingStartStopSyncEnabled);

    const bool available = controlsAvailable();
    CheckBoxEnableLink->setEnabled(available);
    CheckBoxStartStopSync->setEnabled(available);
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
    LabelPlayingValue->setText(
            m_playing.valid() ? playingText(m_playing.toBool()) : tr("n/a"));
}

QString DlgPrefSync::enabledText(bool enabled) const {
    return enabled ? tr("Enabled") : tr("Disabled");
}

QString DlgPrefSync::playingText(bool playing) const {
    return playing ? tr("Playing") : tr("Stopped");
}
