#include "SystemPage.h"
#include "Commands.h"
#include "IconHelper.h"
#include "Win7Ui.h"
#include "Branding.h"
#include "dialogs/LinverConfigDialog.h"
#include "perf/WeiBenchmark.h"

#include <QEvent>
#include <QMouseEvent>

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QTextStream>
#include <QSysInfo>
#include <QHostInfo>
#include <QHash>
#include <QRegularExpression>
#include <cmath>

// Data gathering
// Read a single `key=value` field out of /etc/os-release, stripping any quotes.
static QString osReleaseField(const QString &key)
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&f);
    const QString prefix = key + QLatin1Char('=');
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(prefix)) {
            QString val = line.mid(prefix.size()).trimmed();
            if (val.size() >= 2 && val.startsWith('"') && val.endsWith('"'))
                val = val.mid(1, val.size() - 2);
            return val;
        }
    }
    return QString();
}

// The developer org / copyright holder for a distro, keyed by /etc/os-release
// ID (or an ID_LIKE token). os-release has no universal vendor field, so this
// covers the common distributions; anything unknown falls back to the distro's
// own name at the call site.
static QString vendorForId(const QString &id)
{
    static const QHash<QString, QString> vendors = {
        { QStringLiteral("cachyos"),    QStringLiteral("CachyOS") },
        { QStringLiteral("arch"),       QStringLiteral("Arch Linux") },
        { QStringLiteral("fedora"),     QStringLiteral("Fedora Project") },
        { QStringLiteral("ubuntu"),     QStringLiteral("Canonical Ltd.") },
        { QStringLiteral("kubuntu"),    QStringLiteral("Canonical Ltd.") },
        { QStringLiteral("debian"),     QStringLiteral("The Debian Project") },
        { QStringLiteral("linuxmint"),  QStringLiteral("The Linux Mint Team") },
        { QStringLiteral("manjaro"),    QStringLiteral("Manjaro Team") },
        { QStringLiteral("endeavouros"),QStringLiteral("EndeavourOS Team") },
        { QStringLiteral("opensuse"),           QStringLiteral("openSUSE Project") },
        { QStringLiteral("opensuse-leap"),      QStringLiteral("openSUSE Project") },
        { QStringLiteral("opensuse-tumbleweed"),QStringLiteral("openSUSE Project") },
        { QStringLiteral("pop"),        QStringLiteral("System76") },
        { QStringLiteral("gentoo"),     QStringLiteral("Gentoo Foundation") },
        { QStringLiteral("void"),       QStringLiteral("Void Linux") },
        { QStringLiteral("nixos"),      QStringLiteral("NixOS Contributors") },
        { QStringLiteral("rhel"),       QStringLiteral("Red Hat, Inc.") },
        { QStringLiteral("centos"),     QStringLiteral("The CentOS Project") },
        { QStringLiteral("rocky"),      QStringLiteral("Rocky Enterprise Software Foundation") },
        { QStringLiteral("almalinux"),  QStringLiteral("AlmaLinux OS Foundation") },
        { QStringLiteral("elementary"), QStringLiteral("elementary, Inc.") },
        { QStringLiteral("zorin"),      QStringLiteral("Zorin OS") },
    };
    return vendors.value(id);
}

