#include "ui/control/pages/ProgramsFeaturesPage.h"
#include "DetailsPane.h"
#include "MessageBoxes.h"
#include "ui/control/IconHelper.h"
#include "ui/control/Win7Ui.h"
#include "aero/sounds.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDialog>
#include <QPushButton>
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
#include "aero/palette.h"
#include "aero/artwork.h"

namespace ControlPanel {

namespace {

// The details pane's icon for this page
QIcon programsIcon()
{
    return Aero::themeIcon({"application-vnd.debian.binary-package",
                            "preferences-system"});
}

} // namespace

// Data gathering
// "6.32 MiB" / "121.00 KiB" / "1.04 GiB" to bytes
static qint64 parseSize(const QString &text)
{
    const QStringList parts = text.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return 0;
    const double value = QLocale::c().toDouble(parts[0]);
    const QString unit = parts[1];
    double mult = 1.0;
    if      (unit.startsWith("Ki"))
        mult = 1024.0;
    else if (unit.startsWith("Mi"))
        mult = 1024.0 * 1024.0;
    else if (unit.startsWith("Gi"))
        mult = 1024.0 * 1024.0 * 1024.0;
    else if (unit.startsWith("Ti"))
        mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    return static_cast<qint64>(value * mult);
}

// Bytes to "5.63 MB" / "2.70 GB" / "812 KB", mirroring the column in the
// reference screenshot (which lists everything in MB, totals in GB)
static QString humanSize(qint64 bytes)
{
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QString::number(kb, 'f', 0) + " KB";
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        // Fewer decimals as the number grows, so it stays about three digits
        int decimals = 0;
        if (mb < 10.0)
            decimals = 2;
        else if (mb < 100.0)
            decimals = 1;
        return QString::number(mb, 'f', decimals) + " MB";
    }
    const double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}

// The Size column must sort by the real byte count (stored in SizeSortRole),
// not by the human-readable "5.63 MB" text. All other columns sort lexically,
// which is correct for names, publishers, versions and ISO-formatted dates
namespace {
constexpr int SizeSortRole = Qt::UserRole + 1;

class ProgramItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override {
        int col;
        if (treeWidget())
            col = treeWidget()->sortColumn();
        else
            col = 0;
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
// installer rather than by the user later on
struct InstallRecord { QDate date; bool byInstaller = false; };

// Does this pacman command line look like one the OS installer (Calamares) ran,
// rather than an interactive `pacman -S` or an AUR helper? The installer's
// chroot bootstrap and post-install steps carry tell-tale flags that ordinary
// installs never use: a Calamares-root or explicit "-r /" target, --cachedir,
// or the --noprogressbar / --disable-download-timeout pair. We deliberately do
// NOT key on --config or --noconfirm, since yay/paru's `pacman -U` calls use
// those too
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
// the very first install line, used as a fallback signal in that case
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
        const QString logLine = line.toString();

        const auto rm = runRe.match(logLine);
        // A new transaction's command
        if (rm.hasMatch()) {
            curInstaller = isInstallerCommand(rm.captured(1));
            if (curInstaller)
                sawInstaller = true;
            continue;
        }

