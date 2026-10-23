#include "ProgramsFeaturesPage.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStyle>
#include <QStandardPaths>
#include <QUrl>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLocale>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <climits>

// Data gathering
// "6.32 MiB" / "121.00 KiB" / "1.04 GiB" to bytes.
static qint64 parseSize(const QString &text)
{
    const QStringList parts = text.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2) return 0;
    const double value = QLocale::c().toDouble(parts[0]);
    const QString unit = parts[1];
    double mult = 1.0;
    if      (unit.startsWith("Ki")) mult = 1024.0;
    else if (unit.startsWith("Mi")) mult = 1024.0 * 1024.0;
    else if (unit.startsWith("Gi")) mult = 1024.0 * 1024.0 * 1024.0;
    else if (unit.startsWith("Ti")) mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    return static_cast<qint64>(value * mult);
}

// Bytes to "5.63 MB" / "2.70 GB" / "812 KB", mirroring the column in the
// reference screenshot (which lists everything in MB, totals in GB).
static QString humanSize(qint64 bytes)
{
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QString::number(kb, 'f', 0) + " KB";
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QString::number(mb, 'f', mb < 10.0 ? 2 : (mb < 100.0 ? 1 : 0)) + " MB";
    const double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}

// The Size column must sort by the real byte count (stored in SizeSortRole),
// not by the human-readable "5.63 MB" text. All other columns sort lexically,
// which is correct for names, publishers, versions and ISO-formatted dates.
namespace {
constexpr int SizeSortRole = Qt::UserRole + 1;

class ProgramItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 3) {
            return data(3, SizeSortRole).toLongLong()
                 < other.data(3, SizeSortRole).toLongLong();
        }
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};
} // namespace

// What we remember about each package's first appearance, read from
// /var/log/pacman.log. (pacman's own "Install Date" field is bumped on every
// upgrade, so it reflects the last touch, not the original install, useless
// here.) `byInstaller` records whether that first install was done by the OS
// installer rather than by the user later on.
struct InstallRecord { QDate date; bool byInstaller = false; };

// Does this pacman command line look like one the OS installer (Calamares) ran,
// rather than an interactive `pacman -S` or an AUR helper? The installer's
// chroot bootstrap and post-install steps carry tell-tale flags that ordinary
// installs never use: a Calamares-root or explicit "-r /" target, --cachedir,
// or the --noprogressbar / --disable-download-timeout pair. We deliberately do
// NOT key on --config or --noconfirm, since yay/paru's `pacman -U` calls use
// those too.
static bool isInstallerCommand(const QString &cmd)
{
    static const QRegularExpression rootFlag(R"((?:^|\s)-r\s*/)");
    return cmd.contains("calamares")
        || cmd.contains("--noprogressbar")
        || cmd.contains("--disable-download-timeout")
        || cmd.contains("--cachedir")
        || rootFlag.match(cmd).hasMatch();
}

// Walk the pacman log, attributing each package's first install to the command
// that ran it. `sawInstaller` reports whether any installer command was seen at
// all (false only for an unusually truncated log); `earliestDay` is the date of
// the very first install line, used as a fallback signal in that case.
static QHash<QString, InstallRecord> readInstallHistory(QDate &earliestDay,
                                                        bool &sawInstaller)
{
    QHash<QString, InstallRecord> hist;
    earliestDay  = QDate();
    sawInstaller = false;

    QFile log("/var/log/pacman.log");
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
        return hist;

    static const QRegularExpression runRe(
        R"(^\[[0-9T:+\-]+\] \[PACMAN\] Running '([^']*)')");
    // [2026-01-24T10:00:00+0100] [ALPM] installed code (1.124.0-1.1)
    static const QRegularExpression instRe(
        R"(^\[([0-9-]{10})T[0-9:+\-]+\] \[ALPM\] installed (\S+) )");

    bool curInstaller = false;
    const QString text = QString::fromUtf8(log.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const QString s = line.toString();

        const auto rm = runRe.match(s);
        if (rm.hasMatch()) {                       // a new transaction's command
            curInstaller = isInstallerCommand(rm.captured(1));
            if (curInstaller) sawInstaller = true;
            continue;
        }

        const auto im = instRe.match(s);
        if (!im.hasMatch()) continue;
        const QString pkg = im.captured(2);
        if (hist.contains(pkg)) continue;          // log is chronological: keep first
        InstallRecord rec;
        rec.date        = QDate::fromString(im.captured(1), "yyyy-MM-dd");
        rec.byInstaller = curInstaller;
        hist.insert(pkg, rec);
        if (!earliestDay.isValid() && rec.date.isValid())
            earliestDay = rec.date;
    }
    return hist;
}

