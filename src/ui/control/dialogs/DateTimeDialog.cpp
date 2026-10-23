#include "ui/control/dialogs/DateTimeDialog.h"
#include "MessageBoxes.h"
#include "ui/control/AnalogClock.h"
#include "ui/control/IconHelper.h"
#include "data/control/WindowsTimeZones.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QCalendarWidget>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QFrame>
#include <QStyle>
#include <QTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDateTime>
#include <QTimeZone>
#include <QLocale>
#include <QSettings>
#include <QFont>
#include <QRegularExpression>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include "aero/palette.h"
#include "aero/artwork.h"
#include "aero/text.h"

namespace ControlPanel {

// System backend helpers
namespace {

// Run a short read-only helper under a C locale so field labels stay parseable
QString runTool(const QString &program, const QStringList &args)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(program, args);
    if (!proc.waitForFinished(2500))
        return QString();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

// The locale's long date, but without the leading weekday name, Windows' date
// fields show "11 juli 2026", not "lördag 11 juli 2026". Strips the day-name
// token ('ddd'/'dddd') from the locale's own long-date pattern so the month
// order stays correct for every locale
QString longDateNoWeekday(const QDate &d)
{
    QLocale loc;
    QString fmt = loc.dateFormat(QLocale::LongFormat);
    fmt.remove(QRegularExpression(QStringLiteral("d{3,4}[,]?\\s*")));
    fmt = fmt.trimmed();
    return loc.toString(d, fmt);
}

QString systemZoneId()
{
    QString id = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (id.isEmpty())
        id = QStringLiteral("UTC");
    return id;
}

bool ntpEnabled()
{
    const QString out = runTool(QStringLiteral("timedatectl"),
                                { QStringLiteral("show"),
                                  QStringLiteral("-p"), QStringLiteral("NTP"),
                                  QStringLiteral("--value") });
    return out.trimmed() == QLatin1String("yes");
}

// The active NTP server, taken from `timedatectl timesync-status` ("Server:"
// line: an address optionally followed by "(hostname)"). Empty when not synced
QString ntpServer()
{
    const QString out = runTool(QStringLiteral("timedatectl"),
                                { QStringLiteral("timesync-status") });
    const QList<QStringView> lines = QStringView(out).split(u'\n');
    for (const QStringView &line : lines) {
        const QStringView t = line.trimmed();
        if (t.startsWith(u"Server:")) {
            QString val = t.mid(7).trimmed().toString();
            const int lp = val.indexOf(QLatin1Char('('));
            const int rp = val.indexOf(QLatin1Char(')'));
            if (lp >= 0 && rp > lp)
                // Prefer hostname
                return val.mid(lp + 1, rp - lp - 1).trimmed();
            return val;
        }
    }
    return QString();
}

// Windows-style label for a zone id: the grouped name if known, otherwise a
// synthesized "(UTC±HH:MM) Zone/Id" from the live offset
QString zoneLabel(const QString &id)
{
    const QString known = windowsDisplayForIana(id);
    if (!known.isEmpty())
        return known;

    const QTimeZone tz(id.toUtf8());
    QString prefix = QStringLiteral("(UTC) ");
    if (tz.isValid()) {
        const int secs = tz.standardTimeOffset(QDateTime::currentDateTimeUtc());
        QChar sign = QLatin1Char('+');
        if (secs < 0)
            sign = QLatin1Char('-');
        const int mins = qAbs(secs) / 60;
        prefix = QStringLiteral("(UTC%1%2:%3) ")
                     .arg(sign)
                     .arg(mins / 60, 2, 10, QLatin1Char('0'))
                     .arg(mins % 60, 2, 10, QLatin1Char('0'));
    }
    return prefix + id;
}

// The Daylight-Saving-Time sentence Windows shows under the zone, computed from
// the zone's next transition. Empty when the zone doesn't observe DST
QString dstNote(const QString &id)
{
    const QTimeZone tz(id.toUtf8());
    if (!tz.isValid() || !tz.hasDaylightTime())
        return QStringLiteral("This time zone does not observe Daylight Saving "
                              "Time.");

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QTimeZone::OffsetData next = tz.nextTransition(nowUtc);
    if (!next.atUtc.isValid())
        return QString();

    // The wall-clock reading at the change is the UTC instant plus the offset in
    // effect just before it (so a fall-back reads 03:00, as Windows shows)
    const int offBefore = tz.offsetFromUtc(next.atUtc.addSecs(-1));
    QDateTime wall = next.atUtc.toUTC().addSecs(offBefore);

    // Ending turns the clock back, beginning puts it forward
    QString change = QStringLiteral("begins");
    QString direction = QStringLiteral("forward");
    if (next.daylightTimeOffset == 0) {
        change = QStringLiteral("ends");
        direction = QStringLiteral("back");
    }
    return QStringLiteral(
               "Daylight Saving Time %1 on %2 at %3. The clock is set to go %4 "
               "1 hour at that time.")
        .arg(change)
        .arg(longDateNoWeekday(wall.date()))
        .arg(wall.toString(QStringLiteral("HH:mm")))
        .arg(direction);
}

// The Windows UAC "administrator" shield, drawn to mark privileged buttons.
// Qt's QStyle::SP_VistaShield is empty off Windows, so we paint the familiar
// quartered shield ourselves (blue top, gold and red below, thin light cross)
QIcon uacShieldIcon()
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath shield;
    shield.moveTo(8, 0.5);
    shield.lineTo(15, 2.5);
    shield.lineTo(15, 8);
    shield.cubicTo(15, 12.2, 11.2, 15, 8, 15.6);
    shield.cubicTo(4.8, 15, 1, 12.2, 1, 8);
    shield.lineTo(1, 2.5);
    shield.closeSubpath();

