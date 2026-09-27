#include "FileProgressDialog.h"
#include "aero/buttons.h"
#include "aero/palette.h"
#include "aero/text.h"

#include <KIO/Global>
#include <KJob>

#include <QCloseEvent>
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

// How long an operation may run before the dialog appears, a window that
// flashes up and vanishes being worse than none at all
constexpr int kAppearanceDelay = 500;

// The band behind the heading, which runs left to right and is constant down
// its whole height, with the stops placed where the slope changes
constexpr int kHeaderHeight = 40;

// Elides a path in the middle, as Win7 does
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
    : Aero::TaskDialog(parent)
    , m_job(job)
    , m_source(source)
    , m_destination(destination)
{
    setWindowTitle(tr("Copying..."));
    // Modeless, a long copy not being allowed to lock the window behind it
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);

    // The band reaches the dialog's edges, so the content keeps none of the
    // usual padding and the body below brings its own
    setContentMargins(QMargins(0, 0, 0, 0));
    contentLayout()->setSpacing(0);

    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("progressHeader"));
    header->setFixedHeight(kHeaderHeight);
    header->setStyleSheet(
        QStringLiteral("#progressHeader { background: qlineargradient("
                       "x1:0, y1:0, x2:1, y2:0,"
                       " stop:0 %1, stop:0.46 %2,"
                       " stop:0.81 %3, stop:1 %4); }")
            .arg(QLatin1String(Aero::Palette::BannerStart),
                 QLatin1String(Aero::Palette::BannerMid),
                 QLatin1String(Aero::Palette::BannerFar),
                 QLatin1String(Aero::Palette::BannerEnd)));

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    m_heading = Aero::label(tr("Preparing..."), 12, Aero::Palette::Text);
    headerLayout->addWidget(m_heading, 1, Qt::AlignVCenter);
    contentLayout()->addWidget(header);

    // The white body below the band
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(16, 12, 16, 14);
    bodyLayout->setSpacing(10);
    contentLayout()->addWidget(body, 1);

    m_summary = Aero::label(QString(), 9);
    m_summary->setTextFormat(Qt::RichText);
    bodyLayout->addWidget(m_summary);

    m_details = buildDetails();
    m_details->hide();
    bodyLayout->addWidget(m_details);

    m_bar = new QProgressBar;
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(kBarHeight);
    bodyLayout->addWidget(m_bar);

    m_chevron = new Aero::ChevronButton;
    addFooterWidget(m_chevron);

    // The caption toggles it too, not just the circle
    m_expander = new Aero::LinkLabel(tr("More details"));
    m_expander->setColors(Aero::Palette::Text, Aero::Palette::Text);
    Aero::setPointSize(m_expander, 9);
    addFooterWidget(m_expander);

    m_pause = addButton(tr("Pause"));
    auto *cancel = addButton(tr("Cancel"));

    connect(m_chevron, &QToolButton::toggled, this, &FileProgressDialog::setExpanded);
    connect(m_expander, &Aero::LinkLabel::clicked, m_chevron, &QToolButton::toggle);

    // Both report whether the job supports being held, so a worker that cannot
    // be stops offering it rather than showing a button that lies
    connect(m_pause, &QPushButton::clicked, this, [this] {
        if (!m_job)
            return;

        bool supported = false;
        if (m_job->isSuspended())
            supported = m_job->resume();
        else
            supported = m_job->suspend();

        if (!supported) {
            m_pause->setEnabled(false);
            return;
        }

        if (m_job->isSuspended())
            m_pause->setText(tr("Resume"));
        else
            m_pause->setText(tr("Pause"));
        refreshHeading();
    });

    // Cancel asks the job to stop and lets it report back, killing it quietly
    // saying nothing about a half finished operation
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
        // These name the file in flight, so they feed the details panel rather
        // than the header, and a copy reports nothing else that would
        QString current = field1.second;
        if (current.isEmpty())
            current = field2.second;
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

    // A result means finished either way, errors being the delegate's to report
    connect(job, &KJob::result, this, [this](KJob *) {
        m_job = nullptr;
        if (m_closing)
            return;   // the close is already in flight and did the killing
        if (isVisible())
            close();
        else
            deleteLater();   // finished before it ever appeared
    });

    // The dialog shows itself, so a short operation finishes without one
    // appearing, and the job is checked again on the way in
    QTimer::singleShot(kAppearanceDelay, this, [this] {
        if (m_job)
            show();
    });

    setFixedWidth(kDialogWidth);
    refreshHeading();
    refreshDetails();
}

