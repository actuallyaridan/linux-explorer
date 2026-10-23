#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

#include "ui/control/LevelMeter.h"

class QTabWidget;
class QVBoxLayout;
class QScrollArea;
class QPushButton;
class QToolButton;
class QProcess;

namespace ControlPanel {

// A clone of the Windows 7 "Sound" dialog (Playback / Recording / Sounds /
// Communications tabs), backed directly by the system, no KDE modules
//
//   * Playback and Recording list the real PulseAudio/PipeWire sinks and
//     sources (`pactl list …`), mark the default, and "Set Default" writes
//     `pactl set-default-sink/-source`, what the volume applets do, no
//     privileges needed.
//   * Communications (call-ducking) and Sounds (a Windows event-sound scheme)
//     have no direct Linux equivalent, so those tabs mirror the UI and persist
//     their choices locally
class SoundDialog : public QDialog {
    Q_OBJECT

public:
    explicit SoundDialog(QWidget *parent = nullptr);
    ~SoundDialog() override;

    enum Tab { TabPlayback = 0, TabRecording = 1,
               TabSounds = 2, TabCommunications = 3 };
    void showTab(Tab tab);

protected:
    // Row clicks select a device in the Playback/Recording lists
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Device {
        // Pactl node name (the id used to set the default)
        QString name;
        // E.g. "Speakers"
        QString description;
        // E.g. "High Definition Audio Device"
        QString cardName;
        int     volumePercent = 0;
        bool    muted = false;
    };

    // Everything backing one device list (Playback = sinks, Recording = sources)
    struct DeviceList {
        bool                  sinks = true;
        // Holds the row widgets
        QVBoxLayout          *box = nullptr;
        QList<Device>         devices;
        QString               defaultName;
        int                   selected = -1;
        QList<QWidget *>      rows;
        QHash<QObject *, int> rowToIndex;
        QToolButton          *setDefaultBtn = nullptr;
        QPushButton          *configureBtn = nullptr;
        QPushButton          *propertiesBtn = nullptr;
        // The default device's meter
        LevelMeter           *activeMeter = nullptr;
        // Live parec capture
        QProcess             *monitorProc = nullptr;
    };

    QList<Device> gatherDevices(bool sinks) const;
    QString       defaultDevice(bool sinks) const;

    QWidget *buildDeviceTab(DeviceList &dl, bool sinks, const QString &prompt);
    QWidget *buildSoundsTab();
    QWidget *buildCommunicationsTab();

    void rebuildRows(DeviceList &dl);
    void selectRow(DeviceList &dl, int index);
    void applyDefault(DeviceList &dl);

    // Live level metering of the default device via parec on its monitor/source
    void startMonitor(DeviceList &dl);
    void stopMonitor(DeviceList &dl);
    // Start/stop to match the visible tab
    void updateMonitors();

    QTabWidget *m_tabs = nullptr;
    DeviceList  m_playback;
    DeviceList  m_recording;
};

} // namespace ControlPanel