// Is this package part of the operating system rather than user-installed
// software? Even among packages added after setup we hide categories whose
// removal would break boot, hardware or the desktop foundation: kernels and
// their headers, firmware, microcode, bootloader/initramfs, GPU/hardware
// drivers, plus the distro's own config and meta packages (cachyos-*) and the
// package managers / AUR helpers the system is run with (pacman, yay, etc).
static bool isSystemPackage(const QString &name)
{
    static const QRegularExpression sysRe(
        "^("
        "base|base-devel"                                  // base meta packages
        "|linux|linux-lts|linux-zen|linux-hardened"        // stock kernels
        "|linux-cachyos.*"                                 // cachyos kernels
        "|.*-headers"                                      // kernel/dev headers
        "|.*firmware"                                      // *-firmware
        "|.*-ucode"                                        // intel/amd microcode
        "|cachyos-.*"                                       // distro config & meta
        "|grub|grub-.*|refind|systemd-boot|efibootmgr|efitools|os-prober"
        "|mkinitcpio.*|dracut|booster|plymouth.*"          // initramfs / splash
        "|xf86-video-.*|xf86-input-.*|.*-dkms"             // hardware drivers
        "|nvidia.*|mesa|lib32-.*mesa.*|egl-wayland|vulkan-.*-driver"
        "|pacman.*|pamac.*|libpamac.*|archlinux-keyring"   // package management
        "|yay|yay-bin|paru|paru-bin|pikaur|trizen|aurman|pacaur|aura|octopi"  // AUR helpers
        ")$",
        QRegularExpression::CaseInsensitiveOption);
    return sysRe.match(name).hasMatch();
}

// The friendly name and icon read from a .desktop file's [Desktop Entry].
struct DesktopEntry { QString name; QString icon; };

// Parse the [Desktop Entry] group of a .desktop file. Returns false unless it
// is a displayable application with a name (skipping NoDisplay/Hidden helper
// entries such as VS Code's URL handler). Only the main "Name="/"Icon=" keys
// are read; localized "Name[xx]=" keys and per-action groups are ignored.
static bool parseDesktopEntry(const QString &path, DesktopEntry &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    bool inEntry = false, noDisplay = false, hidden = false;
    QString name, icon, type;
    const QString text = QString::fromUtf8(f.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (const QStringView &lv : lines) {
        const QString line = lv.trimmed().toString();
        if (line.startsWith('[')) {           // a new group begins
            inEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inEntry) continue;
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq);
        const QString val = line.mid(eq + 1);
        if      (key == "Name"      && name.isEmpty()) name = val;
        else if (key == "Icon"      && icon.isEmpty()) icon = val;
        else if (key == "Type")       type      = val;
        else if (key == "NoDisplay")  noDisplay = (val.compare("true", Qt::CaseInsensitive) == 0);
        else if (key == "Hidden")     hidden    = (val.compare("true", Qt::CaseInsensitive) == 0);
    }

    if (name.isEmpty() || noDisplay || hidden) return false;
    if (!type.isEmpty() && type != "Application") return false;
    out.name = name;
    out.icon = icon;
    return true;
}

