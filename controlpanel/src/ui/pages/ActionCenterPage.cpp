#include "ActionCenterPage.h"
#include "Commands.h"
#include "ConfFile.h"
#include "IconHelper.h"
#include "Win7Ui.h"
#include "Branding.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QPushButton>
#include <QStandardPaths>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

using Win7::ClickableWidget;

// Data gathering
ActionCenterPage::AcInfo ActionCenterPage::gatherInfo()
{
    AcInfo ac;

    ac.firewallOn = readConfField(QStringLiteral("/etc/ufw/ufw.conf"),
                                  QStringLiteral("ENABLED")).compare(
                                      QStringLiteral("yes"), Qt::CaseInsensitive) == 0;

    // "Spyware and unwanted software protection" maps to an on-demand scanner.
    // ClamAV is the ubiquitous Linux one; report it when its binary is present.
    if (!QStandardPaths::findExecutable(QStringLiteral("clamscan")).isEmpty()) {
        ac.avPresent = true;
        ac.avName = QStringLiteral("ClamAV");
    }

    // UAC maps to polkit: administrative actions prompt for authentication when
    // a polkit authority is registered on the system bus (it always is on KDE).
    if (auto *iface = QDBusConnection::systemBus().interface())
        ac.uacOn = iface->isServiceRegistered(
            QStringLiteral("org.freedesktop.PolicyKit1"));

    return ac;
}

// Sidebar
QList<SidebarLink> ActionCenterPage::sidebarLinks()
{
    return {
        Nav::plain("Change Action Center settings"),
        Nav::command("Change User Account Control settings", kcm("kcm_users")),
        Nav::plain("View archived messages"),
        Nav::to("View performance information", PageId::Performance),
    };
}

QList<SidebarLink> ActionCenterPage::sidebarSeeAlso()
{
    return {
        Nav::plain("Backup and Restore"),
        Nav::to("Linux Update", PageId::LinuxUpdate),
        Nav::to("Performance Information and Tools", PageId::Performance),
    };
}

using Win7::bodyLabel;

// Status row
QWidget *ActionCenterPage::buildStatusRow(const QString &item,
                                          const QString &state,
                                          const QString &description,
                                          const QString &link)
{
    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *v = new QVBoxLayout(row);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    // Top line: bold item name on the left, state right-aligned.
    auto *topH = new QHBoxLayout;
    topH->setContentsMargins(0, 0, 0, 0);
    topH->setSpacing(8);

    auto *name = new QLabel(item);
    {
        QFont f = name->font();
        f.setPointSize(9);
        name->setFont(f);
    }
    name->setStyleSheet("color: #000000; background: transparent;");
    topH->addWidget(name, 0, Qt::AlignVCenter);
    topH->addStretch(1);

    auto *stateLabel = new QLabel(state);
    {
        QFont f = stateLabel->font();
        f.setPointSize(9);
        stateLabel->setFont(f);
    }
    stateLabel->setStyleSheet("color: #000000; background: transparent;");
    topH->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
    v->addLayout(topH);

    // Description sits indented under the item name, as in the reference.
    auto *desc = bodyLabel(description);
    desc->setContentsMargins(18, 0, 0, 0);
    v->addWidget(desc);

    if (!link.isEmpty()) {
        auto *l = bodyLabel(link, /*link=*/true);
        l->setContentsMargins(18, 0, 0, 0);
        v->addWidget(l);
    }

    return row;
}

