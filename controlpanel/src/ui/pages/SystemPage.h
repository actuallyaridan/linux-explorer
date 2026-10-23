#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"

class QScrollArea;
class QGridLayout;
class QLabel;
class QEvent;

// The "System" detail page, a Linux-flavoured take on the Windows 7
// "View basic information about your computer" screen. All values are gathered
// live from the running machine (/etc/os-release, /proc, QSysInfo).
class SystemPage : public QWidget {
    Q_OBJECT

public:
    explicit SystemPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar.
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

signals:
    // Emitted when the "Rating" row is clicked. MainWindow routes this to the
    // Performance Information and Tools (Linux Experience Index) page.
    void performanceRequested();

    // Emitted after the "Linver configuration" dialog (opened from the
    // distributor logo) is accepted. MainWindow re-navigates to this page so the
    // new branding takes effect.
    void brandingChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Live system facts, collected once in the constructor.
    struct SysInfo {
        QString edition;        // distro pretty name, e.g. "CachyOS"
        QString editionShort;   // distro NAME (no version), e.g. "CachyOS"
        QString vendor;         // copyright holder / developer org, e.g. "Fedora Project"
        QString kernel;         // kernel name + release, e.g. "Linux 7.0.11"
        QString logoIcon;       // theme icon name for the distro logo
        QString processor;      // CPU model name
        QString clock;          // base/max clock, e.g. "3.60 GHz"
        QString memory;         // installed RAM, e.g. "16.0 GB"
        QString systemType;     // e.g. "64-bit Operating System"
        QString hostName;       // machine host name
        QString productId;      // synthesised machine id
    };

    static SysInfo gatherInfo();

    // Adds a "Label:   value" row to a section grid at the given row index.
    // Returns the value label so callers can wire interaction (e.g. the rating
    // link).
    QLabel *addInfoRow(QGridLayout *grid, int row,
                       const QString &label, const QString &value,
                       const QString &trailing = QString(),
                       bool valueIsLink = false);

    QLabel *m_ratingLabel = nullptr;
    QLabel *m_logoLabel = nullptr;   // clickable distributor logo -> config dialog
};
