#include "NetworkSharingPage.h"
#include "Commands.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QDir>
#include <QHostInfo>
#include <QSysInfo>

// Data gathering
// The interface backing the default route, read from /proc/net/route. Each line
// after the header is tab/space separated; the default route is the row whose
// Destination column is all-zero ("00000000"). When several exist we keep the
// one with the lowest metric, mirroring how the kernel picks the active route.
static QString defaultRouteInterface()
{
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    // /proc files report size 0, so slurp with readAll() and split ourselves
    // (QTextStream::readLine() treats a zero-length file as immediately at-EOF).
    const QString text = QString::fromUtf8(f.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n');

    QString best;
    long bestMetric = -1;
    for (int i = 1; i < lines.size(); ++i) {           // skip the header row
        const QList<QStringView> cols = lines[i].split(u'\t', Qt::SkipEmptyParts);
        if (cols.size() < 7)
            continue;
        if (cols[1].trimmed() != u"00000000")          // Destination
            continue;
        const long metric = cols[6].trimmed().toString().toLong();
        if (bestMetric < 0 || metric < bestMetric) {
            bestMetric = metric;
            best = cols[0].trimmed().toString();        // Iface
        }
    }
    return best;
}

NetworkSharingPage::NetInfo NetworkSharingPage::gatherInfo()
{
    NetInfo n;

    n.computerName = QHostInfo::localHostName();
    if (n.computerName.isEmpty())
        n.computerName = QSysInfo::machineHostName();
    n.computerName = n.computerName.toUpper();

    const QString iface = defaultRouteInterface();
    n.connected = !iface.isEmpty();

    // A wireless interface exposes a "wireless" directory under its sysfs node;
    // wired adapters do not.
    if (n.connected)
        n.wireless = QDir(QStringLiteral("/sys/class/net/%1/wireless").arg(iface))
                         .exists();

    n.accessType     = n.connected ? QStringLiteral("Internet")
                                    : QStringLiteral("No network access");
    n.connectionName = n.wireless ? QStringLiteral("Wireless Network Connection")
                                   : QStringLiteral("Local Area Connection");
    n.networkName    = QStringLiteral("Network");

    return n;
}

// Sidebar
QList<SidebarLink> NetworkSharingPage::sidebarLinks()
{
    return {
        Nav::command("Change adapter settings", kcm("kcm_networkmanagement")),
        Nav::plain("Change advanced sharing settings"),
    };
}

QList<SidebarLink> NetworkSharingPage::sidebarSeeAlso()
{
    return {
        Nav::plain("HomeGroup"),
        Nav::plain("Internet Options"),
        Nav::to("Linux Firewall", PageId::Firewall),
    };
}

// Network-map helpers
static const int kMapIconSize = 40;

QWidget *NetworkSharingPage::buildMapNode(const QStringList &iconNames,
                                          const QString &caption,
                                          const QString &subCaption)
{
    auto *node = new QWidget;
    node->setStyleSheet("background: transparent;");
    auto *col = new QVBoxLayout(node);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    // First name that resolves wins, degrading to "preferences-system".
    QIcon icon;
    for (const QString &name : iconNames) {
        icon = tryIconName(name);
        if (!icon.isNull()) break;
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("preferences-system"));

    auto *iconLabel = new QLabel;
    iconLabel->setFixedSize(kMapIconSize, kMapIconSize);
    iconLabel->setPixmap(icon.pixmap(kMapIconSize, kMapIconSize));
    iconLabel->setStyleSheet("background: transparent;");
    col->addWidget(iconLabel, 0, Qt::AlignHCenter);

    auto *cap = Win7::label(caption);
    cap->setAlignment(Qt::AlignHCenter);
    col->addWidget(cap, 0, Qt::AlignHCenter);

    if (!subCaption.isEmpty()) {
        auto *sub = Win7::label(subCaption);
        sub->setAlignment(Qt::AlignHCenter);
        col->addWidget(sub, 0, Qt::AlignHCenter);
    }

    return node;
}

QWidget *NetworkSharingPage::buildMapLink(bool active)
{
    // The connector sits at the vertical centre of the map icons, so pad it down
    // by roughly half an icon height before drawing the thin rule.
    auto *wrap = new QWidget;
    wrap->setStyleSheet("background: transparent;");
    auto *v = new QVBoxLayout(wrap);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addSpacing(kMapIconSize / 2 - 1);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(2);
    line->setFixedWidth(100);
    line->setStyleSheet(active
        ? "QFrame { background: #6E94C0; border: none; }"
        : "QFrame { background: #C0392B; border: none; }");
    v->addWidget(line);
    v->addStretch(1);

    return wrap;
}

// Task helper
void NetworkSharingPage::addTask(QVBoxLayout *into, const QStringList &iconNames,
                                 const QString &title, const QString &description)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    QIcon icon;
    for (const QString &name : iconNames) {
        icon = tryIconName(name);
        if (!icon.isNull()) break;
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("preferences-system"));

    auto *iconLabel = new QLabel;
    iconLabel->setFixedSize(24, 24);
    iconLabel->setPixmap(icon.pixmap(24, 24));
    iconLabel->setStyleSheet("background: transparent;");
    row->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(1);

    textCol->addWidget(Win7::bodyLabel(title, /*link=*/true));

    auto *descLabel = Win7::label(description, 9, "#333333");
    descLabel->setWordWrap(true);
    textCol->addWidget(descLabel);

    row->addLayout(textCol, 1);

    into->addLayout(row);
    into->addSpacing(14);
}