    painter.save();
    painter.setClipPath(shield);
    // Top: blue
    painter.fillRect(QRectF(0, 0, 16, 8), QColor("#2E77D0"));
    // Lower-left: gold
    painter.fillRect(QRectF(0, 8, 8, 8), QColor("#F2B01E"));
    // Lower-right: red
    painter.fillRect(QRectF(8, 8, 8, 8), QColor("#D0021B"));
    // The subtle light cross dividing the quarters
    painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
    painter.drawLine(QPointF(8, 1), QPointF(8, 15));
    painter.drawLine(QPointF(1.5, 8), QPointF(14.5, 8));
    painter.restore();

    painter.setPen(QPen(QColor("#E8EEF6"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shield);
    return QIcon(pm);
}

// A blue underline-on-hover help link, matching the app's other pages
QLabel *makeHelpLink(const QString &text)
{
    auto *label = new QLabel(text);
    Aero::setPointSize(label, 9);
    label->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }"
        "QLabel:hover { color: %2; }")
            .arg(QLatin1String(Aero::Palette::LinkText),
                 QLatin1String(Aero::Palette::LinkHover)));
    label->setCursor(Qt::PointingHandCursor);
    return label;
}

} // namespace

// Dialog
DateTimeDialog::DateTimeDialog(QWidget *parent)
    : QDialog(parent)
{
    m_timezone = systemZoneId();

    setWindowTitle(QStringLiteral("Date and Time"));
    setWindowIcon(Aero::themeIcon({"preferences-system-time", "clock",
                             "preferences-system"}));
    setModal(true);
    // Windows dialogs float white tab pages on a grey body; scope the grey to
    // the dialog itself so it doesn't bleed into the white tab pages
    setStyleSheet(QStringLiteral("DateTimeDialog { background: %1; }")
        .arg(QLatin1String(Aero::Palette::FooterSurface)));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(9, 9, 9, 9);
    root->setSpacing(8);

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildDateTimeTab(), QStringLiteral("Date and Time"));
    m_tabs->addTab(buildAdditionalClocksTab(), QStringLiteral("Additional Clocks"));
    m_tabs->addTab(buildInternetTimeTab(), QStringLiteral("Internet Time"));
    root->addWidget(m_tabs, 1);

    // OK / Cancel / Apply, right-aligned like the Windows dialog
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::Apply);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]() { saveSettings(); });

    refreshDateTime();
    refreshInternetStatus();

    // Tick the clock and the time value every second
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DateTimeDialog::refreshDateTime);
    m_timer->start();

    setFixedSize(432, 452);
}

void DateTimeDialog::showTab(Tab tab)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(tab);
}

// Tab 1: Date and Time
// A plain 9pt black label used throughout the tab
static QLabel *dtLabel(const QString &text)
{
    auto *label = new QLabel(text);
    Aero::setPointSize(label, 9);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(QLatin1String(Aero::Palette::Text)));
    return label;
}