// Collapsible section
QWidget *ActionCenterPage::buildSection(const QString &title, bool expanded,
                                        QWidget *rows)
{
    auto *panel = new QWidget;
    panel->setStyleSheet("background: transparent;");
    auto *panelV = new QVBoxLayout(panel);
    panelV->setContentsMargins(0, 0, 0, 0);
    panelV->setSpacing(0);

    // Header strip: blue section title on the left, a round-ish chevron on the
    // right. The whole strip is clickable and toggles the rows below it.
    auto *header = new ClickableWidget;
    header->setStyleSheet("background: transparent;");
    header->setCursor(Qt::PointingHandCursor);
    header->setMinimumHeight(36);
    auto *headerH = new QHBoxLayout(header);
    headerH->setContentsMargins(0, 4, 8, 4);
    headerH->setSpacing(0);

    headerH->addWidget(Win7::label(title, 13, "#1A5FB4"), 0, Qt::AlignVCenter);
    headerH->addStretch(1);

    // The shared round Aero expander. The whole header strip is the click
    // target, so the button itself lets clicks fall through to it.
    auto *chevron = new Win7::ChevronButton(/*interactive=*/false);
    chevron->setChecked(expanded);
    headerH->addWidget(chevron, 0, Qt::AlignVCenter);
    panelV->addWidget(header);

    // Thin separator directly under the header, always visible.
    panelV->addWidget(Win7::hairline("#DCDCDC"));

    // Body carries a left inset so the rows line up under the title text.
    rows->setContentsMargins(2, 12, 2, 12);
    panelV->addWidget(rows);

    // Clicking the header expands/collapses the rows and flips the chevron,
    // matching the Windows Action Center sections.
    auto applyState = [rows, chevron](bool open) {
        rows->setVisible(open);
        chevron->setChecked(open);
    };
    applyState(expanded);
    header->onClick = [applyState, open = expanded]() mutable {
        open = !open;
        applyState(open);
    };

    return panel;
}

// Yellow "action needed" alert
QWidget *ActionCenterPage::buildAlertBox(const QString &title,
                                         const QString &description,
                                         const QString &buttonText,
                                         const QString &link)
{
    // Same ribbon-bar treatment as the Linux Update page's status box: a white
    // box with a 20px yellow "action needed" stripe down the left edge (rather
    // than a thin accent frame). No icon, matching the Windows backup box.
    auto *frame = new QFrame;
    frame->setObjectName("acAlert");
    frame->setMaximumWidth(620);
    frame->setStyleSheet(
        "QFrame#acAlert {"
        "  background: #FFFFFF;"
        "  border: 1px solid #CCCCCC;"
        "  border-left: 20px solid #D4A800;"
        "}"
        "QFrame#acAlert QLabel { background: transparent; border: none; }");

    auto *innerH = new QHBoxLayout(frame);
    innerH->setContentsMargins(12, 12, 12, 12);
    innerH->setSpacing(12);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(4);

    auto *titleLabel = new QLabel(title);
    {
        QFont f = titleLabel->font();
        f.setPointSize(9);
        f.setBold(true);
        titleLabel->setFont(f);
    }
    titleLabel->setStyleSheet("color: #000000; background: transparent;");
    textCol->addWidget(titleLabel);
    textCol->addWidget(bodyLabel(description));
    if (!link.isEmpty())
        textCol->addWidget(bodyLabel(link, /*link=*/true));
    innerH->addLayout(textCol, 1);

    auto *button = new QPushButton(buttonText);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(themeIcon({"preferences-system-backup", "document-save",
                               "drive-harddisk"}));
    innerH->addWidget(button, 0, Qt::AlignVCenter);

    return frame;
}

// Bottom troubleshooting / recovery task
QWidget *ActionCenterPage::buildBottomTask(
    std::initializer_list<const char *> iconNames,
    const QString &title, const QString &description)
{
    auto *w = new QWidget;
    w->setStyleSheet("background: transparent;");
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);

    auto *icon = new QLabel;
    icon->setFixedSize(40, 40);
    icon->setPixmap(themeIcon(iconNames).pixmap(38, 38));
    icon->setStyleSheet("background: transparent;");
    h->addWidget(icon, 0, Qt::AlignTop);

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 2, 0, 0);
    col->setSpacing(2);
    col->addWidget(bodyLabel(title, /*link=*/true));
    col->addWidget(bodyLabel(description));
    h->addLayout(col, 1);

    return w;
}

