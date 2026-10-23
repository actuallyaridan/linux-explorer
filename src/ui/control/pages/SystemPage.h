#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "ui/control/PageId.h"

class QScrollArea;
class QGridLayout;
class QLabel;
class QEvent;

namespace ControlPanel {

// The "System" detail page, a Linux-flavoured take on the Windows 7
// "View basic information about your computer" screen. All values are gathered
// live from the running machine (/etc/os-release, /proc, QSysInfo)
class SystemPage : public QWidget {
    Q_OBJECT

public:
    explicit SystemPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

signals:
    // Emitted when the "Rating" row is clicked. MainWindow routes this to the
    // Performance Information and Tools (Linux Experience Index) page
    void performanceRequested();

    // Emitted after the "Linver configuration" dialog (opened from the
    // distributor logo) is accepted. MainWindow re-navigates to this page so the
    // new branding takes effect
    void brandingChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Live system facts, collected once in the constructor
    struct SysInfo {
        // Distro pretty name, e.g. "CachyOS"
        QString edition;
        // Distro NAME (no version), e.g. "CachyOS"
        QString editionShort;
        // Copyright holder / developer org, e.g. "Fedora Project"
        QString vendor;
        // Kernel name + release, e.g. "Linux 7.0.11"
        QString kernel;
        // Theme icon name for the distro logo
        QString logoIcon;
        // CPU model name
        QString processor;
        // Base/max clock, e.g. "3.60 GHz"
        QString clock;
        // Installed RAM, e.g. "16.0 GB"
        QString memory;
        // E.g. "64-bit Operating System"
        QString systemType;
        // Machine host name
        QString hostName;
        // Synthesised machine id
        QString productId;
    };

    static SysInfo gatherInfo();

    // Adds a "Label:   value" row to a section grid at the given row index.
    // Returns the value label so callers can wire interaction (e.g. the rating
    // link)
    QLabel *addInfoRow(QGridLayout *grid, int row,
                       const QString &label, const QString &value,
                       const QString &trailing = QString(),
                       bool valueIsLink = false);

    QLabel *m_ratingLabel = nullptr;
    // Clickable distributor logo -> config dialog
    QLabel *m_logoLabel = nullptr;
};

} // namespace ControlPanel
