#include "ui/control/pages/InstalledUpdatesPage.h"
#include "DetailsPane.h"
#include "ui/control/IconHelper.h"
#include "ui/control/Win7Ui.h"

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
#include "aero/palette.h"

namespace ControlPanel {

namespace {

// The details pane's icon for this page
QIcon updatesIcon()
{
    return Aero::themeIcon({"system-software-update", "preferences-system"});
}

} // namespace

// Data gathering
QHash<QString, QString> InstalledUpdatesPage::repositoryMap()
{
    // `pacman -Sl` prints "<repo> <pkg> <version> [installed]" for every package
    // in the configured sync databases. We only need pkg -> repo
    QHash<QString, QString> map;
    QProcess process;
    process.start("pacman", {"-Sl"});
    if (!process.waitForFinished(8000))
        return map;

    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const int firstSp = line.indexOf(u' ');
        if (firstSp < 0)
            continue;
        const int secondSp = line.indexOf(u' ', firstSp + 1);
        if (secondSp < 0)
            continue;
        const QString repo = line.left(firstSp).toString();
        const QString pkg  = line.mid(firstSp + 1, secondSp - firstSp - 1).toString();
        map.insert(pkg, repo);
    }
    return map;
}

// Vendor-style label for the Publisher column, derived from the repository
static QString publisherForRepo(const QString &repo)
{
    if (repo.startsWith("cachyos"))
        return "CachyOS";
    if (repo == "AUR")
        return "Arch User Repository";
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
    // the same package overwrites the earlier one; we track insertion-time data
    QHash<QString, UpdateInfo> latest;
    const QString text = QString::fromUtf8(log.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n', Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const auto match = re.match(line.toString());
        if (!match.hasMatch())
            continue;

        UpdateInfo u;
        u.name    = match.captured(2);
        u.version = match.captured(3);
        u.repo    = repos.value(u.name, QStringLiteral("AUR"));
        u.publisher = publisherForRepo(u.repo);

        const QDate d = QDate::fromString(match.captured(1), "yyyy-MM-dd");
        if (d.isValid())
            u.date = d.toString("M/d/yyyy");
        else
            u.date = match.captured(1);

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
    // indented, via its own inset sub-layout
    contentV->setContentsMargins(0, 18, 0, 0);

    // Title + instruction (the only indented block)
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    headerV->addWidget(Win7::pageTitle("Uninstall an update"));
    headerV->addSpacing(8);
    headerV->addWidget(Aero::label("To uninstall an update, select it from the list "
                                   "and then click Uninstall or Change."));
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

    // Placeholder shown until the worker thread finishes gathering (see below)
    Win7::addTreePlaceholder(m_tree, "Searching for installed updates...");
    contentV->addWidget(m_tree, 1);

    // Explorer's details pane, with the item count once it is known
    m_details = new DetailsPane;
    m_details->showContent(updatesIcon(), "0 items");
    contentV->addWidget(m_details);

    // Navigate first, gather second: the package query shells out to pacman and
    // can take a while, so run it on a worker thread and populate when it lands.
    // The page is already on screen with the "Searching..." placeholder by then
    m_tree->setCursor(Qt::BusyCursor);
    Win7::runAsync(this, &InstalledUpdatesPage::gatherUpdates,
                   [this](const QList<UpdateInfo> &updates) {
        populate(updates);
    });
}

void InstalledUpdatesPage::populate(const QList<UpdateInfo> &updatesIn)
{
    m_tree->setCursor(Qt::ArrowCursor);
    // Drop the "Searching..." placeholder
    m_tree->clear();

    // Order repositories deterministically: official repos first, then CachyOS,
    // then AUR, then anything else alphabetically
    auto repoRank = [](const QString &repo) -> int {
        if (repo == "core")
            return 0;
        if (repo == "extra")
            return 1;
        if (repo == "multilib")
            return 2;
        if (repo.startsWith("cachyos"))
            return 3;
        if (repo == "AUR")
            return 5;
        return 4;
    };

    QStringList repoOrder;
    QHash<QString, QList<UpdateInfo>> byRepo;
    for (const UpdateInfo &u : updatesIn) {
        if (!byRepo.contains(u.repo))
            repoOrder << u.repo;
        byRepo[u.repo].append(u);
    }
    std::sort(repoOrder.begin(), repoOrder.end(),
        [&](const QString &left, const QString &right) {
            const int leftRank = repoRank(left);
            const int rightRank = repoRank(right);
            if (leftRank != rightRank)
                return leftRank < rightRank;
            return left < right;
        });

    const QIcon updateIcon = Aero::themeIcon({"system-software-update",
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
        group->setForeground(0, Aero::Palette::rgb(Aero::Palette::LinkText));
        // Header: not selectable
        group->setFlags(Qt::ItemIsEnabled);
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

    QString count = QString("%1 items").arg(totalItems);
    if (totalItems == 1)
        count = QStringLiteral("1 item");
    m_details->showContent(updatesIcon(), count);
}

} // namespace ControlPanel