// Page
ActionCenterPage::ActionCenterPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const AcInfo info = gatherInfo();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page heading
    contentV->addWidget(
        Win7::pageTitle("Review recent messages and resolve problems", 13));
    contentV->addSpacing(8);

    auto *blurb = bodyLabel(
        "Action Center has detected one or more issues for you to review.");
    contentV->addWidget(blurb);
    contentV->addSpacing(14);

    // ---- Security section -------------------------------------------------
    auto *securityRows = new QWidget;
    securityRows->setStyleSheet("background: transparent;");
    {
        auto *v = new QVBoxLayout(securityRows);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(14);
        v->addWidget(buildStatusRow(
            "Network firewall", info.firewallOn ? "On" : "Off",
            info.firewallOn
                ? Branding::brand("Linux Firewall (ufw) is actively "
                                  "protecting your computer.")
                : Branding::brand("Linux Firewall (ufw) is turned off.")));
        v->addWidget(buildStatusRow(
            Branding::brand("Linux Update"), "On",
            Branding::brand("Linux will automatically install updates as they "
                            "become available.")));
        v->addWidget(buildStatusRow(
            "Spyware and unwanted software protection",
            info.avPresent ? "On" : "Off",
            info.avPresent
                ? QStringLiteral("%1 reports that it is turned on.")
                      .arg(info.avName)
                : QStringLiteral("No antivirus program is installed."),
            "View installed antivirus programs"));
        v->addWidget(buildStatusRow(
            "Internet security settings", "OK",
            "All Internet security settings are set to their recommended "
            "levels."));
        v->addWidget(buildStatusRow(
            "User Account Control", info.uacOn ? "On" : "Off",
            info.uacOn
                ? "polkit will prompt for authentication when programs try to "
                  "make administrative changes."
                : "polkit authentication is not available.",
            "Change settings"));
        v->addWidget(buildStatusRow(
            "Network Access Protection", "Off",
            "Network Access Protection Agent service is not running.",
            "What is Network Access Protection?"));
    }
    contentV->addWidget(buildSection("Security", /*expanded=*/false,
                                     securityRows));

    // ---- Maintenance section ---------------------------------------------
    auto *maintRows = new QWidget;
    maintRows->setStyleSheet("background: transparent;");
    {
        auto *v = new QVBoxLayout(maintRows);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(14);
        v->addWidget(buildStatusRow(
            "Check for solutions to problem reports", "Off",
            "Problem reporting is turned off."));
        v->addWidget(buildStatusRow(
            "Backup", "Not set up",
            "Your files are not being backed up."));
        v->addWidget(buildStatusRow(
            "Check for updates", "No action needed",
            Branding::brand("Linux Update does not require any action.")));
        v->addWidget(buildStatusRow(
            "Troubleshooting: System Maintenance", "No action needed",
            "The system is actively checking for maintenance problems."));
    }
    contentV->addWidget(buildSection("Maintenance", /*expanded=*/false,
                                     maintRows));
    contentV->addSpacing(14);

    // The Set up backup alert box shows regardless of section state, as the
    // Windows Action Center keeps outstanding "action needed" items visible.
    contentV->addWidget(buildAlertBox(
        "Set up backup", "Your files are not being backed up.",
        "Set up backup", "Turn off messages about Backup"));
    contentV->addSpacing(24);

    // ---- Bottom "If you don't see your problem listed" --------------------
    auto *bottomIntro = bodyLabel(
        "If you don't see your problem listed, try one of these:");
    contentV->addWidget(bottomIntro);
    contentV->addSpacing(12);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(40);
    bottomRow->addWidget(buildBottomTask(
        {"tools-report-bug", "system-run", "preferences-system"},
        "Troubleshooting", "Find and fix problems"), 0, Qt::AlignTop);
    bottomRow->addWidget(buildBottomTask(
        {"document-revert", "chronometer", "system-reboot"},
        "Recovery", "Restore your computer to an earlier time"), 0,
        Qt::AlignTop);
    bottomRow->addStretch(1);
    contentV->addLayout(bottomRow);

    contentV->addStretch(1);
}