QWidget *DateTimeDialog::buildDateTimeTab()
{
    auto *page = new QWidget;
    page->setObjectName("dtPage");
    page->setStyleSheet(
        Aero::panelSheet(QStringLiteral("dtPage"),
                         Aero::Palette::Surface));
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(14, 12, 14, 10);
    column->setSpacing(0);

    // Clock + Date/Time block
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(16);

    m_clock = new AnalogClock(110);
    top->addWidget(m_clock, 0, Qt::AlignTop);

    auto *info = new QVBoxLayout;
    info->setContentsMargins(0, 6, 0, 0);
    info->setSpacing(0);
    info->addWidget(dtLabel(QStringLiteral("Date:")));
    m_dateValue = dtLabel(QString());
    info->addWidget(m_dateValue);
    info->addSpacing(12);
    info->addWidget(dtLabel(QStringLiteral("Time:")));
    m_timeValue = dtLabel(QString());
    info->addWidget(m_timeValue);
    info->addStretch(1);
    top->addLayout(info, 1);
    column->addLayout(top);
    column->addSpacing(4);

    // "Change date and time…", a privileged action, so it carries the UAC
    // shield, right-aligned like Windows
    auto *changeDtRow = new QHBoxLayout;
    changeDtRow->addStretch(1);
    auto *changeDt = new QPushButton(QStringLiteral("Change date and time…"));
    changeDt->setIcon(uacShieldIcon());
    changeDt->setCursor(Qt::PointingHandCursor);
    connect(changeDt, &QPushButton::clicked, this,
            &DateTimeDialog::openChangeDateTime);
    changeDtRow->addWidget(changeDt);
    column->addLayout(changeDtRow);
    column->addSpacing(12);

    // Section heading with a trailing hairline rule, the same divider the
    // System page uses
    auto addHeading = [&](const QString &text) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        row->addWidget(dtLabel(text), 0, Qt::AlignVCenter);
        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; }")
            .arg(QLatin1String(Aero::Palette::Hairline)));
        row->addWidget(line, 1, Qt::AlignVCenter);
        column->addLayout(row);
    };

    addHeading(QStringLiteral("Time zone"));
    column->addSpacing(6);

    m_tzValue = dtLabel(QString());
    m_tzValue->setContentsMargins(2, 0, 0, 0);
    column->addWidget(m_tzValue);
    column->addSpacing(6);

    auto *tzRow = new QHBoxLayout;
    tzRow->addStretch(1);
    auto *changeTz = new QPushButton(QStringLiteral("Change time zone…"));
    changeTz->setCursor(Qt::PointingHandCursor);
    connect(changeTz, &QPushButton::clicked, this,
            &DateTimeDialog::openChangeTimeZone);
    tzRow->addWidget(changeTz);
    column->addLayout(tzRow);
    column->addSpacing(10);

    m_dstNote = dtLabel(QString());
    m_dstNote->setWordWrap(true);
    column->addWidget(m_dstNote);
    column->addSpacing(8);

    m_notifyCheck = new QCheckBox(QStringLiteral("Notify me when the clock changes"));
    Aero::setPointSize(m_notifyCheck, 9);
    QSettings settings;
    m_notifyCheck->setChecked(
        settings.value(QStringLiteral("datetime/notifyClockChange"), true).toBool());
    column->addWidget(m_notifyCheck);

    column->addStretch(1);

    column->addWidget(makeHelpLink(
        QStringLiteral("Get more time zone information online")));
    column->addSpacing(2);
    column->addWidget(makeHelpLink(
        QStringLiteral("How do I set the clock and time zone?")));

    return page;
}