// Publisher lookup
// pacman's "Packager" field only says who *built* the binary, the distro's
// build farm ("CachyOS") for repo packages, "Unknown Packager" for local AUR
// builds, never who actually makes or maintains the software. The Publisher
// column wants the latter, so we consult two better sources and keep the
// Packager-derived text only as a fallback:
//  - repo packages: the AppStream catalog's <developer> for the package
//    (e.g. steam -> "Valve Corporation"),
//  - foreign/AUR packages: the AUR maintainer from the RPC endpoint
//    (e.g. linux-devmgmt -> "actuallyaridan").

// The names of all foreign (not-in-any-repo, i.e. AUR/local) packages.
static QSet<QString> foreignPackages()
{
    QSet<QString> set;
    QProcess p;
    p.start("pacman", { "-Qmq" });
    if (!p.waitForFinished(10000)) return set;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &l : lines)
        set.insert(l.trimmed().toString());
    return set;
}

// Scan one AppStream catalog/metainfo XML document, adding pkgname -> developer
// entries. Handles both the current <developer><name> form and the legacy
// <developer_name> tag, ignoring translated (xml:lang) variants. Existing
// entries win, so the first component naming a package sticks.
static void parseAppStreamXml(const QByteArray &xml, QHash<QString, QString> &devs)
{
    QXmlStreamReader r(xml);
    QStringList pkgs;
    QString devName, legacyName;
    bool inDeveloper = false;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::EndElement) {
            if (r.name() == u"developer") {
                inDeveloper = false;
            } else if (r.name() == u"component") {
                const QString dev = !devName.isEmpty() ? devName : legacyName;
                if (!dev.isEmpty())
                    for (const QString &p : pkgs)
                        if (!devs.contains(p)) devs.insert(p, dev);
            }
            continue;
        }
        if (tok != QXmlStreamReader::StartElement) continue;

        const auto tag = r.name();
        const bool translated = r.attributes().hasAttribute("xml:lang");
        if (tag == u"component") {
            pkgs.clear(); devName.clear(); legacyName.clear();
            inDeveloper = false;
        }
        else if (tag == u"pkgname") {
            pkgs << r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        }
        else if (tag == u"developer") {
            inDeveloper = true;
        }
        else if (tag == u"name" && inDeveloper) {
            const QString t = r.readElementText(QXmlStreamReader::IncludeChildElements);
            if (!translated && devName.isEmpty()) devName = t.simplified();
        }
        else if (tag == u"developer_name") {
            const QString t = r.readElementText(QXmlStreamReader::IncludeChildElements);
            if (!translated && legacyName.isEmpty()) legacyName = t.simplified();
        }
    }
}

// pkgname -> upstream developer, read from the distro's AppStream catalogs
// (the compressed XML shipped by archlinux-appstream-data). Covers every repo
// desktop application; CLI-only packages have no AppStream component and keep
// the fallback. CachyOS rebuilds of Arch packages keep the same pkgname, so
// they match too.
static QHash<QString, QString> readAppStreamDevelopers()
{
    QHash<QString, QString> devs;
    static const QStringList dirs = {
        "/usr/share/swcatalog/xml", "/var/lib/swcatalog/xml",
        "/usr/share/app-info/xml",  "/var/lib/app-info/xml",   // pre-0.16 paths
    };
    for (const QString &dir : dirs) {
        const QFileInfoList files = QDir(dir).entryInfoList(
            { "*.xml", "*.xml.gz" }, QDir::Files);
        for (const QFileInfo &fi : files) {
            QByteArray xml;
            if (fi.fileName().endsWith(".gz")) {
                QProcess gz;
                gz.start("gzip", { "-dc", fi.absoluteFilePath() });
                if (!gz.waitForFinished(15000)) { gz.kill(); continue; }
                xml = gz.readAllStandardOutput();
            } else {
                QFile f(fi.absoluteFilePath());
                if (!f.open(QIODevice::ReadOnly)) continue;
                xml = f.readAll();
            }
            parseAppStreamXml(xml, devs);
        }
    }
    return devs;
}