void FileProgressDialog::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    if (KJob *job = m_job) {
        m_job = nullptr;
        job->kill(KJob::EmitResult);
    }
    Aero::TaskDialog::closeEvent(event);
}

QWidget *FileProgressDialog::buildDetails()
{
    auto *panel = new QWidget;
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);

    // A grey caption on the left and an empty value label on the right, which
    // is handed back through valueOut to be filled in later
    const auto addRow = [grid](int row, const QString &caption, QLabel **valueOut) {
        QLabel *captionLabel = Aero::label(caption, 9, Aero::Palette::MutedText);
        grid->addWidget(captionLabel, row, 0, Qt::AlignTop);

        *valueOut = Aero::label(QString(), 9);
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
    // Win7 shows one or the other, the summary or the grid, never both
    m_summary->setVisible(!expanded);
    if (expanded)
        m_expander->setText(tr("Fewer details"));
    else
        m_expander->setText(tr("More details"));
    QSignalBlocker blocker(m_chevron);
    m_chevron->setChecked(expanded);
    // Fixed width, but the height must follow the panel or the dialog keeps its
    // taller geometry after folding back up
    adjustSize();
}

void FileProgressDialog::refreshHeading()
{
    QString action = m_action;
    if (action.isEmpty())
        action = tr("Copying");

    // How much is being worked on, such as "3 items (12 MB)"
    QString what;
    if (m_totalItems > 0) {
        if (m_totalItems == 1)
            what = tr("1 item");
        else
            what = tr("%1 items").arg(m_totalItems);

        if (m_totalBytes > 0)
            what = tr("%1 (%2)").arg(what, KIO::convertSize(m_totalBytes));
    } else if (m_totalBytes > 0) {
        what = KIO::convertSize(m_totalBytes);
    }

    QString heading = action;
    if (!what.isEmpty())
        heading = QStringLiteral("%1 %2").arg(action, what);
    if (m_job && m_job->isSuspended())
        heading = tr("%1 (Paused)").arg(heading);

    m_heading->setText(heading);
    setWindowTitle(heading);

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

    const qulonglong bytesLeft = m_totalBytes - m_processedBytes;
    const qulonglong seconds = bytesLeft / m_speed;

    if (seconds < 60)
        return tr("About %1 Seconds").arg(seconds);

    const qulonglong minutes = seconds / 60;
    const qulonglong extraSeconds = seconds % 60;
    if (extraSeconds == 0)
        return tr("About %1 Minutes").arg(minutes);
    return tr("About %1 Minutes and %2 Seconds").arg(minutes).arg(extraSeconds);
}

void FileProgressDialog::refreshDetails()
{
    m_detailName->setText(m_currentItem);
    m_detailFrom->setText(elidePath(m_source));
    m_detailTo->setText(elidePath(m_destination));
    m_detailRemainingTime->setText(remainingText());

    const bool bytesLeft = m_totalBytes > m_processedBytes;

    qulonglong itemsLeft = 0;
    if (m_totalItems > m_processedItems)
        itemsLeft = m_totalItems - m_processedItems;

    // KIO counts a file as processed the moment it starts, where Win7 counts
    // the file in flight as remaining
    if (itemsLeft == 0 && m_totalItems > 0 && bytesLeft)
        itemsLeft = 1;

    if (itemsLeft > 0) {
        QString remaining;
        if (itemsLeft == 1)
            remaining = tr("1 item");
        else
            remaining = tr("%1 items").arg(itemsLeft);

        if (bytesLeft) {
            const QString size = KIO::convertSize(m_totalBytes - m_processedBytes);
            remaining = tr("%1 (%2)").arg(remaining, size);
        }
        m_detailRemainingItems->setText(remaining);
    } else {
        m_detailRemainingItems->clear();
    }

    if (m_speed > 0)
        m_detailSpeed->setText(tr("%1/second").arg(KIO::convertSize(m_speed)));
    else
        m_detailSpeed->clear();
}