// Tab 2: Additional Clocks
QWidget *DateTimeDialog::buildAdditionalClocksTab()
{
    auto *page = new QWidget;
    page->setObjectName("acPage");
    page->setStyleSheet(
        Aero::panelSheet(QStringLiteral("acPage"),
                         Aero::Palette::Surface));
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(16, 16, 16, 12);
    column->setSpacing(10);

    auto *intro = new QLabel(
        QStringLiteral("Additional clocks can display the time in other time "
                       "zones. You can view them by clicking on or hovering "
                       "over the taskbar clock."));
    Aero::setPointSize(intro, 9);
    intro->setWordWrap(true);
    column->addWidget(intro);

    column->addSpacing(6);

    QSettings settings;
    for (int i = 1; i <= 2; ++i) {
        // A checkable group box: the "Show this clock" checkbox is the box's
        // title, embedded in the top of a light framed rectangle, the same
        // hairline divider as the Time zone section, closed into a box like
        // Windows. Unchecking it disables the fields inside automatically
        auto *box = new QGroupBox(QStringLiteral("Show this clock"));
        box->setCheckable(true);
        const QString showKey = QStringLiteral("datetime/clock%1/show").arg(i);
        box->setChecked(settings.value(showKey, false).toBool());
        Aero::setPointSize(box, 9);
        // The title's white background masks the border behind it, leaving the
        // Windows-style gap in the top edge where the checkbox sits
        box->setStyleSheet(
            QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: 3px;"
            " margin-top: 7px; background: transparent; }"
            "QGroupBox::title { subcontrol-origin: margin;"
            " subcontrol-position: top left; left: 8px; padding: 0 4px;"
            " background: %2; color: %3; }")
                .arg(QLatin1String(Aero::Palette::Hairline),
                     QLatin1String(Aero::Palette::Surface),
                     QLatin1String(Aero::Palette::Text)));

        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(18, 12, 14, 12);
        bv->setSpacing(4);

        auto *tzLabel = new QLabel(QStringLiteral("Select time zone:"));
        Aero::setPointSize(tzLabel, 9);
        bv->addWidget(tzLabel);

        auto *tzCombo = new QComboBox;
        for (const WinTimeZone &z : windowsTimeZones())
            tzCombo->addItem(z.display, z.iana);
        // Default the selection to the current zone
        int cur = tzCombo->findData(m_timezone);
        if (cur < 0)
            cur = tzCombo->findText(zoneLabel(m_timezone));
        tzCombo->setCurrentIndex(qMax(0, cur));
        bv->addWidget(tzCombo);
        bv->addSpacing(2);

        auto *nameLabel = new QLabel(QStringLiteral("Enter display name:"));
        Aero::setPointSize(nameLabel, 9);
        bv->addWidget(nameLabel);

        auto *nameEdit = new QLineEdit(
            settings.value(QStringLiteral("datetime/clock%1/name").arg(i),
                    QStringLiteral("Clock %1").arg(i)).toString());
        bv->addWidget(nameEdit);

        // Tag the widgets so saveSettings can find them; the checkable box
        // itself carries the "show" state
        box->setObjectName(QStringLiteral("clock%1_show").arg(i));
        tzCombo->setObjectName(QStringLiteral("clock%1_tz").arg(i));
        nameEdit->setObjectName(QStringLiteral("clock%1_name").arg(i));

        column->addWidget(box);
        column->addSpacing(10);
    }

    column->addStretch(1);
    return page;
}

// Tab 3: Internet Time
QWidget *DateTimeDialog::buildInternetTimeTab()
{
    auto *page = new QWidget;
    page->setObjectName("itPage");
    page->setStyleSheet(
        Aero::panelSheet(QStringLiteral("itPage"),
                         Aero::Palette::Surface));
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(16, 18, 16, 12);
    column->setSpacing(0);

    m_internetStatus = new QLabel;
    Aero::setPointSize(m_internetStatus, 9);
    m_internetStatus->setWordWrap(true);
    column->addWidget(m_internetStatus);
    column->addSpacing(22);

    m_internetSched = new QLabel;
    Aero::setPointSize(m_internetSched, 9);
    m_internetSched->setWordWrap(true);
    column->addWidget(m_internetSched);

    column->addStretch(1);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *change = new QPushButton(QStringLiteral("Change settings…"));
    change->setIcon(uacShieldIcon());
    change->setCursor(Qt::PointingHandCursor);
    connect(change, &QPushButton::clicked, this,
            &DateTimeDialog::openInternetSettings);
    btnRow->addWidget(change);
    column->addLayout(btnRow);
    column->addSpacing(10);

    column->addWidget(makeHelpLink(
        QStringLiteral("What is Internet time synchronization?")));

    return page;
}

// Live refreshers
void DateTimeDialog::refreshDateTime()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (m_dateValue)
        m_dateValue->setText(longDateNoWeekday(now.date()));
    if (m_timeValue)
        m_timeValue->setText(now.toString(QStringLiteral("HH:mm:ss")));
    if (m_clock)
        m_clock->update();
    if (m_tzValue)
        m_tzValue->setText(zoneLabel(m_timezone));
    if (m_dstNote)
        m_dstNote->setText(dstNote(m_timezone));
}