// pkgname -> AUR maintainer, via batched requests to the AUR info RPC. Shells
// out to curl (pacman itself depends on it) with a short timeout, so an
// offline machine just keeps the fallback text. Orphaned packages report a
// null maintainer and also keep the fallback.
static QHash<QString, QString> fetchAurMaintainers(const QStringList &pkgs)
{
    QHash<QString, QString> out;
    constexpr int batch = 150;                 // keep the GET URL a sane length
    for (int i = 0; i < pkgs.size(); i += batch) {
        QStringList args;
        for (int j = i; j < qMin(i + batch, pkgs.size()); ++j)
            args << "arg[]=" + QString::fromUtf8(QUrl::toPercentEncoding(pkgs[j]));
        const QString url =
            "https://aur.archlinux.org/rpc/v5/info?" + args.join(u'&');

        QProcess curl;
        curl.start("curl", { "-sf", "--max-time", "8", url });
        if (!curl.waitForFinished(10000)) { curl.kill(); continue; }
        const QJsonArray results = QJsonDocument::fromJson(
            curl.readAllStandardOutput()).object().value("results").toArray();
        for (const QJsonValue &v : results) {
            const QJsonObject o = v.toObject();
            const QString name  = o.value("Name").toString();
            const QString maint = o.value("Maintainer").toString();
            if (!name.isEmpty() && !maint.isEmpty()) out.insert(name, maint);
        }
    }
    return out;
}

void ProgramsFeaturesPage::applyPublishers(QList<ProgramInfo> &programs)
{
    if (programs.isEmpty()) return;

    const QSet<QString> foreign = foreignPackages();
    QStringList aurPkgs;
    bool anyNative = false;
    for (const ProgramInfo &p : programs) {
        if (foreign.contains(p.name)) aurPkgs << p.name;
        else                          anyNative = true;
    }

    const QHash<QString, QString> maint = fetchAurMaintainers(aurPkgs);
    const QHash<QString, QString> devs  = anyNative ? readAppStreamDevelopers()
                                                    : QHash<QString, QString>();
    for (ProgramInfo &p : programs) {
        const QString better = foreign.contains(p.name) ? maint.value(p.name)
                                                        : devs.value(p.name);
        if (!better.isEmpty()) p.publisher = better;
    }
}

void ProgramsFeaturesPage::applyFriendlyNames(QList<ProgramInfo> &programs)
{
    for (ProgramInfo &p : programs)
        p.display = p.name;                   // default: the package name itself
    if (programs.isEmpty()) return;

    // One `pacman -Ql` over all packages lists the files each owns; we want the
    // .desktop entries under share/applications/.
    QStringList args{ "-Ql" };
    for (const ProgramInfo &p : programs) args << p.name;
    QProcess q;
    q.start("pacman", args);
    if (!q.waitForFinished(20000)) return;

    QHash<QString, QStringList> desktops;     // pkg -> its .desktop file paths
    const QString out = QString::fromUtf8(q.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &lv : lines) {
        const int sp = lv.indexOf(u' ');
        if (sp < 0) continue;
        const QString path = lv.mid(sp + 1).toString();
        if (path.endsWith(".desktop") && path.contains("/share/applications/"))
            desktops[lv.left(sp).toString()] << path;
    }

    for (ProgramInfo &p : programs) {
        const QStringList files = desktops.value(p.name);
        QString bestName, bestIcon;
        int bestRank = 99, bestLen = INT_MAX;
        for (const QString &file : files) {
            DesktopEntry e;
            if (!parseDesktopEntry(file, e)) continue;
            // Prefer the entry whose filename matches the package, then the
            // shortest name (e.g. "LibreOffice" over "LibreOffice Writer").
            const int rank = (QFileInfo(file).completeBaseName() == p.name) ? 0 : 1;
            const int len  = e.name.size();
            if (rank < bestRank || (rank == bestRank && len < bestLen)) {
                bestRank = rank; bestLen = len;
                bestName = e.name; bestIcon = e.icon;
            }
        }
        if (!bestName.isEmpty()) {
            p.display  = bestName;
            p.iconName = bestIcon;
        }
    }
}

