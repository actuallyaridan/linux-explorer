#include "ui/control/pages/ActionCenterPage.h"
#include "ui/control/Commands.h"
#include "data/control/ConfFile.h"
#include "ui/control/IconHelper.h"
#include "ui/control/Win7Ui.h"
#include "data/control/Branding.h"

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
#include "aero/palette.h"

namespace ControlPanel {

using Win7::ClickableWidget;

// Data gathering
ActionCenterPage::AcInfo ActionCenterPage::gatherInfo()
{
    AcInfo ac;

    ac.firewallOn = readConfField(QStringLiteral("/etc/ufw/ufw.conf"),
                                  QStringLiteral("ENABLED")).compare(
                                      QStringLiteral("yes"), Qt::CaseInsensitive) == 0;

    // "Spyware and unwanted software protection" maps to an on-demand scanner.
    // ClamAV is the ubiquitous Linux one; report it when its binary is present
    if (!QStandardPaths::findExecutable(QStringLiteral("clamscan")).isEmpty()) {
        ac.avPresent = true;
        ac.avName = QStringLiteral("ClamAV");
    }

    // UAC maps to polkit: administrative actions prompt for authentication when
    // a polkit authority is registered on the system bus (it always is on KDE)
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

using Win7::wrappedLabel;

// Status row
QWidget *ActionCenterPage::buildStatusRow(const QString &item,
                                          const QString &state,
                                          const QString &description,
                                          const QString &link)
{
    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *column = new QVBoxLayout(row);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);

    // Top line: bold item name on the left, state right-aligned
    auto *topH = new QHBoxLayout;
    topH->setContentsMargins(0, 0, 0, 0);
    topH->setSpacing(8);

    auto *name = new QLabel(item);
    Aero::setPointSize(name, 9);
    name->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(QLatin1String(Aero::Palette::Text)));
    topH->addWidget(name, 0, Qt::AlignVCenter);
    topH->addStretch(1);

    auto *stateLabel = new QLabel(state);
    Aero::setPointSize(stateLabel, 9);
    stateLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(QLatin1String(Aero::Palette::Text)));
    topH->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
    column->addLayout(topH);

    // Description sits indented under the item name, as in the reference
    auto *desc = wrappedLabel(description);
    desc->setContentsMargins(18, 0, 0, 0);
    column->addWidget(desc);

    if (!link.isEmpty()) {
        auto *linkLabel = wrappedLabel(link, /*link=*/true);
        linkLabel->setContentsMargins(18, 0, 0, 0);
        column->addWidget(linkLabel);
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
    // right. The whole strip is clickable and toggles the rows below it
    auto *header = new ClickableWidget;
    header->setStyleSheet("background: transparent;");
    header->setCursor(Qt::PointingHandCursor);
    header->setMinimumHeight(36);
    auto *headerH = new QHBoxLayout(header);
    headerH->setContentsMargins(0, 4, 8, 4);
    headerH->setSpacing(0);

    headerH->addWidget(Aero::label(title, 13, "#1A5FB4"), 0, Qt::AlignVCenter);
    headerH->addStretch(1);

    // The shared round Aero expander. The whole header strip is the click
    // target, so the button itself lets clicks fall through to it
    auto *chevron = new Aero::ChevronButton;
    // The whole header strip takes the click, so this lets it through
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    chevron->setChecked(expanded);
    headerH->addWidget(chevron, 0, Qt::AlignVCenter);
    panelV->addWidget(header);

    // Thin separator directly under the header, always visible
    panelV->addWidget(Aero::hairline("#DCDCDC"));

    // Body carries a left inset so the rows line up under the title text
    rows->setContentsMargins(2, 12, 2, 12);
    panelV->addWidget(rows);

    // Clicking the header expands/collapses the rows and flips the chevron,
    // matching the Windows Action Center sections
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
    // than a thin accent frame). No icon, matching the Windows backup box
    auto *frame = new QFrame;
    frame->setObjectName("acAlert");
    frame->setMaximumWidth(620);
    frame->setStyleSheet(
        QStringLiteral("QFrame#acAlert {"
        "  background: %1;"
        "  border: 1px solid #CCCCCC;"
        "  border-left: 20px solid #D4A800;"
        "}"
        "QFrame#acAlert QLabel { background: transparent; border: none; }")
            .arg(QLatin1String(Aero::Palette::Surface)));

    auto *innerH = new QHBoxLayout(frame);
    innerH->setContentsMargins(12, 12, 12, 12);
    innerH->setSpacing(12);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(4);

    auto *titleLabel = new QLabel(title);
    {
        Aero::setPointSize(titleLabel, 9);
        Win7::setBold(titleLabel, true);
    }
    titleLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(QLatin1String(Aero::Palette::Text)));
    textCol->addWidget(titleLabel);
    textCol->addWidget(wrappedLabel(description));
    if (!link.isEmpty())
        textCol->addWidget(wrappedLabel(link, /*link=*/true));
    innerH->addLayout(textCol, 1);

    auto *button = new QPushButton(buttonText);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(Aero::themeIcon({"preferences-system-backup", "document-save",
                               "drive-harddisk"}));
    innerH->addWidget(button, 0, Qt::AlignVCenter);

    return frame;
}

