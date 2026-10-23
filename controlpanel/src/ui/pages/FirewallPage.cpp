#include "FirewallPage.h"
#include "ConfFile.h"
#include "IconHelper.h"
#include "Win7Ui.h"
#include "Branding.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QHostInfo>

using Win7::ClickableWidget;

// True when the machine has a default route (i.e. is on a network). Mirrors the
// Network and Sharing Center's reading of /proc/net/route: the default route is
// the row whose Destination column ("00000000") is all-zero.
static bool hasDefaultRoute()
{
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString text = QString::fromUtf8(f.readAll());
    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (int i = 1; i < lines.size(); ++i) {           // skip the header row
        const QList<QStringView> cols = lines[i].split(u'\t', Qt::SkipEmptyParts);
        if (cols.size() >= 2 && cols[1].trimmed() == u"00000000")
            return true;
    }
    return false;
}

// Count the `### tuple` rule entries in a ufw user.rules file, one per rule
// the user has added, matching the rule count KDE's firewall module lists.
static int countRules(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QString text = QString::fromUtf8(f.readAll());
    int n = 0;
    for (const QStringView &line : QStringView(text).split(u'\n')) {
        if (line.trimmed().startsWith(QLatin1String("### tuple ###")))
            ++n;
    }
    return n;
}