QList<ProgramsFeaturesPage::ProgramInfo> ProgramsFeaturesPage::gatherPrograms()
{
    QList<ProgramInfo> result;

    // Force the C locale so the "Install Date" field is the predictable
    // asctime-style "Thu Apr 30 23:14:31 2026" rather than a localized string.
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    p.setProcessEnvironment(env);
    p.start("pacman", {"-Qei"});
    if (!p.waitForFinished(15000))
        return result;

    const QString out = QString::fromUtf8(p.readAllStandardOutput());

    // A package counts as "installed by the user" only if its first install was
    // NOT performed by the OS installer. That filters out everything the
    // installer laid down (the base system and the whole KDE desktop: dolphin,
    // ark, kate, etc) while keeping anything the user added later, even on the
    // very same day, since the test is how it was installed, not when. We still
    // drop anything that looks like a system component even when user-added.
    QDate earliestDay;
    bool sawInstaller = false;
    const QHash<QString, InstallRecord> hist = readInstallHistory(earliestDay, sawInstaller);
    auto userInstalled = [&](const QString &name) -> bool {
        const auto it = hist.constFind(name);
        if (it == hist.constEnd() || !it->date.isValid())
            return false;
        if (sawInstaller)
            return !it->byInstaller;
        // Fallback for a truncated log with no installer commands: treat
        // anything first installed on the earliest logged day as base OS.
        return earliestDay.isValid() && it->date > earliestDay;
    };

    // Parse the "Key   : value" blocks separated by blank lines.
    ProgramInfo cur;
    bool have = false;
    auto flush = [&]() {
        if (have && !cur.name.isEmpty()
            && userInstalled(cur.name) && !isSystemPackage(cur.name))
            result.append(cur);
        cur = ProgramInfo();
        have = false;
    };

    const QList<QStringView> lines = QStringView(out).split(u'\n');
    for (const QStringView &lineView : lines) {
        const QString line = lineView.toString();
        if (line.trimmed().isEmpty()) { flush(); continue; }

        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();

        if      (key == "Name")           { cur.name = val; have = true; }
        else if (key == "Version")        cur.version = val;
        else if (key == "Installed Size") cur.sizeBytes = parseSize(val);
        else if (key == "Packager") {
            // Fallback publisher only; applyPublishers() replaces it with the
            // real developer/maintainer wherever one is known.
            // "CachyOS <admin@cachyos.org>" -> "CachyOS"; drop the e-mail part.
            const int lt = val.indexOf('<');
            cur.publisher = (lt > 0 ? val.left(lt) : val).trimmed();
            if (cur.publisher.isEmpty() || cur.publisher == "Unknown Packager")
                cur.publisher = "Unknown";
        }
        else if (key == "Install Date") {
            // Collapse runs of spaces (the day is space-padded) before parsing.
            QString s = val.simplified();
            QDateTime dt = QLocale::c().toDateTime(s, "ddd MMM d hh:mm:ss yyyy");
            cur.installed = dt.date();
        }
    }
    flush();

    applyPublishers(result);      // packager -> real developer / AUR maintainer
    applyFriendlyNames(result);   // package name -> .desktop "Name=" where available
    return result;
}

// Sidebar entries
QList<SidebarLink> ProgramsFeaturesPage::sidebarLinks()
{
    return {
        Nav::to("View installed updates", PageId::InstalledUpdates),
        Nav::plain("Turn Linux features on or off"),
    };
}

QList<SidebarLink> ProgramsFeaturesPage::sidebarSeeAlso()
{
    return {};
}

