#pragma once

#include <QWidget>
#include <QStringList>
#include "ui/control/PageId.h"
#include "model/control/WeiBenchmark.h"

class QScrollArea;
class QLabel;
class QPushButton;
class QDialog;
class QProgressBar;
class QFrame;

namespace ControlPanel {

// "Performance Information and Tools", the Linux Experience Index screen, a
// faithful take on the Windows 7 WEI page. Shows the five component subscores,
// the base score (the minimum subscore) in a glossy badge, and a "Rate this
// computer" action that runs the live benchmark engine behind a modal progress
// dialog. A details view exposes the raw measured metrics for calibration
class PerformancePage : public QWidget {
    Q_OBJECT
public:
    explicit PerformancePage(QScrollArea *sidebar, QWidget *parent = nullptr);

    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void populate(const WeiResult &r);
    void applyState(bool rated);
    void onRateClicked();

    WeiBenchmark    *m_bench = nullptr;
    WeiResult        m_result;

    // Yellow "not yet established" bar
    QFrame          *m_banner      = nullptr;
    // Lives in the banner (unrated)
    QPushButton     *m_rateBtn     = nullptr;

    // "Subscore"  column header
    QLabel          *m_subHeader   = nullptr;
    // "Base score" column header
    QLabel          *m_baseHeader  = nullptr;
    // Shaded panel behind score cols
    QFrame          *m_shade       = nullptr;
    // Holds the glossy badge + caption
    QFrame          *m_basePanel   = nullptr;
    // Glossy blue base-score badge
    QFrame          *m_badge       = nullptr;
    QLabel          *m_baseCaption = nullptr;
    // Processor, Memory, Graphics, Gaming, Disk
    QLabel          *m_subscore[5] = {};
    QLabel          *m_baseBig     = nullptr;

    // "View and print detailed…"
    QLabel          *m_detailToggle= nullptr;
    QWidget         *m_detailWrap  = nullptr;
    QLabel          *m_detailLabel = nullptr;

    // "Your scores are current / Last update…"
    QLabel          *m_scoresCurrent = nullptr;
    // "Re-run the assessment" (rated)
    QLabel          *m_rerunLink   = nullptr;

    // "Assessing…" progress window
    QDialog         *m_dlg      = nullptr;
    // Bold phase title in the header band
    QLabel          *m_dlgTitle = nullptr;
    QProgressBar    *m_dlgBar   = nullptr;
    bool             m_canceled = false;
};

} // namespace ControlPanel
