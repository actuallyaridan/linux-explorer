#pragma once

#include <QDialog>
#include <QString>

class KJob;
class QLabel;
class QPushButton;
namespace Win7 { class ChevronButton; }
class QProgressBar;

// Windows 7's file operation dialog: a heading, a green progress bar, and a
// "More details" panel that folds out to show speed, time remaining and the
// item being worked on. Modeless, as Win7's is: a copy can run for minutes and
// the user carries on browsing meanwhile.
//
// The job is watched, never driven. Every number arrives through KJob's
// signals, and Cancel asks the job to stop rather than tearing anything down,
// so the operation stays in KIO's hands throughout.
class FileProgressDialog : public QDialog {
    Q_OBJECT

public:
    // Passed in rather than read from the job's description signal, which KIO
    // re-emits per file: the header would flicker through 25,000 filenames
    // instead of naming the two folders, which is what Win7 shows.
    FileProgressDialog(KJob *job, const QString &source, const QString &destination,
                       QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildDetails();
    void setExpanded(bool expanded);
    void refreshHeading();
    void refreshDetails();

    // "2 Minutes and 15 Seconds", as Win7 words it, or empty when there is not
    // yet enough to estimate from.
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

    QLabel *m_expander = nullptr;
    Win7::ChevronButton *m_chevron = nullptr;

    // KJob reports each piece through its own signal at its own pace, so the
    // labels are rebuilt from this rather than from whichever fired last.
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
