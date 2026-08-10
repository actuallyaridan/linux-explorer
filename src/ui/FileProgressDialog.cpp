#include "FileProgressDialog.h"
#include "Win7Ui.h"

#include <KIO/Global>
#include <KJob>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kBarHeight = 19;
constexpr int kDialogWidth = 440;

// How long an operation may run before the dialog appears. A window that
// flashes up and vanishes is worse than no window at all; same reasoning as
// the file list's "Working on it..." page.
constexpr int kAppearanceDelay = 500;

// The band behind the heading, sampled from a Windows 7 copy dialog. It runs
// left to right, not top to bottom: pale blue under the text, deepening to navy
// at the right where Win7 puts its glass icon, and constant down the band's
// whole height, so a vertical gradient would be the wrong axis. The stops sit
// where the slope changes rather than evenly, the fade accelerating toward the
// dark end. It ends in a hard edge against the white body, with no divider.
constexpr int kHeaderHeight = 40;

// Elides a path in the middle, as Win7 does: "C:\User...\Documents".
QString elidePath(const QString &path, int maxChars = 42)
{
    if (path.length() <= maxChars)
        return path;
    const int keep = (maxChars - 3) / 2;
    return path.left(keep) + QStringLiteral("...") + path.right(keep);
}

} // namespace

FileProgressDialog::FileProgressDialog(KJob *job, const QString &source,
                                       const QString &destination, QWidget *parent)
    : QDialog(parent)
    , m_job(job)
    , m_source(source)
    , m_destination(destination)
{
    setWindowTitle(tr("Copying..."));
    // Modeless: a long copy must not lock the window behind it.
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("progressContent"));
    content->setStyleSheet("#progressContent { background: #FFFFFF; }");

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // The heading sits on its own gradient band, the rest on plain white.
    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("progressHeader"));
    header->setFixedHeight(kHeaderHeight);
    header->setStyleSheet(
        "#progressHeader { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        " stop:0 #DCE5F4, stop:0.46 #A0C3E4,"
        " stop:0.81 #3E668D, stop:1 #093D64); }");

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    m_heading = Win7::label(tr("Preparing..."), 12, "#000000");
    headerLayout->addWidget(m_heading, 1, Qt::AlignVCenter);
    contentLayout->addWidget(header);

    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(16, 12, 16, 14);
    bodyLayout->setSpacing(10);
    contentLayout->addWidget(body, 1);
    contentLayout = bodyLayout;   // everything below goes in the white body

    m_summary = Win7::label(QString(), 9);
    m_summary->setTextFormat(Qt::RichText);
    contentLayout->addWidget(m_summary);

    m_details = buildDetails();
    m_details->hide();
    contentLayout->addWidget(m_details);

    m_bar = new QProgressBar;
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(kBarHeight);
    contentLayout->addWidget(m_bar);

    root->addWidget(content, 1);

    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("progressFooter"));
    footer->setStyleSheet(
        "#progressFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");

    auto *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(12, 10, 12, 10);
    footerRow->setSpacing(8);

    m_chevron = new Win7::ChevronButton;
    footerRow->addWidget(m_chevron, 0, Qt::AlignVCenter);

    m_expander = Win7::label(tr("More details"), 9);
    m_expander->setCursor(Qt::PointingHandCursor);
    footerRow->addWidget(m_expander, 0, Qt::AlignVCenter);
    footerRow->addStretch(1);

    auto *cancel = new QPushButton(tr("Cancel"));
    Win7::setPointSize(cancel, 9);
    footerRow->addWidget(cancel);

    root->addWidget(footer);

    connect(m_chevron, &QToolButton::toggled, this, &FileProgressDialog::setExpanded);
    // The caption toggles it too, so the whole affordance is clickable rather
    // than just the 20px circle.
    m_expander->installEventFilter(this);

    // Cancel asks the job to stop and lets it report back through result(),
    // which closes the dialog. Killing quietly would say nothing about a
    // half-finished operation.
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (m_job)
            m_job->kill(KJob::EmitResult);
        else
            close();
    });

    connect(job, &KJob::description, this,
            [this](KJob *, const QString &title,
                   const QPair<QString, QString> &field1,
                   const QPair<QString, QString> &field2) {
        m_action = title;
        // These name the file in flight rather than the operation, so they feed
        // the details panel's "Name" row rather than the header. CopyJob emits
        // no infoMessage, so without this that row stays blank.
        const QString current = field1.second.isEmpty() ? field2.second : field1.second;
        if (!current.isEmpty())
            m_currentItem = current.section(QLatin1Char('/'), -1);
        refreshHeading();
        refreshDetails();
    });

    connect(job, &KJob::infoMessage, this, [this](KJob *, const QString &message) {
        m_currentItem = message;
        refreshDetails();
    });

    connect(job, &KJob::totalAmountChanged, this,
            [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes)
            m_totalBytes = amount;
        else if (unit == KJob::Files || unit == KJob::Items)
            m_totalItems = amount;
        refreshHeading();
        refreshDetails();
    });

    connect(job, &KJob::processedAmountChanged, this,
            [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes)
            m_processedBytes = amount;
        else if (unit == KJob::Files || unit == KJob::Items)
            m_processedItems = amount;
        refreshDetails();
    });

    connect(job, &KJob::percentChanged, this, [this](KJob *, unsigned long percent) {
        m_bar->setValue(int(percent));
    });

    connect(job, &KJob::speed, this, [this](KJob *, unsigned long bytesPerSecond) {
        m_speed = bytesPerSecond;
        refreshDetails();
    });

    // Once the job reports a result it is finished either way and the dialog has
    // nothing left to show. Errors are the job's UI delegate's to report.
    connect(job, &KJob::result, this, [this](KJob *) {
        m_job = nullptr;
        if (isVisible())
            close();
        else
            deleteLater();   // finished before it ever appeared
    });

    // The dialog shows itself rather than being shown by its creator, so a short
    // operation finishes without one appearing. Re-checked on fire: result()
    // clears m_job, which is exactly the case this delay exists to catch.
    QTimer::singleShot(kAppearanceDelay, this, [this] {
        if (m_job)
            show();
    });

    setFixedWidth(kDialogWidth);
    refreshHeading();
    refreshDetails();
}