void DateTimeDialog::refreshInternetStatus()
{
    const bool on = ntpEnabled();
    if (on) {
        const QString server = ntpServer();
        if (server.isEmpty()) {
            m_internetStatus->setText(
                QStringLiteral("This computer is set to automatically "
                               "synchronize with an Internet time server."));
        } else {
            m_internetStatus->setText(
                QStringLiteral("This computer is set to automatically "
                               "synchronize with '%1'.").arg(server));
        }
        m_internetSched->setText(
            QStringLiteral("This computer is set to automatically synchronize "
                           "on a scheduled basis."));
    } else {
        m_internetStatus->setText(
            QStringLiteral("This computer is not set to automatically "
                           "synchronize with an Internet time server."));
        m_internetSched->clear();
    }
}

// Privileged actions
void DateTimeDialog::runPrivileged(const QStringList &argv,
                                   std::function<void()> onDone)
{
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, onDone](int code, QProcess::ExitStatus) {
                if (code != 0) {
                    const QString err =
                        QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    // Exit 126/127 == polkit auth dismissed/failed; stay quiet
                    if (!err.isEmpty() && code != 126 && code != 127)
                        MessageBoxes::showError(this, QStringLiteral("Date and Time"),
                                                err);
                }
                if (onDone)
                    onDone();
                proc->deleteLater();
            });
    proc->start(QStringLiteral("pkexec"), argv);
}

void DateTimeDialog::openChangeDateTime()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Date and Time Settings"));
    dlg.setModal(true);
    auto *column = new QVBoxLayout(&dlg);
    column->setContentsMargins(16, 16, 16, 12);
    column->setSpacing(12);

    auto *row = new QHBoxLayout;
    row->setSpacing(18);

    auto *cal = new QCalendarWidget;
    cal->setGridVisible(false);
    cal->setSelectedDate(QDate::currentDate());
    row->addWidget(cal, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(10);
    auto *clock = new AnalogClock(96);
    right->addWidget(clock, 0, Qt::AlignHCenter);
    auto *timeEdit = new QTimeEdit(QTime::currentTime());
    timeEdit->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    timeEdit->setAlignment(Qt::AlignCenter);
    right->addWidget(timeEdit);
    right->addStretch(1);
    row->addLayout(right, 0);

    column->addLayout(row, 1);

    auto *hint = new QLabel;
    Aero::setPointSize(hint, 9);
    hint->setStyleSheet("color: #555555;");
    if (ntpEnabled())
        hint->setText(QStringLiteral(
            "Internet time synchronization is on; turn it off in the Internet "
            "Time tab before setting the clock manually."));
    hint->setWordWrap(true);
    column->addWidget(hint);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok
                                    | QDialogButtonBox::Cancel);
    column->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QDateTime chosen(cal->selectedDate(), timeEdit->time());
    const QString stamp = chosen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    runPrivileged({ QStringLiteral("timedatectl"), QStringLiteral("set-time"),
                    stamp },
                  [this]() { refreshDateTime(); });
}

void DateTimeDialog::openChangeTimeZone()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Time Zone Settings"));
    dlg.setModal(true);
    auto *column = new QVBoxLayout(&dlg);
    column->setContentsMargins(16, 16, 16, 12);
    column->setSpacing(10);

    auto *label = new QLabel(QStringLiteral("Time zone:"));
    column->addWidget(label);

    auto *combo = new QComboBox;
    for (const WinTimeZone &z : windowsTimeZones())
        combo->addItem(z.display, z.iana);
    int cur = combo->findData(m_timezone);
    if (cur < 0) {
        // The system zone isn't one of the grouped Windows zones: add it so the
        // current setting is always represented and selectable
        combo->addItem(zoneLabel(m_timezone), m_timezone);
        cur = combo->count() - 1;
    }
    combo->setCurrentIndex(cur);
    column->addWidget(combo);
    column->addSpacing(6);

    auto *dst = new QCheckBox(
        QStringLiteral("Automatically adjust clock for Daylight Saving Time"));
    dst->setChecked(true);
    // IANA zones handle DST automatically
    dst->setEnabled(false);
    column->addWidget(dst);

    column->addStretch(1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok
                                    | QDialogButtonBox::Cancel);
    column->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString id = combo->currentData().toString();
    if (id.isEmpty() || id == m_timezone)
        return;
    runPrivileged({ QStringLiteral("timedatectl"),
                    QStringLiteral("set-timezone"), id },
                  [this, id]() {
                      m_timezone = systemZoneId();
                      refreshDateTime();
                  });
}

