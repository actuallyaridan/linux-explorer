#pragma once

#include "aero/taskdialog.h"
#include <QString>

class KJob;
class QLabel;
class QPushButton;
namespace Aero { class ChevronButton; class LinkLabel; }
class QProgressBar;

// Win7's file operation dialog, with a details panel that folds out
//
// Modeless, and the job is watched rather than driven, every number arriving
// through the job's own signals and Cancel only asking it to stop
class FileProgressDialog : public Aero::TaskDialog {
    Q_OBJECT

public:
    // Passed in rather than read from the job's description, which is reemitted
    // per file and would flicker through every filename
    FileProgressDialog(KJob *job, const QString &source, const QString &destination,
                       QWidget *parent = nullptr);

private:
    QWidget *buildDetails();
    void setExpanded(bool expanded);
    void refreshHeading();
    void refreshDetails();

    // Worded as Win7 words it, and empty when there is nothing to estimate from
    QString remainingText() const;

    KJob *m_job = nullptr;

    QLabel *m_heading = nullptr;
    QLabel *m_summary = nullptr;
    QProgressBar *m_bar = nullptr;

    QWidget *m_details = nullptr;
    QLabel  *m_detailName = nullptr;
    QLabel  *m_detailFrom = nullptr;
    QLabel  *m_detailTo = nullptr;
    QLabel  *m_detailRemainingTime = nullptr;
    QLabel  *m_detailRemainingItems = nullptr;
    QLabel  *m_detailSpeed = nullptr;

    Aero::LinkLabel *m_expander = nullptr;
    Aero::ChevronButton *m_chevron = nullptr;

    // The job reports each piece through its own signal, so the labels are
    // rebuilt from this rather than from whichever fired last
    QString m_action;
    QString m_source;
    QString m_destination;
    QString m_currentItem;

    qulonglong m_totalBytes = 0;
    qulonglong m_processedBytes = 0;
    qulonglong m_totalItems = 0;
    qulonglong m_processedItems = 0;
    unsigned long m_speed = 0;
};
