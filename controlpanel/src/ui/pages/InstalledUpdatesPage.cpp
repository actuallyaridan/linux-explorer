#include "InstalledUpdatesPage.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QProcess>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDate>
#include <QColor>
#include <QRegularExpression>
#include <QPushButton>
#include <algorithm>

// Data gathering
QHash<QString, QString> InstalledUpdatesPage::repositoryMap()
{
    // `pacman -Sl` prints "<repo> <pkg> <version> [installed]" for every package
    // in the configured sync databases. We only need pkg -> repo.
    QHash<QString, QString> map;
    QProcess p;
    p.start("pacman", {"-Sl"});
    if (!p.waitForFinished(8000))
        return map;

    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const int firstSp = line.indexOf(u' ');
        if (firstSp < 0) continue;
        const int secondSp = line.indexOf(u' ', firstSp + 1);
        if (secondSp < 0) continue;
        const QString repo = line.left(firstSp).toString();
        const QString pkg  = line.mid(firstSp + 1, secondSp - firstSp - 1).toString();
        map.insert(pkg, repo);
    }
    return map;
}

// Vendor-style label for the Publisher column, derived from the repository.
static QString publisherForRepo(const QString &repo)
{
    if (repo.startsWith("cachyos")) return "CachyOS";
    if (repo == "AUR")             return "Arch User Repository";
    if (repo == "core" || repo == "extra" || repo == "multilib"
        || repo == "testing" || repo == "extra-testing"
        || repo == "multilib-testing" || repo == "core-testing"
        || repo == "community")
        return "Arch Linux";
    return "Linux";
}

QList<InstalledUpdatesPage::UpdateInfo> InstalledUpdatesPage::gatherUpdates()
{
    QList<UpdateInfo> result;

    QFile log("/var/log/pacman.log");
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QHash<QString, QString> repos = repositoryMap();

    // [2026-06-11T19:30:23+0200] [ALPM] upgraded pcsclite (2.5.0-1.1 -> 2.5.1-1.1)
    static const QRegularExpression re(
        R"(^\[([0-9-]{10})T[0-9:+\-]+\] \[ALPM\] upgraded (\S+) \(\S+ -> (\S+)\))");

    // Latest upgrade per package. The log is chronological, so a later line for
    // the same package overwrites the earlier one; we track insertion-time data.
    QHash<QString, UpdateInfo> latest;
    const QString text = QString::fromUtf8(log.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const auto m = re.match(line.toString());
        if (!m.hasMatch()) continue;

        UpdateInfo u;
        u.name    = m.captured(2);
        u.version = m.captured(3);
        u.repo    = repos.value(u.name, QStringLiteral("AUR"));
        u.publisher = publisherForRepo(u.repo);

        const QDate d = QDate::fromString(m.captured(1), "yyyy-MM-dd");
        u.date = d.isValid() ? d.toString("M/d/yyyy") : m.captured(1);

        latest.insert(u.name, u);
    }

    result = latest.values();
    return result;
}

// Sidebar entries
QList<SidebarLink> InstalledUpdatesPage::sidebarLinks()
{
    return {
        Nav::home("Control Panel Home"),
        Nav::to("Uninstall a program", PageId::ProgramsFeatures),
        Nav::plain("Turn Linux features on or off"),
    };
}

QList<SidebarLink> InstalledUpdatesPage::sidebarSeeAlso()
{
    return {};
}