// Page
ProgramsFeaturesPage::ProgramsFeaturesPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    // The Win7 warning-dialog sound, loaded once so it's ready to play instantly
    // when the uninstall/reinstall dialog opens.
    m_dialogSound.setSource(QUrl::fromLocalFile(
        "/usr/share/sounds/Windows 7/og/Windows Exclamation.wav"));
    m_dialogSound.setVolume(1.0f);

    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/0);
    // The list frame (command bar + tree + status strip) is full-bleed, so the
    // column itself carries no horizontal padding; only the header text below is
    // indented, via its own inset sub-layout.
    contentV->setContentsMargins(0, 18, 0, 0);

    // Title + instruction (the only indented block).
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    headerV->addWidget(Win7::pageTitle("Uninstall or change a program"));
    headerV->addSpacing(8);
    headerV->addWidget(Win7::label(
        "To uninstall a program, select it from the list and then click "
        "Uninstall, Change, or Repair."));
    contentV->addLayout(headerV);
    contentV->addSpacing(12);
    contentV->addWidget(Win7::hSeparator());

    // "Organize" command bar
    QHBoxLayout *toolH = nullptr;
    auto *toolBar = Win7::commandBar(&toolH);
    toolH->addWidget(Win7::dropdownLabel("Organize"));
    toolH->addStretch(1);
    Win7::addCommandBarIcons(toolH);
    contentV->addWidget(toolBar);

    // Programs tree
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({ "Name", "Publisher", "Installed On", "Size", "Version" });
    m_tree->setSortingEnabled(true);
    Win7::configureListTree(m_tree);
    for (int c = 0; c < 4; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 300);
    m_tree->setColumnWidth(1, 150);
    m_tree->setColumnWidth(2, 90);
    m_tree->setColumnWidth(3, 70);
    // Double-clicking a program opens the Uninstall/Reinstall dialog.
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &ProgramsFeaturesPage::onProgramActivated);
    contentV->addWidget(m_tree, 1);

    // Status bar: total size + program count
    QHBoxLayout *statusH = nullptr;
    auto *statusBar = Win7::statusPanel(52, &statusH);

    auto *statusIcon = new QLabel;
    statusIcon->setPixmap(themeIcon({"application-vnd.debian.binary-package",
                                     "preferences-system"}).pixmap(42, 42));
    statusH->addWidget(statusIcon);

    auto *statusText = new QVBoxLayout;
    statusText->setContentsMargins(0, 0, 0, 0);
    statusText->setSpacing(0);

    auto *line1 = new QHBoxLayout;
    line1->setContentsMargins(0, 0, 0, 0);
    line1->setSpacing(40);
    m_totalLbl = Win7::label(QString(), 9, "#1F1F1F");
    line1->addWidget(Win7::label("Currently installed programs", 9, "#1F1F1F"));
    line1->addWidget(m_totalLbl);
    line1->addStretch(1);
    statusText->addLayout(line1);

    m_countLbl = Win7::label("0 programs installed", 9, "#555555");
    statusText->addWidget(m_countLbl);

    statusH->addLayout(statusText);
    statusH->addStretch(1);
    contentV->addWidget(statusBar);

    startLoad();
}

void ProgramsFeaturesPage::startLoad()
{
    // Navigate first, gather second: the `pacman -Qei` query can take a moment,
    // so run it on a worker thread and populate when it lands. The page is
    // already on screen with the "Searching..." placeholder by then. This is
    // also called to refresh the list after a package is uninstalled.
    m_tree->clear();
    Win7::addTreePlaceholder(m_tree, "Searching for installed programs...");
    m_countLbl->setText("0 programs installed");
    m_totalLbl->clear();
    m_tree->setCursor(Qt::BusyCursor);

    Win7::runAsync(this, &ProgramsFeaturesPage::gatherPrograms,
                   [this](const QList<ProgramInfo> &programs) {
        populate(programs);
    });
}