        const auto im = instRe.match(logLine);
        if (!im.hasMatch())
            continue;
        const QString pkg = im.captured(2);
        // Log is chronological: keep first
        if (hist.contains(pkg))
            continue;
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
// package managers / AUR helpers the system is run with (pacman, yay, etc)
static bool isSystemPackage(const QString &name)
{
    static const QRegularExpression sysRe(
        "^("
        // Base meta packages
        "base|base-devel"
        // Stock kernels
        "|linux|linux-lts|linux-zen|linux-hardened"
        // Cachyos kernels
        "|linux-cachyos.*"
        // Kernel/dev headers
        "|.*-headers"
        // *-firmware
        "|.*firmware"
        // Intel/amd microcode
        "|.*-ucode"
        // Distro config & meta
        "|cachyos-.*"
        "|grub|grub-.*|refind|systemd-boot|efibootmgr|efitools|os-prober"
        // Initramfs / splash
        "|mkinitcpio.*|dracut|booster|plymouth.*"
        // Hardware drivers
        "|xf86-video-.*|xf86-input-.*|.*-dkms"
        "|nvidia.*|mesa|lib32-.*mesa.*|egl-wayland|vulkan-.*-driver"
        // Package management
        "|pacman.*|pamac.*|libpamac.*|archlinux-keyring"
        // AUR helpers
        "|yay|yay-bin|paru|paru-bin|pikaur|trizen|aurman|pacaur|aura|octopi"
        ")$",
        QRegularExpression::CaseInsensitiveOption);
    return sysRe.match(name).hasMatch();
}

// The friendly name and icon read from a .desktop file's [Desktop Entry]
struct DesktopEntry { QString name; QString icon; };

// Parse the [Desktop Entry] group of a .desktop file. Returns false unless it
// is a displayable application with a name (skipping NoDisplay/Hidden helper
// entries such as VS Code's URL handler). Only the main "Name="/"Icon=" keys
// are read; localized "Name[xx]=" keys and per-action groups are ignored
static bool parseDesktopEntry(const QString &path, DesktopEntry &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    bool inEntry = false, noDisplay = false, hidden = false;
    QString name, icon, type;
    const QString text = QString::fromUtf8(file.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (const QStringView &lv : lines) {
        const QString line = lv.trimmed().toString();
        // A new group begins
        if (line.startsWith('[')) {
            inEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq);
        const QString val = line.mid(eq + 1);
        if      (key == "Name"      && name.isEmpty())
            name = val;
        else if (key == "Icon"      && icon.isEmpty())
            icon = val;
        else if (key == "Type")
            type      = val;
        else if (key == "NoDisplay")
            noDisplay = (val.compare("true", Qt::CaseInsensitive) == 0);
        else if (key == "Hidden")
            hidden    = (val.compare("true", Qt::CaseInsensitive) == 0);
    }

    if (name.isEmpty() || noDisplay || hidden)
        return false;
    if (!type.isEmpty() && type != "Application")
        return false;
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
//    (e.g. linux-devmgmt -> "actuallyaridan")

// The names of all foreign (not-in-any-repo, i.e. AUR/local) packages
static QSet<QString> foreignPackages()
{
    QSet<QString> set;
    QProcess process;
    process.start("pacman", { "-Qmq" });
    if (!process.waitForFinished(10000))
        return set;
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &l : lines)
        set.insert(l.trimmed().toString());
    return set;
}

// Scan one AppStream catalog/metainfo XML document, adding pkgname -> developer
// entries. Handles both the current <developer><name> form and the legacy
// <developer_name> tag, ignoring translated (xml:lang) variants. Existing
// entries win, so the first component naming a package sticks
static void parseAppStreamXml(const QByteArray &xml, QHash<QString, QString> &devs)
{
    QXmlStreamReader reader(xml);
    QStringList pkgs;
    QString devName;
    QString legacyName;
    bool inDeveloper = false;
    while (!reader.atEnd()) {
        const auto tok = reader.readNext();
        if (tok == QXmlStreamReader::EndElement) {
            if (reader.name() == u"developer") {
                inDeveloper = false;
            } else if (reader.name() == u"component") {
                QString dev;
                if (!devName.isEmpty())
                    dev = devName;
                else
                    dev = legacyName;
                if (!dev.isEmpty()) {
                    for (const QString &p : pkgs) {
                        if (!devs.contains(p))
                            devs.insert(p, dev);
                    }
                }
            }
            continue;
        }
        if (tok != QXmlStreamReader::StartElement)
            continue;

        const auto tag = reader.name();
        const bool translated = reader.attributes().hasAttribute("xml:lang");
        if (tag == u"component") {
            pkgs.clear();
            devName.clear();
            legacyName.clear();
            inDeveloper = false;
        }
        else if (tag == u"pkgname") {
            const QString pkg =
                reader.readElementText(QXmlStreamReader::IncludeChildElements);
            pkgs << pkg.trimmed();
        }
        else if (tag == u"developer") {
            inDeveloper = true;
        }
        else if (tag == u"name" && inDeveloper) {
            const QString name =
                reader.readElementText(QXmlStreamReader::IncludeChildElements);
            if (!translated && devName.isEmpty())
                devName = name.simplified();
        }
        else if (tag == u"developer_name") {
            const QString name =
                reader.readElementText(QXmlStreamReader::IncludeChildElements);
            if (!translated && legacyName.isEmpty())
                legacyName = name.simplified();
        }
    }
}

// pkgname -> upstream developer, read from the distro's AppStream catalogs
// (the compressed XML shipped by archlinux-appstream-data). Covers every repo
// desktop application; CLI-only packages have no AppStream component and keep
// the fallback. CachyOS rebuilds of Arch packages keep the same pkgname, so
// they match too
static QHash<QString, QString> readAppStreamDevelopers()
{
    QHash<QString, QString> devs;
    static const QStringList dirs = {
        "/usr/share/swcatalog/xml", "/var/lib/swcatalog/xml",
        // Pre-0.16 paths
        "/usr/share/app-info/xml",  "/var/lib/app-info/xml",
    };
    for (const QString &dir : dirs) {
        const QFileInfoList files = QDir(dir).entryInfoList(
            { "*.xml", "*.xml.gz" }, QDir::Files);
        for (const QFileInfo &fi : files) {
            QByteArray xml;
            if (fi.fileName().endsWith(".gz")) {
                QProcess gz;
                gz.start("gzip", { "-dc", fi.absoluteFilePath() });
                if (!gz.waitForFinished(15000)) {
                    gz.kill();
                    continue;
                }
                xml = gz.readAllStandardOutput();
            } else {
                QFile file(fi.absoluteFilePath());
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                xml = file.readAll();
            }
            parseAppStreamXml(xml, devs);
        }
    }
    return devs;
}

// pkgname -> AUR maintainer, via batched requests to the AUR info RPC. Shells
// out to curl (pacman itself depends on it) with a short timeout, so an
// offline machine just keeps the fallback text. Orphaned packages report a
// null maintainer and also keep the fallback
static QHash<QString, QString> fetchAurMaintainers(const QStringList &pkgs)
{
    QHash<QString, QString> out;
    // Keep the GET URL a sane length
    constexpr int batch = 150;
    for (int i = 0; i < pkgs.size(); i += batch) {
        QStringList args;
        for (int j = i; j < qMin(i + batch, pkgs.size()); ++j)
            args << "arg[]=" + QString::fromUtf8(QUrl::toPercentEncoding(pkgs[j]));
        const QString url =
            "https://aur.archlinux.org/rpc/v5/info?" + args.join(u'&');

        QProcess curl;
        curl.start("curl", { "-sf", "--max-time", "8", url });
        if (!curl.waitForFinished(10000)) {
            curl.kill();
            continue;
        }
        const QJsonArray results = QJsonDocument::fromJson(
            curl.readAllStandardOutput()).object().value("results").toArray();
        for (const QJsonValue &value : results) {
            const QJsonObject entry = value.toObject();
            const QString name  = entry.value("Name").toString();
            const QString maint = entry.value("Maintainer").toString();
            if (!name.isEmpty() && !maint.isEmpty())
                out.insert(name, maint);
        }
    }
    return out;
}

void ProgramsFeaturesPage::applyPublishers(QList<ProgramInfo> &programs)
{
    if (programs.isEmpty())
        return;

    const QSet<QString> foreign = foreignPackages();
    QStringList aurPkgs;
    bool anyNative = false;
    for (const ProgramInfo &p : programs) {
        if (foreign.contains(p.name))
            aurPkgs << p.name;
        else
            anyNative = true;
    }

    const QHash<QString, QString> maint = fetchAurMaintainers(aurPkgs);
    // AppStream only knows the repositories' programs, so it is read only when
    // there are some
    QHash<QString, QString> devs;
    if (anyNative)
        devs = readAppStreamDevelopers();
    for (ProgramInfo &p : programs) {
        QString better;
        if (foreign.contains(p.name))
            better = maint.value(p.name);
        else
            better = devs.value(p.name);
        if (!better.isEmpty())
            p.publisher = better;
    }
}

void ProgramsFeaturesPage::applyFriendlyNames(QList<ProgramInfo> &programs)
{
    for (ProgramInfo &p : programs)
        // Default: the package name itself
        p.display = p.name;
    if (programs.isEmpty())
        return;

    // One `pacman -Ql` over all packages lists the files each owns; we want the
    // .desktop entries under share/applications/.
    QStringList args{ "-Ql" };
    for (const ProgramInfo &p : programs)
        args << p.name;
    QProcess process;
    process.start("pacman", args);
    if (!process.waitForFinished(20000))
        return;

    // Pkg -> its .desktop file paths
    QHash<QString, QStringList> desktops;
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &lv : lines) {
        const int sp = lv.indexOf(u' ');
        if (sp < 0)
            continue;
        const QString path = lv.mid(sp + 1).toString();
        if (path.endsWith(".desktop") && path.contains("/share/applications/"))
            desktops[lv.left(sp).toString()] << path;
    }

    for (ProgramInfo &p : programs) {
        const QStringList files = desktops.value(p.name);
        QString bestName;
        QString bestIcon;
        int bestRank = 99;
        int bestLen = INT_MAX;
        for (const QString &file : files) {
            DesktopEntry candidate;
            if (!parseDesktopEntry(file, candidate))
                continue;
            // Prefer the entry whose filename matches the package, then the
            // shortest name (e.g. "LibreOffice" over "LibreOffice Writer")
            int rank;
            if (QFileInfo(file).completeBaseName() == p.name)
                rank = 0;
            else
                rank = 1;
            const int len  = candidate.name.size();
            if (rank < bestRank || (rank == bestRank && len < bestLen)) {
                bestRank = rank;
                bestLen = len;
                bestName = candidate.name;
                bestIcon = candidate.icon;
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
    // asctime-style "Thu Apr 30 23:14:31 2026" rather than a localized string
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    process.start("pacman", {"-Qei"});
    if (!process.waitForFinished(15000))
        return result;

    const QString out = QString::fromUtf8(process.readAllStandardOutput());

    // A package counts as "installed by the user" only if its first install was
    // NOT performed by the OS installer. That filters out everything the
    // installer laid down (the base system and the whole KDE desktop: dolphin,
    // ark, kate, etc) while keeping anything the user added later, even on the
    // very same day, since the test is how it was installed, not when. We still
    // drop anything that looks like a system component even when user-added
    QDate earliestDay;
    bool sawInstaller = false;
    const QHash<QString, InstallRecord> hist =
        readInstallHistory(earliestDay, sawInstaller);
    auto userInstalled = [&](const QString &name) -> bool {
        const auto it = hist.constFind(name);
        if (it == hist.constEnd() || !it->date.isValid())
            return false;
        if (sawInstaller)
            return !it->byInstaller;
        // Fallback for a truncated log with no installer commands: treat
        // anything first installed on the earliest logged day as base OS
        return earliestDay.isValid() && it->date > earliestDay;
    };

    // Parse the "Key   : value" blocks separated by blank lines
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
        if (line.trimmed().isEmpty()) {
            flush();
            continue;
        }

        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();

        if (key == "Name") {
            cur.name = val;
            have = true;
        }
        else if (key == "Version")
            cur.version = val;
        else if (key == "Installed Size")
            cur.sizeBytes = parseSize(val);
        else if (key == "Packager") {
            // Fallback publisher only; applyPublishers() replaces it with the
            // real developer/maintainer wherever one is known.
            // "CachyOS <admin@cachyos.org>" -> "CachyOS"; drop the e-mail part
            const int lt = val.indexOf('<');
            if (lt > 0)
                cur.publisher = val.left(lt).trimmed();
            else
                cur.publisher = val.trimmed();
            if (cur.publisher.isEmpty() || cur.publisher == "Unknown Packager")
                cur.publisher = "Unknown";
        }
        else if (key == "Install Date") {
            // Collapse runs of spaces (the day is space-padded) before parsing
            const QString date = val.simplified();
            QDateTime dt = QLocale::c().toDateTime(date, "ddd MMM d hh:mm:ss yyyy");
            cur.installed = dt.date();
        }
    }
    flush();

    // Packager -> real developer / AUR maintainer
    applyPublishers(result);
    // Package name -> .desktop "Name=" where available
    applyFriendlyNames(result);
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
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/0);
    // The list frame (command bar + tree + status strip) is full-bleed, so the
    // column itself carries no horizontal padding; only the header text below is
    // indented, via its own inset sub-layout
    contentV->setContentsMargins(0, 18, 0, 0);

    // Title + instruction (the only indented block)
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    headerV->addWidget(Win7::pageTitle("Uninstall or change a program"));
    headerV->addSpacing(8);
    headerV->addWidget(Aero::label(
        "To uninstall a program, select it from the list and then click "
        "Uninstall, Change, or Repair."));
    contentV->addLayout(headerV);
    contentV->addSpacing(12);
    contentV->addWidget(Win7::hSeparator());

    // "Organize" command bar
    QHBoxLayout *toolH = nullptr;
    auto *toolBar = Win7::commandBar(&toolH);
    toolH->addWidget(new Aero::MenuButton("Organize"));
    toolH->addStretch(1);
    Win7::addCommandBarIcons(toolH);
    contentV->addWidget(toolBar);

    // Programs tree
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({ "Name", "Publisher", "Installed On", "Size", "Version" });
    Win7::configureListTree(m_tree);
    m_tree->setSortingEnabled(true);
    for (int c = 0; c < 4; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 300);
    m_tree->setColumnWidth(1, 150);
    m_tree->setColumnWidth(2, 90);
    m_tree->setColumnWidth(3, 70);
    // Double-clicking a program opens the Uninstall/Reinstall dialog
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &ProgramsFeaturesPage::onProgramActivated);
    contentV->addWidget(m_tree, 1);

    // Explorer's details pane, which Win7 also puts under this list, filled
    // with the total size and program count once they are known
    m_details = new DetailsPane;
    contentV->addWidget(m_details);

    startLoad();
}

void ProgramsFeaturesPage::startLoad()
{
    // Navigate first, gather second: the `pacman -Qei` query can take a moment,
    // so run it on a worker thread and populate when it lands. The page is
    // already on screen with the "Searching..." placeholder by then. This is
    // also called to refresh the list after a package is uninstalled
    m_tree->clear();
    Win7::addTreePlaceholder(m_tree, "Searching for installed programs...");
    m_details->showContent(programsIcon(), "Currently installed programs",
                           "0 programs installed");
    m_tree->setCursor(Qt::BusyCursor);

    Win7::runAsync(this, &ProgramsFeaturesPage::gatherPrograms,
                   [this](const QList<ProgramInfo> &programs) {
        populate(programs);
    });
}

void ProgramsFeaturesPage::populate(const QList<ProgramInfo> &programs)
{
    m_tree->setCursor(Qt::ArrowCursor);
    // Drop the "Searching..." placeholder
    m_tree->clear();

    qint64 totalBytes = 0;
    for (const ProgramInfo &p : programs) {
        auto *item = new ProgramItem(m_tree);
        // Prefer the icon named in the program's .desktop file (an absolute path
        // or a theme name), then a theme icon matching the package name, finally
        // a generic package icon
        QIcon icon;
        // An absolute path is a file of its own, anything else a theme name
        if (p.iconName.startsWith('/'))
            icon = QIcon(p.iconName);
        else if (!p.iconName.isEmpty())
            icon = QIcon::fromTheme(p.iconName);
        if (icon.isNull())
            icon = QIcon::fromTheme(p.name);
        if (icon.isNull()) {
            icon = Aero::themeIcon({"exec", "application-x-executable",
                                    "gnome-fs-executable"});
        }
        item->setIcon(0, icon);
        item->setText(0, p.display);
        // Package name for the action dialog
        item->setData(0, Qt::UserRole, p.name);
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

    const QString primary = QString("Currently installed programs    Total size:  %1")
                                .arg(humanSize(totalBytes));
    QString count = QString("%1 programs installed").arg(programs.size());
    if (programs.size() == 1)
        count = QStringLiteral("1 program installed");
    m_details->showContent(programsIcon(), primary, count);
}

// Uninstall / Reinstall
void ProgramsFeaturesPage::onProgramActivated(QTreeWidgetItem *item, int /*column*/)
{
    if (!item)
        return;
    const QString pkg = item->data(0, Qt::UserRole).toString();
    // The "Searching..." placeholder row
    if (pkg.isEmpty())
        return;
    const QString display = item->text(0);

    // A Win7-style confirmation: a white message area (standard warning icon +
    // single-line question) above a slightly darker footer panel holding the
    // Uninstall / Reinstall / Cancel buttons at the bottom-right
    QDialog dlg(this);
    dlg.setWindowTitle("Programs and Features");
    dlg.setModal(true);
    dlg.setStyleSheet(QStringLiteral("QDialog { background: %1; }")
        .arg(QLatin1String(Aero::Palette::Surface)));

    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // Lock the dialog to its content's size hint: fixed, non-resizable
    outer->setSizeConstraint(QLayout::SetFixedSize);

    // Message area (white)
    auto *body = new QWidget;
    body->setStyleSheet(QStringLiteral("background: %1;")
        .arg(QLatin1String(Aero::Palette::Surface)));
    auto *bodyH = new QHBoxLayout(body);
    bodyH->setContentsMargins(18, 12, 28, 12);
    bodyH->setSpacing(14);

    auto *iconLbl = new QLabel;
    const QIcon warning = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    iconLbl->setPixmap(warning.pixmap(32, 32));
    iconLbl->setFixedSize(32, 32);
    bodyH->addWidget(iconLbl, 0, Qt::AlignVCenter);

    auto *msg = new QLabel(
        QString("Do you want to uninstall or repair %1?").arg(display));
    Aero::setPointSize(msg, 9);
    msg->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(QLatin1String(Aero::Palette::Text)));
    msg->setMinimumWidth(360);
    bodyH->addWidget(msg, 1, Qt::AlignVCenter);
    outer->addWidget(body);

    // Footer panel (slightly darker, like the Win7 command area)
    auto *footer = new QFrame;
    footer->setObjectName("dlgFooter");
    footer->setStyleSheet(
        Aero::panelSheet(QStringLiteral("dlgFooter"),
                         Aero::Palette::FooterSurface,
                         Qt::TopEdge,
                         Aero::Palette::FooterRule));
    auto *footH = new QHBoxLayout(footer);
    footH->setContentsMargins(12, 8, 12, 8);
    footH->setSpacing(8);
    footH->addStretch(1);
    auto *uninstallBtn = new QPushButton("Uninstall");
    auto *reinstallBtn = new QPushButton("Repair");
    auto *cancelBtn    = new QPushButton("Cancel");
    for (QPushButton *b : { uninstallBtn, reinstallBtn, cancelBtn }) {
        b->setMinimumWidth(75);
        // Don't let the first button claim the default
        b->setAutoDefault(false);
        footH->addWidget(b);
    }
    // Cancel is both the default (Enter) and the initially-focused button, so
    // neither the keyboard nor the focus highlight lands on the destructive
    // Uninstall action
    cancelBtn->setDefault(true);
    cancelBtn->setFocus();
    outer->addWidget(footer);

    // 0 = cancel, 1 = uninstall, 2 = reinstall
    int choice = 0;
    connect(uninstallBtn, &QPushButton::clicked, &dlg, [&]{ choice = 1; dlg.accept(); });
    connect(reinstallBtn, &QPushButton::clicked, &dlg, [&]{ choice = 2; dlg.accept(); });
    connect(cancelBtn,    &QPushButton::clicked, &dlg, &QDialog::reject);

    Aero::playSound(QStringLiteral("Windows Exclamation"));
    dlg.exec();
    if      (choice == 1)
        runPackageAction(pkg, display, /*reinstall=*/false);
    else if (choice == 2)
        runPackageAction(pkg, display, /*reinstall=*/true);
}

void ProgramsFeaturesPage::runPackageAction(const QString &pkg, const QString &display,
                                            bool reinstall)
{
    // Privilege model mirrors the Linux Update page: pacman runs under pkexec;
    // AUR/foreign packages reinstall through yay (which calls pkexec itself).
    // The package name is a real installed-package token passed as a separate
    // argv entry, so there is no shell-injection surface
    QString program;
    QStringList args;
    if (reinstall) {
        // Foreign (AUR) packages: pacman -Qm
        QProcess chk;
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

    // Repairing and uninstalling differ only in their wording from here on
    QString action = QStringLiteral("Uninstall");
    QString ongoing = QStringLiteral("Uninstalling");
    QString pastTense = QStringLiteral("uninstalled");
    if (reinstall) {
        action = QStringLiteral("Repair");
        ongoing = QStringLiteral("Repairing");
        pastTense = QStringLiteral("reinstalled");
    }

    auto *progress = new QProgressDialog(
        QString("%1 %2…").arg(ongoing, display),
        // Empty cancel label -> busy, no cancel
        QString(), 0, 0, this);
    progress->setWindowTitle(action);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, progress, reinstall, display, action, pastTense]
            (int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(proc->readAll()).trimmed();
        proc->deleteLater();
        progress->close();
        progress->deleteLater();

        if (status == QProcess::NormalExit && code == 0) {
            MessageBoxes::showInformation(this, action + " complete",
                QString("%1 was %2.").arg(display, pastTense));
            // It's gone now, refresh the list
            if (!reinstall)
                startLoad();
        } else {
            QString tail;
            if (out.length() > 1500)
                tail = "…" + out.right(1500);
            else
                tail = out;
            MessageBoxes::showError(this, action + " failed",
                QString("The operation did not complete."), tail);
        }
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, progress](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        progress->close();
        progress->deleteLater();
        proc->deleteLater();
        MessageBoxes::showError(this, "Action failed",
            "Could not launch the package manager.");
    });

    proc->start(program, args);
    progress->show();
}

} // namespace ControlPanel