// Page
InstalledUpdatesPage::InstalledUpdatesPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/0);
    // The list frame (command bar + tree + status strip) is full-bleed, so the
    // column itself carries no horizontal padding; only the header text below is
    // indented, via its own inset sub-layout.
    contentV->setContentsMargins(0, 18, 0, 0);

    // Title + instruction (the only indented block).
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    headerV->addWidget(Win7::pageTitle("Uninstall an update"));
    headerV->addSpacing(8);
    headerV->addWidget(Win7::label(
        "To uninstall an update, select it from the list and then click Uninstall or Change."));
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

    // Updates tree
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ "Name", "Program", "Version", "Publisher" });
    m_tree->setItemsExpandable(false);
    Win7::configureListTree(m_tree);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 320);
    m_tree->setColumnWidth(1, 150);
    m_tree->setColumnWidth(2, 90);

    // Placeholder shown until the worker thread finishes gathering (see below).
    Win7::addTreePlaceholder(m_tree, "Searching for installed updates...");
    contentV->addWidget(m_tree, 1);

    // Status bar: item count
    QHBoxLayout *statusH = nullptr;
    auto *statusBar = Win7::statusPanel(36, &statusH);

    auto *statusIcon = new QLabel;
    statusIcon->setPixmap(themeIcon({"system-software-update",
                                     "preferences-system"}).pixmap(24, 24));
    statusH->addWidget(statusIcon);

    m_countLbl = Win7::label("0 items", 9, "#1F1F1F");
    statusH->addWidget(m_countLbl);
    statusH->addStretch(1);
    contentV->addWidget(statusBar);

    // Navigate first, gather second: the package query shells out to pacman and
    // can take a while, so run it on a worker thread and populate when it lands.
    // The page is already on screen with the "Searching..." placeholder by then.
    m_tree->setCursor(Qt::BusyCursor);
    Win7::runAsync(this, &InstalledUpdatesPage::gatherUpdates,
                   [this](const QList<UpdateInfo> &updates) {
        populate(updates);
    });
}

void InstalledUpdatesPage::populate(const QList<UpdateInfo> &updatesIn)
{
    m_tree->setCursor(Qt::ArrowCursor);
    m_tree->clear();   // drop the "Searching..." placeholder

    // Order repositories deterministically: official repos first, then CachyOS,
    // then AUR, then anything else alphabetically.
    auto repoRank = [](const QString &r) -> int {
        if (r == "core")     return 0;
        if (r == "extra")    return 1;
        if (r == "multilib") return 2;
        if (r.startsWith("cachyos")) return 3;
        if (r == "AUR")      return 5;
        return 4;
    };

    QStringList repoOrder;
    QHash<QString, QList<UpdateInfo>> byRepo;
    for (const UpdateInfo &u : updatesIn) {
        if (!byRepo.contains(u.repo)) repoOrder << u.repo;
        byRepo[u.repo].append(u);
    }
    std::sort(repoOrder.begin(), repoOrder.end(),
        [&](const QString &a, const QString &b) {
            const int ra = repoRank(a), rb = repoRank(b);
            return (ra != rb) ? ra < rb : a < b;
        });

    const QIcon updateIcon = themeIcon({"system-software-update",
                                        "package-x-generic", "applications-other"});

    int totalItems = 0;
    for (const QString &repo : repoOrder) {
        QList<UpdateInfo> &items = byRepo[repo];
        std::sort(items.begin(), items.end(),
            [](const UpdateInfo &a, const UpdateInfo &b) {
                return a.name.localeAwareCompare(b.name) < 0;
            });

        auto *group = new QTreeWidgetItem(m_tree);
        group->setFirstColumnSpanned(true);
        group->setText(0, QString("%1 (%2)").arg(repo).arg(items.size()));
        group->setForeground(0, QColor("#1F4E99"));
        group->setFlags(Qt::ItemIsEnabled);   // header: not selectable
        Win7::setItemPointSize(group);

        for (const UpdateInfo &u : items) {
            auto *child = new QTreeWidgetItem(group);
            child->setIcon(0, updateIcon);
            child->setText(0, QString("Update for %1 (%2)").arg(u.name, u.version));
            child->setText(1, u.name);
            child->setText(2, u.version);
            child->setText(3, u.publisher);
            Win7::setItemPointSize(child);
            ++totalItems;
        }
    }
    m_tree->expandAll();

    m_countLbl->setText(QString("%1 item%2")
                            .arg(totalItems).arg(totalItems == 1 ? "" : "s"));
}