void ProgramsFeaturesPage::populate(const QList<ProgramInfo> &programs)
{
    m_tree->setCursor(Qt::ArrowCursor);
    m_tree->clear();   // drop the "Searching..." placeholder

    qint64 totalBytes = 0;
    for (const ProgramInfo &p : programs) {
        auto *item = new ProgramItem(m_tree);
        // Prefer the icon named in the program's .desktop file (an absolute path
        // or a theme name), then a theme icon matching the package name, finally
        // a generic package icon.
        QIcon icon;
        if (!p.iconName.isEmpty())
            icon = p.iconName.startsWith('/') ? QIcon(p.iconName)
                                              : QIcon::fromTheme(p.iconName);
        if (icon.isNull()) icon = QIcon::fromTheme(p.name);
        if (icon.isNull()) icon = themeIcon({"exec", "application-x-executable",
                                             "gnome-fs-executable"});
        item->setIcon(0, icon);
        item->setText(0, p.display);
        item->setData(0, Qt::UserRole, p.name);   // package name for the action dialog
        item->setText(1, p.publisher);
        item->setText(2, p.installed.isValid()
                              ? p.installed.toString("yyyy-MM-dd") : QString());
        item->setText(3, humanSize(p.sizeBytes));
        item->setText(4, p.version);
        item->setData(3, SizeSortRole, static_cast<qlonglong>(p.sizeBytes));
        Win7::setItemPointSize(item);
        totalBytes += p.sizeBytes;
    }
    m_tree->sortByColumn(0, Qt::AscendingOrder);

    m_totalLbl->setText("Total size:  " + humanSize(totalBytes));
    m_countLbl->setText(QString("%1 program%2 installed")
                            .arg(programs.size())
                            .arg(programs.size() == 1 ? "" : "s"));
}

// Uninstall / Reinstall
void ProgramsFeaturesPage::onProgramActivated(QTreeWidgetItem *item, int /*column*/)
{
    if (!item) return;
    const QString pkg = item->data(0, Qt::UserRole).toString();
    if (pkg.isEmpty()) return;                 // the "Searching..." placeholder row
    const QString display = item->text(0);

    // A Win7-style confirmation: a white message area (standard warning icon +
    // single-line question) above a slightly darker footer panel holding the
    // Uninstall / Reinstall / Cancel buttons at the bottom-right.
    QDialog dlg(this);
    dlg.setWindowTitle("Programs and Features");
    dlg.setModal(true);
    dlg.setStyleSheet("QDialog { background: #FFFFFF; }");

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // Lock the dialog to its content's size hint: fixed, non-resizable.
    outer->setSizeConstraint(QLayout::SetFixedSize);

    // Message area (white)
    auto *body = new QWidget;
    body->setStyleSheet("background: #FFFFFF;");
    auto *bodyH = new QHBoxLayout(body);
    bodyH->setContentsMargins(18, 12, 28, 12);
    bodyH->setSpacing(14);

    auto *iconLbl = new QLabel;
    iconLbl->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32));
    iconLbl->setFixedSize(32, 32);
    bodyH->addWidget(iconLbl, 0, Qt::AlignVCenter);

    auto *msg = new QLabel(
        QString("Do you want to uninstall or repair %1?").arg(display));
    {
        QFont f = msg->font();
        f.setPointSize(9);
        msg->setFont(f);
    }
    msg->setStyleSheet("color: #000000; background: transparent;");
    msg->setMinimumWidth(360);
    bodyH->addWidget(msg, 1, Qt::AlignVCenter);
    outer->addWidget(body);

    // Footer panel (slightly darker, like the Win7 command area)
    auto *footer = new QFrame;
    footer->setObjectName("dlgFooter");
    footer->setStyleSheet(
        "#dlgFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");
    auto *footH = new QHBoxLayout(footer);
    footH->setContentsMargins(12, 8, 12, 8);
    footH->setSpacing(8);
    footH->addStretch(1);
    auto *uninstallBtn = new QPushButton("Uninstall");
    auto *reinstallBtn = new QPushButton("Repair");
    auto *cancelBtn    = new QPushButton("Cancel");
    for (QPushButton *b : { uninstallBtn, reinstallBtn, cancelBtn }) {
        b->setMinimumWidth(75);
        b->setAutoDefault(false);   // don't let the first button claim the default
        footH->addWidget(b);
    }
    // Cancel is both the default (Enter) and the initially-focused button, so
    // neither the keyboard nor the focus highlight lands on the destructive
    // Uninstall action.
    cancelBtn->setDefault(true);
    cancelBtn->setFocus();
    outer->addWidget(footer);

    int choice = 0;   // 0 = cancel, 1 = uninstall, 2 = reinstall
    connect(uninstallBtn, &QPushButton::clicked, &dlg, [&]{ choice = 1; dlg.accept(); });
    connect(reinstallBtn, &QPushButton::clicked, &dlg, [&]{ choice = 2; dlg.accept(); });
    connect(cancelBtn,    &QPushButton::clicked, &dlg, &QDialog::reject);

    m_dialogSound.play();   // Win7 exclamation chime as the dialog appears
    dlg.exec();
    if      (choice == 1) runPackageAction(pkg, display, /*reinstall=*/false);
    else if (choice == 2) runPackageAction(pkg, display, /*reinstall=*/true);
}