// First matching field from /proc/cpuinfo (e.g. "model name").
//
// These /proc files report a size of 0, and QTextStream::readLine() treats a
// zero-length file as immediately at-EOF, so we must slurp the whole thing
// with readAll() (which reads until the device returns no more bytes) and split
// it ourselves.
static QString cpuInfoField(const QString &key)
{
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString text = QString::fromUtf8(f.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (const QStringView &line : lines) {
        const int colon = line.indexOf(u':');
        if (colon < 0)
            continue;
        if (line.left(colon).trimmed() == key)
            return line.mid(colon + 1).trimmed().toString();
    }
    return QString();
}

SystemPage::SysInfo SystemPage::gatherInfo()
{
    SysInfo s;

    // Edition
    s.edition = osReleaseField(QStringLiteral("PRETTY_NAME"));
    if (s.edition.isEmpty())
        s.edition = osReleaseField(QStringLiteral("NAME"));
    if (s.edition.isEmpty())
        s.edition = QSysInfo::prettyProductName();

    // The short name (no version) drives the badge's non-full-name case and the
    // branding dialog's dropdown label.
    s.editionShort = osReleaseField(QStringLiteral("NAME"));
    if (s.editionShort.isEmpty())
        s.editionShort = s.edition;

    // Copyright holder / developer org: prefer os-release's own VENDOR_NAME,
    // then a curated ID (or ID_LIKE) lookup, then the distro name itself.
    s.vendor = osReleaseField(QStringLiteral("VENDOR_NAME"));
    if (s.vendor.isEmpty())
        s.vendor = vendorForId(osReleaseField(QStringLiteral("ID")));
    if (s.vendor.isEmpty()) {
        const QStringList idLike =
            osReleaseField(QStringLiteral("ID_LIKE")).split(QLatin1Char(' '),
                                                            Qt::SkipEmptyParts);
        for (const QString &like : idLike) {
            s.vendor = vendorForId(like);
            if (!s.vendor.isEmpty())
                break;
        }
    }
    if (s.vendor.isEmpty())
        s.vendor = s.editionShort;

    s.kernel = QStringLiteral("%1 %2")
                   .arg(QSysInfo::kernelType().replace(0, 1,
                        QSysInfo::kernelType().left(1).toUpper()))
                   .arg(QSysInfo::kernelVersion());

    s.logoIcon = osReleaseField(QStringLiteral("LOGO"));

    // Processor
    s.processor = cpuInfoField(QStringLiteral("model name"));
    if (s.processor.isEmpty())
        s.processor = QStringLiteral("Unknown processor");

    // Prefer the advertised max frequency; fall back to the current clock.
    double mhz = 0.0;
    QFile maxFreq(QStringLiteral(
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"));
    if (maxFreq.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mhz = maxFreq.readAll().trimmed().toDouble() / 1000.0;  // kHz to MHz
    }
    if (mhz <= 0.0)
        mhz = cpuInfoField(QStringLiteral("cpu MHz")).toDouble();
    if (mhz > 0.0)
        s.clock = QStringLiteral("%1 GHz").arg(mhz / 1000.0, 0, 'f', 2);

    // Memory
    QFile mem(QStringLiteral("/proc/meminfo"));
    if (mem.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Slurp with readAll() for the same zero-size /proc reason as above.
        const QString text = QString::fromUtf8(mem.readAll());
        const QList<QStringView> lines = QStringView(text).split(u'\n');
        for (const QStringView &line : lines) {
            if (line.startsWith(QLatin1String("MemTotal:"))) {
                const double kb = line.toString()
                                      .split(QRegularExpression("\\s+"))
                                      .value(1).toDouble();
                // MemTotal is what the kernel can use; the physically installed
                // amount is a bit higher (firmware/kernel reserve some). Round
                // the usable figure up to the next even GiB to recover the
                // nominal installed capacity (e.g. 15.5 -> 16).
                const double usableGiB = kb / 1024.0 / 1024.0;
                int installedGiB = static_cast<int>(std::ceil(usableGiB));
                if (installedGiB % 2)
                    ++installedGiB;
                s.memory = QStringLiteral("%1 GB (%2 GB usable)")
                               .arg(installedGiB)
                               .arg(usableGiB, 0, 'f', 1);
                break;
            }
        }
    }

    // System type
    const QString arch = QSysInfo::currentCpuArchitecture();
    const bool is64 = (QSysInfo::WordSize == 64);
    s.systemType = QStringLiteral("%1-bit Operating System (%2)")
                       .arg(is64 ? 64 : 32)
                       .arg(arch);

    // Identity
    s.hostName = QHostInfo::localHostName();
    if (s.hostName.isEmpty())
        s.hostName = QSysInfo::machineHostName();

    // The "Product ID" slot shows the real systemd machine-id, the stable,
    // unique identifier for this OS installation (/etc/machine-id, with the
    // legacy D-Bus path as a fallback).
    for (const char *path : { "/etc/machine-id", "/var/lib/dbus/machine-id" }) {
        QFile f(QString::fromLatin1(path));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            s.productId = QString::fromLatin1(f.readAll()).trimmed();
            if (!s.productId.isEmpty())
                break;
        }
    }
    if (s.productId.isEmpty())
        s.productId = QString::fromLatin1(QSysInfo::machineUniqueId());

    return s;
}

// Clicking the "Rating" value navigates to the Linux Experience Index page;
// clicking the distributor logo opens the branding-configuration dialog.
bool SystemPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            if (watched == m_ratingLabel) {
                emit performanceRequested();
                return true;
            }
            if (watched == m_logoLabel) {
                const SysInfo info = gatherInfo();
                LinverConfigDialog dlg(info.editionShort, this);
                if (dlg.exec() == QDialog::Accepted)
                    emit brandingChanged();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Sidebar
QList<SidebarLink> SystemPage::sidebarLinks()
{
    return {
        Nav::command("Device Manager", kDeviceManagerCmd),
        Nav::plain("Remote settings"),
        Nav::plain("System protection"),
        Nav::plain("Advanced system settings"),
    };
}

QList<SidebarLink> SystemPage::sidebarSeeAlso()
{
    return {
        Nav::to("Action Center", PageId::ActionCenter),
        Nav::to("Linux Update", PageId::LinuxUpdate),
        Nav::to("Performance Information and Tools", PageId::Performance),
    };
}

// Layout helpers
QLabel *SystemPage::addInfoRow(QGridLayout *grid, int row,
                               const QString &label, const QString &value,
                               const QString &trailing, bool valueIsLink)
{
    auto *lbl = new QLabel(label);
    {
        QFont f = lbl->font();
        f.setPointSize(9);
        lbl->setFont(f);
    }
    lbl->setStyleSheet("color: #333333; background: transparent;");
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    grid->addWidget(lbl, row, 0, Qt::AlignLeft | Qt::AlignTop);

    auto *val = new QLabel(value);
    {
        QFont f = val->font();
        f.setPointSize(9);
        val->setFont(f);
    }
    val->setWordWrap(true);
    val->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (valueIsLink) {
        val->setCursor(Qt::PointingHandCursor);
        val->setStyleSheet(
            "QLabel { color: #1F4E99; background: transparent; }"
            "QLabel:hover { color: #0033AA; }");
    } else {
        val->setStyleSheet("color: #000000; background: transparent;");
    }
    if (trailing.isEmpty()) {
        grid->addWidget(val, row, 1, Qt::AlignTop);
    } else {
        // Keep the trailing text (e.g. the clock speed) right beside the value
        // rather than pushed to the far-right column.
        auto *tr = new QLabel(trailing);
        QFont f = tr->font();
        f.setPointSize(9);
        tr->setFont(f);
        tr->setStyleSheet("color: #000000; background: transparent;");

        // A word-wrap label would be squeezed to its widest word inside the
        // HBox; keep the value on one line so the trailing text stays beside it.
        val->setWordWrap(false);

        auto *cell = new QWidget;
        cell->setStyleSheet("background: transparent;");
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(18);
        h->addWidget(val, 0, Qt::AlignTop);
        h->addWidget(tr, 0, Qt::AlignTop);
        h->addStretch(1);
        grid->addWidget(cell, row, 1, Qt::AlignTop);
    }
    return val;
}

// Page
SystemPage::SystemPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const SysInfo info = gatherInfo();

    auto *contentV = Win7::pageScaffold(this, sidebar);

    // Page title.
    contentV->addWidget(
        Win7::pageTitle("View basic information about your computer"));
    contentV->addSpacing(18);

    // Section heading with an optional `trailing` widget (e.g. a "Change
    // settings" link) pinned to the far right after the rule.
    auto addHeading = [&](const QString &text, QWidget *trailing = nullptr) {
        contentV->addLayout(
            Win7::sectionHeading(text, trailing, nullptr, "#000000"));
        contentV->addSpacing(6);
    };

    // Linux edition (with distro logo on the right)
    addHeading(Branding::brand("Linux edition"));

    auto *editionRow = new QHBoxLayout;
    editionRow->setContentsMargins(14, 0, 0, 0);
    editionRow->setSpacing(0);

    // Text wrapper kept separate so it can be top-anchored in the row. Adding
    // the column layout directly would let the QHBoxLayout stretch it to the
    // logo's 96px height, spreading the rows out as if vertically centred.
    auto *editionTextWrap = new QWidget;
    editionTextWrap->setStyleSheet("background: transparent;");
    auto *editionCol = new QVBoxLayout(editionTextWrap);
    editionCol->setContentsMargins(0, 0, 0, 0);
    editionCol->setSpacing(7);   // match the row spacing used by the info grids

    // Order: edition name, copyright, then version line at the bottom. All three
    // follow the branding choice: the real distro identity, or fake Windows 7.
    editionCol->addWidget(Win7::label(
        Branding::editionName(info.edition)));
    editionCol->addWidget(Win7::label(
        QString::fromUtf8("Copyright © %1  %2.  All rights reserved.")
            .arg(Branding::copyrightYear())
            .arg(Branding::copyrightHolder(info.vendor))));
    editionCol->addWidget(Win7::label(
        Branding::fakeBadgeActive() ? QStringLiteral("Service Pack 1")
                                    : info.kernel));

    editionRow->addWidget(editionTextWrap, 1, Qt::AlignTop);

    // Distro logo (or the fake Windows flag), mirroring the big edition badge in
    // the reference. Clicking it opens the "Linver configuration" dialog.
    m_logoLabel = new QLabel;
    m_logoLabel->setFixedSize(96, 96);
    m_logoLabel->setStyleSheet("background: transparent;");
    m_logoLabel->setCursor(Qt::PointingHandCursor);
    m_logoLabel->setToolTip(QStringLiteral("Configure branding"));
    if (Branding::fakeBadgeActive()) {
        // The orb-backed Windows 7 logo. The theme only draws the orb at <=48px
        // (its 256px raster is the bare flag), so this upscales the 48px orb.
        m_logoLabel->setPixmap(Branding::windowsOrb(96));
    } else {
        m_logoLabel->setPixmap(themeIcon({ info.logoIcon.toUtf8().constData(),
                                           "distributor-logo", "start-here",
                                           "computer", "preferences-system" })
                                   .pixmap(96, 96));
    }
    m_logoLabel->installEventFilter(this);
    editionRow->addWidget(m_logoLabel, 0, Qt::AlignTop);

    contentV->addLayout(editionRow);
    contentV->addSpacing(16);

    // System
    addHeading("System");

    auto *sysGrid = new QGridLayout;
    sysGrid->setContentsMargins(14, 0, 0, 0);
    sysGrid->setHorizontalSpacing(16);
    sysGrid->setVerticalSpacing(7);
    sysGrid->setColumnStretch(1, 1);

    int r = 0;
    // The rating reflects the cached Linux Experience Index, if one exists, and
    // always links to the Performance Information and Tools page.
    const WeiResult wei = WeiResult::load();
    const QString ratingText = wei.valid && wei.baseScore > 0.0
        ? QStringLiteral("%1   %2")
              .arg(wei.baseScore, 0, 'f', 1)
              .arg(Branding::brand("Linux Experience Index"))
        : Branding::brand("Linux Experience Index is not available");
    m_ratingLabel = addInfoRow(sysGrid, r++, "Rating:", ratingText,
                               QString(), /*valueIsLink=*/true);
    m_ratingLabel->installEventFilter(this);
    addInfoRow(sysGrid, r++, "Processor:", info.processor, info.clock);
    addInfoRow(sysGrid, r++, "Installed memory (RAM):", info.memory);
    addInfoRow(sysGrid, r++, "System type:", info.systemType);
    addInfoRow(sysGrid, r++, "Pen and Touch:",
               "No Pen or Touch Input is available for this Display");

    contentV->addLayout(sysGrid);
    contentV->addSpacing(16);

    // Computer name, domain, and workgroup settings
    addHeading("Computer name, domain, and workgroup settings");

    auto *nameGrid = new QGridLayout;
    nameGrid->setContentsMargins(14, 0, 0, 0);
    nameGrid->setHorizontalSpacing(16);
    nameGrid->setVerticalSpacing(7);
    nameGrid->setColumnStretch(1, 1);

    r = 0;
    addInfoRow(nameGrid, r++, "Computer name:", info.hostName);
    addInfoRow(nameGrid, r++, "Full computer name:", info.hostName);
    addInfoRow(nameGrid, r++, "Computer description:", QString());
    addInfoRow(nameGrid, r++, "Workgroup:", "WORKGROUP");

    // "Change settings" sits at the right edge, on the "Computer name:" row.
    nameGrid->addWidget(Win7::bodyLabel("Change settings", /*link=*/true),
                        0, 2, Qt::AlignRight | Qt::AlignVCenter);

    contentV->addLayout(nameGrid);
    contentV->addSpacing(16);

    // Linux activation
    addHeading(Branding::brand("Linux activation"));

    auto *actGrid = new QGridLayout;
    actGrid->setContentsMargins(14, 0, 0, 0);
    actGrid->setHorizontalSpacing(16);
    actGrid->setVerticalSpacing(7);
    actGrid->setColumnStretch(1, 1);

    actGrid->addWidget(Win7::label(Branding::brand("Linux is activated")),
                       0, 0, 1, 2);
    addInfoRow(actGrid, 1, "Product ID:", info.productId);

    contentV->addLayout(actGrid);

    contentV->addStretch(1);
}
