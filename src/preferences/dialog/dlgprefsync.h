#pragma once

#include "control/controlproxy.h"
#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefsyncdlg.h"

class QWidget;

class DlgPrefSync : public DlgPreferencePage, public Ui::DlgPrefSyncDlg {
    Q_OBJECT
  public:
    explicit DlgPrefSync(QWidget* pParent);

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private slots:
    void slotSetLinkEnabled(bool enabled);
    void slotSetStartStopSyncEnabled(bool enabled);
    void slotSetLinkAudioEnabled(bool enabled);
    void slotSetLinkAudioReceiveEnabled(bool enabled);
    void slotSetLinkAudioReceiveMuted(bool muted);
    void slotSetLinkAudioReceiveGain(double gain);
    void slotSetLaunchQuantum(int index);
    void slotLinkStatusChanged(double value);

  private:
    bool controlsAvailable() const;
    void setPendingState(
            bool linkEnabled,
            bool startStopSyncEnabled,
            bool linkAudioEnabled,
            bool linkAudioReceiveEnabled,
            bool linkAudioReceiveMuted,
            double linkAudioReceiveGain,
            double launchQuantum);
    void selectLaunchQuantum(double launchQuantum);
    void updateStatusLabels();
    QString enabledText(bool enabled) const;
    QString playingText(bool playing) const;

    ControlProxy m_linkEnabled;
    ControlProxy m_effectiveEnabled;
    ControlProxy m_startStopSyncEnabled;
    ControlProxy m_linkAudioEnabled;
    ControlProxy m_linkAudioReceiveEnabled;
    ControlProxy m_linkAudioReceiveMuted;
    ControlProxy m_linkAudioReceiveGain;
    ControlProxy m_linkAudioAvailable;
    ControlProxy m_linkAudioNumChannels;
    ControlProxy m_linkAudioReceiveNumChannels;
    ControlProxy m_linkAudioReceiveActive;
    ControlProxy m_launchQuantum;
    ControlProxy m_numPeers;
    ControlProxy m_bpm;
    ControlProxy m_beatDistance;
    ControlProxy m_quantum;
    ControlProxy m_playing;
    ControlProxy m_outputLatency;
    ControlProxy m_hostTimeFilterEnabled;
    ControlProxy m_nextBeatEta;
    ControlProxy m_quantizedLaunchEta;

    bool m_pendingLinkEnabled;
    bool m_pendingStartStopSyncEnabled;
    bool m_pendingLinkAudioEnabled;
    bool m_pendingLinkAudioReceiveEnabled;
    bool m_pendingLinkAudioReceiveMuted;
    double m_pendingLinkAudioReceiveGain;
    double m_pendingLaunchQuantum;
};