void ProgramsFeaturesPage::runPackageAction(const QString &pkg, const QString &display,
                                            bool reinstall)
{
    // Privilege model mirrors the Linux Update page: pacman runs under pkexec;
    // AUR/foreign packages reinstall through yay (which calls pkexec itself).
    // The package name is a real installed-package token passed as a separate
    // argv entry, so there is no shell-injection surface.
    QString program;
    QStringList args;
    if (reinstall) {
        QProcess chk;                          // foreign (AUR) packages: pacman -Qm
        chk.start("pacman", { "-Qm", pkg });
        chk.waitForFinished(4000);
        const bool foreign =
            (chk.exitStatus() == QProcess::NormalExit && chk.exitCode() == 0);
        const QString yay = QStandardPaths::findExecutable("yay");
        if (foreign && !yay.isEmpty()) {
            program = "yay";
            args = { "-S", "--noconfirm", "--sudo", "pkexec", "--cleanafter", pkg };
        } else {
            program = "pkexec";
            args = { "pacman", "-S", "--noconfirm", pkg };
        }
    } else {
        program = "pkexec";
        args = { "pacman", "-R", "--noconfirm", pkg };
    }

    auto *progress = new QProgressDialog(
        QString("%1 %2…").arg(reinstall ? "Repairing" : "Uninstalling", display),
        QString(), 0, 0, this);               // empty cancel label -> busy, no cancel
    progress->setWindowTitle(reinstall ? "Repair" : "Uninstall");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, progress, reinstall, display](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(proc->readAll()).trimmed();
        proc->deleteLater();
        progress->close();
        progress->deleteLater();

        if (status == QProcess::NormalExit && code == 0) {
            QMessageBox::information(this,
                reinstall ? "Repair complete" : "Uninstall complete",
                QString("%1 was %2.")
                    .arg(display, reinstall ? "reinstalled" : "uninstalled"));
            if (!reinstall) startLoad();       // it's gone now, refresh the list
        } else {
            const QString tail = out.length() > 1500 ? "…" + out.right(1500) : out;
            QMessageBox::warning(this,
                reinstall ? "Repair failed" : "Uninstall failed",
                tail.isEmpty() ? QString("The operation did not complete.")
                               : QString("The operation did not complete:\n\n%1").arg(tail));
        }
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, progress](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        progress->close();
        progress->deleteLater();
        proc->deleteLater();
        QMessageBox::warning(this, "Action failed",
            "Could not launch the package manager.");
    });

    proc->start(program, args);
    progress->show();
}