void DateTimeDialog::openInternetSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Internet Time Settings"));
    dlg.setModal(true);
    auto *column = new QVBoxLayout(&dlg);
    column->setContentsMargins(16, 16, 16, 12);
    column->setSpacing(10);

    column->addWidget(new QLabel(QStringLiteral("Configure Internet time settings:")));

    auto *sync = new QCheckBox(
        QStringLiteral("Synchronize with an Internet time server"));
    sync->setChecked(ntpEnabled());
    column->addWidget(sync);

    auto *serverRow = new QHBoxLayout;
    serverRow->setSpacing(8);
    auto *serverLabel = new QLabel(QStringLiteral("Server:"));
    serverRow->addWidget(serverLabel);
    auto *serverCombo = new QComboBox;
    serverCombo->setEditable(true);
    const QStringList servers = { "time.cloudflare.com", "time.windows.com",
                                  "pool.ntp.org", "time.google.com",
                                  "0.arch.pool.ntp.org" };
    serverCombo->addItems(servers);
    const QString curServer = ntpServer();
    if (!curServer.isEmpty()) {
        if (serverCombo->findText(curServer) < 0)
            serverCombo->insertItem(0, curServer);
        serverCombo->setCurrentText(curServer);
    }
    serverRow->addWidget(serverCombo, 1);
    auto *update = new QPushButton(QStringLiteral("Update now"));
    serverRow->addWidget(update);
    column->addLayout(serverRow);

    // Enable server controls only while synchronization is on
    auto syncEnable = [serverLabel, serverCombo, update](bool on) {
        serverLabel->setEnabled(on);
        serverCombo->setEnabled(on);
        update->setEnabled(on);
    };
    syncEnable(sync->isChecked());
    connect(sync, &QCheckBox::toggled, &dlg, syncEnable);

    // "Update now" forces a resync by restarting timesyncd
    connect(update, &QPushButton::clicked, &dlg, [this]() {
        runPrivileged({ QStringLiteral("systemctl"), QStringLiteral("restart"),
                        QStringLiteral("systemd-timesyncd") },
                      [this]() { refreshInternetStatus(); });
    });

    column->addSpacing(4);
    auto *note = new QLabel;
    Aero::setPointSize(note, 9);
    note->setWordWrap(true);
    note->setStyleSheet("color: #555555;");
    note->setText(QStringLiteral(
        "This computer is set to automatically synchronize on a scheduled "
        "basis."));
    column->addWidget(note);

    column->addStretch(1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok
                                    | QDialogButtonBox::Cancel);
    column->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const bool wantSync = sync->isChecked();
    const QString server = serverCombo->currentText().trimmed();

    // Toggle NTP to match the checkbox
    QString ntp = QStringLiteral("false");
    if (wantSync)
        ntp = QStringLiteral("true");
    runPrivileged({ QStringLiteral("timedatectl"), QStringLiteral("set-ntp"), ntp },
                  [this]() { refreshInternetStatus(); });

    // If synchronizing, point timesyncd at the chosen server via a drop-in and
    // restart it, all in one elevated shell so the user sees a single prompt
    if (wantSync && !server.isEmpty() && server != curServer) {
        const QString script = QStringLiteral(
            "mkdir -p /etc/systemd/timesyncd.conf.d && "
            "printf '[Time]\\nNTP=%1\\n' > "
            "/etc/systemd/timesyncd.conf.d/10-controlpanel.conf && "
            "systemctl restart systemd-timesyncd").arg(server);
        runPrivileged({ QStringLiteral("/bin/sh"), QStringLiteral("-c"), script },
                      [this]() { refreshInternetStatus(); });
    }
}

// Persistence for the cosmetic settings
void DateTimeDialog::saveSettings()
{
    QSettings settings;
    if (m_notifyCheck)
        settings.setValue(QStringLiteral("datetime/notifyClockChange"),
                   m_notifyCheck->isChecked());

    // Additional clocks live on the second tab; find their widgets by name
    for (int i = 1; i <= 2; ++i) {
        if (auto *show = m_tabs->findChild<QGroupBox *>(
                QStringLiteral("clock%1_show").arg(i)))
            settings.setValue(QStringLiteral("datetime/clock%1/show").arg(i),
                       show->isChecked());
        if (auto *tz = m_tabs->findChild<QComboBox *>(
                QStringLiteral("clock%1_tz").arg(i)))
            settings.setValue(QStringLiteral("datetime/clock%1/tz").arg(i),
                       tz->currentData());
        if (auto *name = m_tabs->findChild<QLineEdit *>(
                QStringLiteral("clock%1_name").arg(i)))
            settings.setValue(QStringLiteral("datetime/clock%1/name").arg(i),
                       name->text());
    }
}

} // namespace ControlPanel