// Page
NetworkSharingPage::NetworkSharingPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const NetInfo info = gatherInfo();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page title
    contentV->addWidget(Win7::pageTitle(
        "View your basic network information and set up connections"));
    contentV->addSpacing(16);

    // Network map: This computer, Network, Internet
    auto *mapRow = new QHBoxLayout;
    mapRow->setContentsMargins(8, 0, 0, 0);
    mapRow->setSpacing(0);

    auto *computerNode = buildMapNode({"computer", "video-display",
                                       "preferences-system"},
                                      info.computerName, "(This computer)");
    auto *networkNode  = buildMapNode({"network-type-public", "network-workgroup",
                                       "preferences-system-network",
                                       "network-wired"},
                                      info.networkName);
    auto *internetNode = buildMapNode({"emblem-web", "applications-internet",
                                       "internet-web-browser", "network-globe",
                                       "globe"},
                                      "Internet");

    // Each node is as wide as its caption, but the icon is centred within it.
    // If the nodes had different widths (the host name is far wider than
    // "Network"/"Internet"), every icon would sit a different distance from the
    // connector lines. Pin all three to the widest node so the gap on either
    // side of each icon is identical.
    const int nodeW = qMax(computerNode->sizeHint().width(),
                           qMax(networkNode->sizeHint().width(),
                                internetNode->sizeHint().width()));
    computerNode->setFixedWidth(nodeW);
    networkNode->setFixedWidth(nodeW);
    internetNode->setFixedWidth(nodeW);

    mapRow->addWidget(computerNode, 0, Qt::AlignTop);
    mapRow->addWidget(buildMapLink(info.connected), 0, Qt::AlignTop);
    mapRow->addWidget(networkNode, 0, Qt::AlignTop);
    mapRow->addWidget(buildMapLink(info.connected), 0, Qt::AlignTop);
    mapRow->addWidget(internetNode, 0, Qt::AlignTop);
    mapRow->addStretch(1);

    // "See full map" link, pinned to the top-right of the map row.
    mapRow->addWidget(Win7::bodyLabel("See full map", /*link=*/true),
                      0, Qt::AlignTop);

    contentV->addLayout(mapRow);
    contentV->addSpacing(18);

    // "View your active networks" heading + "Connect or disconnect".
    // Same design as the System page: the heading label sits on a row with a
    // faint rule trailing off to its right, and an optional link pinned past the
    // rule at the far right.
    auto addHeading = [&](const QString &text, const QString &trailingLink) {
        QWidget *trailing = trailingLink.isEmpty()
            ? nullptr
            : Win7::bodyLabel(trailingLink, /*link=*/true);
        contentV->addLayout(
            Win7::sectionHeading(text, trailing, nullptr, "#000000"));
        contentV->addSpacing(6);
    };

    addHeading("View your active networks", "Connect or disconnect");

    // Active-network panel: icon + (name / category) on the left, a vertical
    // rule, then the access-type / connections grid on the right.
    auto *activeRow = new QHBoxLayout;
    activeRow->setContentsMargins(8, 0, 0, 0);
    activeRow->setSpacing(14);

    auto *netIcon = new QLabel;
    netIcon->setFixedSize(40, 40);
    netIcon->setPixmap(themeIcon({"network-type-public", "network-workgroup",
                                  "preferences-system-network",
                                  "network-wired"}).pixmap(40, 40));
    netIcon->setStyleSheet("background: transparent;");
    activeRow->addWidget(netIcon, 0, Qt::AlignVCenter);

    auto *nameCol = new QVBoxLayout;
    nameCol->setContentsMargins(0, 0, 0, 0);
    nameCol->setSpacing(2);

    auto *netName = new QLabel(info.networkName);
    {
        QFont f = netName->font();
        f.setPointSize(9);
        f.setBold(true);
        netName->setFont(f);
    }
    netName->setStyleSheet("color: #000000; background: transparent;");
    nameCol->addWidget(netName);

    nameCol->addWidget(Win7::bodyLabel("Public network", /*link=*/true));
    activeRow->addLayout(nameCol);

    activeRow->addSpacing(20);

    auto *vrule = new QFrame;
    vrule->setFrameShape(QFrame::VLine);
    vrule->setFixedWidth(1);
    vrule->setFixedHeight(40);
    vrule->setStyleSheet("QFrame { background: #DDDDDD; border: none; }");
    activeRow->addWidget(vrule, 0, Qt::AlignVCenter);

    activeRow->addSpacing(20);

    // Access type / Connections grid.
    auto *detailGrid = new QGridLayout;
    detailGrid->setContentsMargins(0, 0, 0, 0);
    detailGrid->setHorizontalSpacing(10);
    detailGrid->setVerticalSpacing(6);

    detailGrid->addWidget(Win7::label("Access type:"), 0, 0,
                          Qt::AlignLeft | Qt::AlignVCenter);
    detailGrid->addWidget(Win7::label(info.accessType), 0, 1,
                          Qt::AlignLeft | Qt::AlignVCenter);

    detailGrid->addWidget(Win7::label("Connections:"), 1, 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    // The connection value is a small-icon + blue link cell.
    auto *connCell = new QWidget;
    connCell->setStyleSheet("background: transparent;");
    auto *connH = new QHBoxLayout(connCell);
    connH->setContentsMargins(0, 0, 0, 0);
    connH->setSpacing(5);

    auto *connIcon = new QLabel;
    connIcon->setFixedSize(16, 16);
    connIcon->setPixmap(themeIcon(info.wireless
                            ? std::initializer_list<const char *>{
                                  "network-wireless", "network-wireless-connected"}
                            : std::initializer_list<const char *>{
                                  "network-wired", "network-wired-activated"})
                            .pixmap(16, 16));
    connIcon->setStyleSheet("background: transparent;");
    connH->addWidget(connIcon, 0, Qt::AlignVCenter);

    connH->addWidget(Win7::bodyLabel(info.connectionName, /*link=*/true),
                     0, Qt::AlignVCenter);
    connH->addStretch(1);

    detailGrid->addWidget(connCell, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    activeRow->addLayout(detailGrid);
    activeRow->addStretch(1);

    contentV->addLayout(activeRow);
    contentV->addSpacing(20);

    // "Change your networking settings" heading + tasks
    addHeading("Change your networking settings", QString());

    addTask(contentV, {"network-wired", "preferences-system-network",
                       "list-add"},
            "Set up a new connection or network",
            "Set up a wireless, broadband, dial-up, ad hoc, or VPN connection; "
            "or set up a router or access point.");

    addTask(contentV, {"network-wireless", "network-connect",
                       "preferences-system-network"},
            "Connect to a network",
            "Connect or reconnect to a wireless, wired, dial-up, or VPN network "
            "connection.");

    addTask(contentV, {"network-type-home", "network-workgroup", "system-users",
                       "preferences-system-network"},
            "Choose homegroup and sharing options",
            "Access files and printers located on other network computers, or "
            "change sharing settings.");

    addTask(contentV, {"system-help", "help-browser", "tools-wizard",
                       "preferences-system"},
            "Troubleshoot problems",
            "Diagnose and repair network problems, or get troubleshooting "
            "information.");

    contentV->addStretch(1);
}