// Bottom troubleshooting / recovery task
QWidget *ActionCenterPage::buildBottomTask(
    std::initializer_list<const char *> iconNames,
    const QString &title, const QString &description)
{
    auto *widget = new QWidget;
    widget->setStyleSheet("background: transparent;");
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    auto *icon = new QLabel;
    icon->setFixedSize(40, 40);
    icon->setPixmap(Aero::themeIcon(iconNames).pixmap(38, 38));
    icon->setStyleSheet("background: transparent;");
    row->addWidget(icon, 0, Qt::AlignTop);

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 2, 0, 0);
    col->setSpacing(2);
    col->addWidget(wrappedLabel(title, /*link=*/true));
    col->addWidget(wrappedLabel(description));
    row->addLayout(col, 1);

    return widget;
}

// Page
ActionCenterPage::ActionCenterPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const AcInfo info = gatherInfo();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page heading
    contentV->addWidget(
        Win7::pageTitle("Review recent messages and resolve problems", 13));
    contentV->addSpacing(8);

    auto *blurb = wrappedLabel(
        "Action Center has detected one or more issues for you to review.");
    contentV->addWidget(blurb);
    contentV->addSpacing(14);

    // Security section
    auto *securityRows = new QWidget;
    securityRows->setStyleSheet("background: transparent;");
    {
        auto *column = new QVBoxLayout(securityRows);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(14);

        // Each row's state and the sentence under it, which differ by whether
        // the protection is on
        QString firewallState = QStringLiteral("Off");
        QString firewallText = Branding::brand("Linux Firewall (ufw) is turned off.");
        if (info.firewallOn) {
            firewallState = QStringLiteral("On");
            firewallText = Branding::brand("Linux Firewall (ufw) is actively "
                                           "protecting your computer.");
        }

        QString spywareState = QStringLiteral("Off");
        QString spywareText = QStringLiteral("No antivirus program is installed.");
        if (info.avPresent) {
            spywareState = QStringLiteral("On");
            spywareText = QStringLiteral("%1 reports that it is turned on.")
                              .arg(info.avName);
        }

        QString uacState = QStringLiteral("Off");
        QString uacText = QStringLiteral("polkit authentication is not available.");
        if (info.uacOn) {
            uacState = QStringLiteral("On");
            uacText = QStringLiteral("polkit will prompt for authentication when "
                                     "programs try to make administrative changes.");
        }

        column->addWidget(buildStatusRow("Network firewall", firewallState,
                                         firewallText));
        column->addWidget(buildStatusRow(
            Branding::brand("Linux Update"), "On",
            Branding::brand("Linux will automatically install updates as they "
                            "become available.")));
        column->addWidget(buildStatusRow(
            "Spyware and unwanted software protection", spywareState,
            spywareText, "View installed antivirus programs"));
        column->addWidget(buildStatusRow(
            "Internet security settings", "OK",
            "All Internet security settings are set to their recommended "
            "levels."));
        column->addWidget(buildStatusRow("User Account Control", uacState, uacText,
                                         "Change settings"));
        column->addWidget(buildStatusRow(
            "Network Access Protection", "Off",
            "Network Access Protection Agent service is not running.",
            "What is Network Access Protection?"));
    }
    contentV->addWidget(buildSection("Security", /*expanded=*/false,
                                     securityRows));

    // Maintenance section
    auto *maintRows = new QWidget;
    maintRows->setStyleSheet("background: transparent;");
    {
        auto *column = new QVBoxLayout(maintRows);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(14);
        column->addWidget(buildStatusRow(
            "Check for solutions to problem reports", "Off",
            "Problem reporting is turned off."));
        column->addWidget(buildStatusRow(
            "Backup", "Not set up",
            "Your files are not being backed up."));
        column->addWidget(buildStatusRow(
            "Check for updates", "No action needed",
            Branding::brand("Linux Update does not require any action.")));
        column->addWidget(buildStatusRow(
            "Troubleshooting: System Maintenance", "No action needed",
            "The system is actively checking for maintenance problems."));
    }
    contentV->addWidget(buildSection("Maintenance", /*expanded=*/false,
                                     maintRows));
    contentV->addSpacing(14);

    // The Set up backup alert box shows regardless of section state, as the
    // Windows Action Center keeps outstanding "action needed" items visible
    contentV->addWidget(buildAlertBox(
        "Set up backup", "Your files are not being backed up.",
        "Set up backup", "Turn off messages about Backup"));
    contentV->addSpacing(24);

    // Bottom "If you don't see your problem listed"
    auto *bottomIntro = wrappedLabel(
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

} // namespace ControlPanel