FirewallPage::FwInfo FirewallPage::gatherInfo()
{
    FwInfo fw;

    fw.enabled = readConfField(QStringLiteral("/etc/ufw/ufw.conf"),
                           QStringLiteral("ENABLED")).compare(
                               QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
    fw.logLevel = readConfField(QStringLiteral("/etc/ufw/ufw.conf"),
                            QStringLiteral("LOGLEVEL"));

    fw.inputPolicy  = readConfField(QStringLiteral("/etc/default/ufw"),
                                QStringLiteral("DEFAULT_INPUT_POLICY")).toUpper();
    fw.outputPolicy = readConfField(QStringLiteral("/etc/default/ufw"),
                                QStringLiteral("DEFAULT_OUTPUT_POLICY")).toUpper();
    if (fw.inputPolicy.isEmpty())
        fw.inputPolicy = QStringLiteral("DROP");

    fw.ruleCount = countRules(QStringLiteral("/etc/ufw/user.rules"))
                 + countRules(QStringLiteral("/etc/ufw/user6.rules"));

    fw.netConnected = hasDefaultRoute();
    fw.networkName  = QStringLiteral("Network");

    return fw;
}

// Sidebar
QList<SidebarLink> FirewallPage::sidebarLinks()
{
    return {
        Nav::plain("Allow a program or feature through Linux Firewall"),
        Nav::plain("Change notification settings"),
        Nav::plain("Turn Linux Firewall on or off"),
        Nav::plain("Restore defaults"),
        Nav::plain("Advanced settings"),
        Nav::plain("Troubleshoot my network"),
    };
}

QList<SidebarLink> FirewallPage::sidebarSeeAlso()
{
    return {
        Nav::to("Action Center", PageId::ActionCenter),
        Nav::to("Network and Sharing Center", PageId::NetworkSharing),
    };
}

// Network-location panel
// Human-readable phrasing for the default-incoming-policy line, mirroring the
// way KDE's firewall module summarises DEFAULT_INPUT_POLICY.
static QString incomingText(const QString &policy)
{
    if (policy == QLatin1String("ACCEPT"))
        return QStringLiteral("Allow all incoming connections");
    if (policy == QLatin1String("REJECT"))
        return QStringLiteral("Reject all connections to programs that are not "
                              "on the list of allowed programs");
    // DROP (the ufw default): silently dropped, matching Windows' "Block all".
    return QStringLiteral("Block all connections to programs that are not on "
                          "the list of allowed programs");
}

// Notification phrasing derived from ufw's LOGLEVEL ("off" disables logging).
static QString notifyText(const QString &logLevel)
{
    if (logLevel.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0)
        return Branding::brand("Do not notify me when Linux Firewall blocks a "
                               "new program");
    return Branding::brand("Notify me when Linux Firewall blocks a new program");
}

QWidget *FirewallPage::buildLocationPanel(const QString &title,
                                          const QString &connState,
                                          bool expanded,
                                          const FwInfo &info)
{
    auto *panel = new QFrame;
    panel->setObjectName("fwPanel");
    panel->setStyleSheet(
        "#fwPanel { background: #FFFFFF; border: 1px solid #DCDCDC; }");
    auto *panelV = new QVBoxLayout(panel);
    panelV->setContentsMargins(0, 0, 0, 0);
    panelV->setSpacing(0);

    // Header strip: green accent bar, shield icon, title, conn-state, chevron.
    // The header is clickable and toggles the body below, like the Windows
    // firewall network sections.
    auto *header = new ClickableWidget;
    header->setStyleSheet("background: transparent;");
    header->setCursor(Qt::PointingHandCursor);
    auto *headerH = new QHBoxLayout(header);
    headerH->setContentsMargins(0, 0, 8, 0);
    headerH->setSpacing(0);

    auto *accent = new QFrame;
    accent->setFixedWidth(14);
    accent->setMinimumHeight(34);
    accent->setStyleSheet("background: #4E9A2A; border: none;");
    headerH->addWidget(accent);

    auto *shield = new QLabel;
    shield->setFixedSize(20, 20);
    shield->setPixmap(themeIcon({"security-high", "preferences-security-firewall",
                                 "security-medium", "firewall-config"})
                          .pixmap(16, 16));
    shield->setStyleSheet("background: transparent;");
    headerH->addSpacing(8);
    headerH->addWidget(shield, 0, Qt::AlignVCenter);

    headerH->addSpacing(6);
    headerH->addWidget(Win7::label(title, 11, "#1A5FB4"), 0, Qt::AlignVCenter);
    headerH->addStretch(1);

    headerH->addWidget(Win7::label(connState, 11, "#2D2D2D"), 0, Qt::AlignVCenter);

    // The shared round Aero expander. The whole header strip is the click
    // target, so the button itself lets clicks fall through to it.
    auto *chevron = new Win7::ChevronButton(/*interactive=*/false);
    chevron->setChecked(expanded);
    headerH->addSpacing(8);
    headerH->addWidget(chevron, 0, Qt::AlignVCenter);

    header->setMinimumHeight(34);
    panelV->addWidget(header);

    // Body: caption line + the status grid. Built regardless of the initial
    // state so the header can toggle it on click; visibility is set at the end.
    auto *sep = Win7::hairline("#E2E2E2");
    panelV->addWidget(sep);

    // Body has no horizontal margin so the dividers below span the full panel
    // width; the caption and grid carry their own left inset (kLeftInset) so
    // their text lines up under the green banner rather than under the icon.
    const int kLeftInset = 8;
    const int kRightInset = 16;

    auto *body = new QWidget;
    body->setStyleSheet("background: transparent;");
    auto *bodyV = new QVBoxLayout(body);
    bodyV->setContentsMargins(0, 4, 0, 14);
    bodyV->setSpacing(0);

    auto *caption = Win7::bodyLabel(
        "Linux Firewall (ufw) applies one set of rules to every network. Unlike "
        "Windows, Linux has no separate Home, Work, or Public network profiles.");
    caption->setContentsMargins(kLeftInset, 0, kRightInset, 0);
    bodyV->addWidget(caption);
    bodyV->addSpacing(12);

    // Divider between the caption and the status grid, as in the Windows panel.
    bodyV->addWidget(Win7::hairline("#E2E2E2"));
    bodyV->addSpacing(14);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(kLeftInset, 0, kRightInset, 0);
    grid->setHorizontalSpacing(55);
    grid->setVerticalSpacing(12);
    grid->setColumnMinimumWidth(0, 200);
    grid->setColumnStretch(1, 1);

    int r = 0;
    grid->addWidget(Win7::bodyLabel(Branding::brand("Linux Firewall state:")),
                    r, 0);
    grid->addWidget(Win7::bodyLabel(info.enabled ? "On" : "Off"), r, 1);
    ++r;

    grid->addWidget(Win7::bodyLabel("Incoming connections:"), r, 0);
    grid->addWidget(Win7::bodyLabel(incomingText(info.inputPolicy)), r, 1);
    ++r;

    grid->addWidget(Win7::bodyLabel("Active networks:"), r, 0);
    // Icon + network-name cell.
    {
        auto *cell = new QWidget;
        cell->setStyleSheet("background: transparent;");
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        auto *icon = new QLabel;
        icon->setFixedSize(16, 16);
        icon->setPixmap(themeIcon({"network-workgroup", "network-type-public",
                                   "preferences-system-network", "network-wired"})
                            .pixmap(16, 16));
        icon->setStyleSheet("background: transparent;");
        h->addWidget(icon, 0, Qt::AlignVCenter);
        h->addWidget(Win7::bodyLabel(info.netConnected ? info.networkName
                                                       : QStringLiteral("None")),
                     0, Qt::AlignVCenter);
        h->addStretch(1);
        grid->addWidget(cell, r, 1, Qt::AlignLeft | Qt::AlignTop);
    }
    ++r;

    grid->addWidget(Win7::bodyLabel("Notification state:"), r, 0);
    grid->addWidget(Win7::bodyLabel(notifyText(info.logLevel)), r, 1);
    ++r;

    bodyV->addLayout(grid);
    panelV->addWidget(body);

    // Dropdown behaviour: clicking the header expands/collapses the body and
    // flips the chevron, matching the Windows firewall network sections.
    auto applyState = [body, sep, chevron](bool open) {
        sep->setVisible(open);
        body->setVisible(open);
        chevron->setChecked(open);
    };
    applyState(expanded);

    // The mutable lambda is stored in header->onClick, so its captured `open`
    // copy persists between clicks and tracks the open/closed state.
    header->onClick = [applyState, open = expanded]() mutable {
        open = !open;
        applyState(open);
    };

    return panel;
}

// Page
FirewallPage::FirewallPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const FwInfo info = gatherInfo();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    contentV->addWidget(
        Win7::pageTitle(Branding::brand(
            "Help protect your computer with Linux Firewall")));
    contentV->addSpacing(10);

    contentV->addWidget(Win7::bodyLabel(Branding::brand(
        "Linux Firewall can help prevent hackers or malicious software from "
        "gaining access to your computer through the Internet or a network.")));
    contentV->addSpacing(10);

    auto addHelpLink = [&](const QString &text) {
        contentV->addWidget(Win7::bodyLabel(text, /*link=*/true), 0, Qt::AlignLeft);
        contentV->addSpacing(4);
    };
    addHelpLink("How does a firewall help protect my computer?");
    addHelpLink("What are network locations?");

    contentV->addSpacing(12);

    // The network panel. ufw has no per-network profiles (no Home/Work/Public
    // split), so rather than mimic Windows' two location panels we show a
    // single "All networks" section expanded to the live ufw state. It is
    // "Connected" when a default route exists.
    contentV->addWidget(buildLocationPanel(
        "All networks",
        info.netConnected ? "Connected" : "Not Connected",
        /*expanded=*/true, info));

    contentV->addStretch(1);
}