QWidget *FileProgressDialog::buildDetails()
{
    auto *panel = new QWidget;
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);

    const auto addRow = [&](int row, const QString &caption, QLabel **valueOut) {
        grid->addWidget(Win7::label(caption, 9, "#5A5A5A"), row, 0, Qt::AlignTop);
        *valueOut = Win7::label(QString(), 9);
        grid->addWidget(*valueOut, row, 1);
    };

    addRow(0, tr("Name:"), &m_detailName);
    addRow(1, tr("From:"), &m_detailFrom);
    addRow(2, tr("To:"), &m_detailTo);
    addRow(3, tr("Time remaining:"), &m_detailRemainingTime);
    addRow(4, tr("Items remaining:"), &m_detailRemainingItems);
    addRow(5, tr("Speed:"), &m_detailSpeed);
    grid->setColumnStretch(1, 1);

    return panel;
}

void FileProgressDialog::setExpanded(bool expanded)
{
    m_details->setVisible(expanded);
    // Win7 shows one or the other: folded carries the from/to summary, unfolded
    // spells the same thing out in the grid.
    m_summary->setVisible(!expanded);
    m_expander->setText(expanded ? tr("Fewer details") : tr("More details"));
    QSignalBlocker blocker(m_chevron);
    m_chevron->setChecked(expanded);
    // Fixed-width but height follows the panel; without this it keeps the taller
    // geometry after folding back up.
    adjustSize();
}

void FileProgressDialog::refreshHeading()
{
    const QString action = m_action.isEmpty() ? tr("Copying") : m_action;

    QString what;
    if (m_totalItems > 0) {
        what = m_totalItems == 1 ? tr("1 item") : tr("%1 items").arg(m_totalItems);
        if (m_totalBytes > 0)
            what = tr("%1 (%2)").arg(what, KIO::convertSize(m_totalBytes));
    } else if (m_totalBytes > 0) {
        what = KIO::convertSize(m_totalBytes);
    }

    m_heading->setText(what.isEmpty() ? action : QStringLiteral("%1 %2").arg(action, what));
    setWindowTitle(m_heading->text());

    if (!m_source.isEmpty() && !m_destination.isEmpty()) {
        m_summary->setText(tr("from <b>%1</b> to <b>%2</b>")
                               .arg(elidePath(m_source).toHtmlEscaped(),
                                    elidePath(m_destination).toHtmlEscaped()));
    }
}

QString FileProgressDialog::remainingText() const
{
    if (m_speed == 0 || m_totalBytes <= m_processedBytes)
        return {};

    const qulonglong left = m_totalBytes - m_processedBytes;
    const qulonglong seconds = left / m_speed;

    if (seconds < 60)
        return tr("About %1 Seconds").arg(seconds);

    const qulonglong minutes = seconds / 60;
    const qulonglong rest = seconds % 60;
    if (rest == 0)
        return tr("About %1 Minutes").arg(minutes);
    return tr("About %1 Minutes and %2 Seconds").arg(minutes).arg(rest);
}

void FileProgressDialog::refreshDetails()
{
    m_detailName->setText(m_currentItem);
    m_detailFrom->setText(elidePath(m_source));
    m_detailTo->setText(elidePath(m_destination));
    m_detailRemainingTime->setText(remainingText());

    // KIO counts a file as processed the moment it starts, so a single-file copy
    // reports 1 of 1 done while the bytes are still moving. Win7 counts the file
    // in flight as remaining ("1 (2.96 GB)").
    qulonglong left = m_totalItems > m_processedItems ? m_totalItems - m_processedItems : 0;
    if (left == 0 && m_totalItems > 0 && m_totalBytes > m_processedBytes)
        left = 1;

    if (left > 0) {
        const QString items = left == 1 ? tr("1 item") : tr("%1 items").arg(left);
        m_detailRemainingItems->setText(
            m_totalBytes > m_processedBytes
                ? tr("%1 (%2)").arg(items,
                                    KIO::convertSize(m_totalBytes - m_processedBytes))
                : items);
    } else {
        m_detailRemainingItems->setText(QString());
    }

    m_detailSpeed->setText(m_speed > 0
                               ? tr("%1/second").arg(KIO::convertSize(m_speed))
                               : QString());
}

bool FileProgressDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_expander && event->type() == QEvent::MouseButtonRelease) {
        m_chevron->toggle();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}
