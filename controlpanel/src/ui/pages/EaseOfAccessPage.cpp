#include "EaseOfAccessPage.h"
#include "Commands.h"
#include "LinkLabel.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>

// The KDE modules the tools and settings hand off to.
static const QStringList kAccess      = { "kcmshell6", "kcm_access" };
static const QStringList kMagnifier   = { "kcmshell6", "kcm_kwin_effects" };
static const QStringList kHighContrast = { "kcmshell6", "kcm_colors" };
static const QStringList kMouse       = { "kcmshell6", "kcm_mouse" };
static const QStringList kCursorTheme = { "kcmshell6", "kcm_cursortheme" };

// Sidebar
QList<SidebarLink> EaseOfAccessPage::sidebarLinks()
{
    return {};
}

QList<SidebarLink> EaseOfAccessPage::sidebarSeeAlso()
{
    return {
        Nav::to("Personalization", PageId::Personalization),
        Nav::command("Display", kcm("kcm_kscreen")),
    };
}

void EaseOfAccessPage::addSettingLink(QVBoxLayout *into, const QString &iconName,
                                      const QString &text, const QStringList &cmd)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    auto *icon = new QLabel;
    icon->setFixedSize(24, 24);
    icon->setPixmap(themeIcon({iconName.toUtf8().constData(),
                               "preferences-desktop-accessibility"}).pixmap(24, 24));
    icon->setStyleSheet("background: transparent;");
    row->addWidget(icon, 0, Qt::AlignVCenter);

    auto *link = new LinkLabel(text);
    QObject::connect(link, &LinkLabel::clicked, this, [this, cmd]() {
        launchDetached(this, cmd);
    });
    row->addWidget(link, 0, Qt::AlignVCenter);
    row->addStretch(1);

    into->addLayout(row);
    into->addSpacing(10);
}

// Page
EaseOfAccessPage::EaseOfAccessPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page title.
    contentV->addWidget(Win7::pageTitle("Make your computer easier to use"));
    contentV->addSpacing(16);

    auto addHeading = [&](const QString &text) {
        contentV->addLayout(
            Win7::sectionHeading(text, nullptr, nullptr, "#000000"));
        contentV->addSpacing(10);
    };

    // Quick access to common tools
    addHeading("Quick access to common tools");

    auto *intro = Win7::label(
        "You can use the tools in this section to help you get started.",
        9, "#333333");
    intro->setWordWrap(true);
    contentV->addWidget(intro);
    contentV->addSpacing(12);

    // 2x2 grid of tool tiles.
    auto *grid = new QGridLayout;
    grid->setContentsMargins(6, 0, 0, 0);
    grid->setHorizontalSpacing(28);
    grid->setVerticalSpacing(14);

    auto makeTile = [&](const QString &iconName, const QString &text,
                        const QStringList &cmd) {
        auto *tile = new QHBoxLayout;
        tile->setContentsMargins(0, 0, 0, 0);
        tile->setSpacing(10);

        auto *icon = new QLabel;
        icon->setFixedSize(32, 32);
        icon->setPixmap(themeIcon({iconName.toUtf8().constData(),
                                   "preferences-desktop-accessibility"}).pixmap(32, 32));
        icon->setStyleSheet("background: transparent;");
        tile->addWidget(icon, 0, Qt::AlignVCenter);

        auto *link = new LinkLabel(text);
        QObject::connect(link, &LinkLabel::clicked, this, [this, cmd]() {
            launchDetached(this, cmd);
        });
        tile->addWidget(link, 0, Qt::AlignVCenter);
        tile->addStretch(1);
        return tile;
    };

    grid->addLayout(makeTile("zoom-in", "Start Magnifier", kMagnifier), 0, 0);
    grid->addLayout(makeTile("preferences-desktop-keyboard",
                             "Start On-Screen Keyboard", kAccess), 0, 1);
    grid->addLayout(makeTile("audio-volume-high", "Start Narrator", kAccess), 1, 0);
    grid->addLayout(makeTile("preferences-desktop-theme",
                             "Set up High Contrast", kHighContrast), 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    contentV->addLayout(grid);
    contentV->addSpacing(20);

    // Explore all settings
    addHeading("Explore all settings");

    auto *explore = Win7::label(
        "When you select these settings, they will automatically start each time "
        "you sign in.", 9, "#333333");
    explore->setWordWrap(true);
    contentV->addWidget(explore);
    contentV->addSpacing(12);

    auto *list = new QVBoxLayout;
    list->setContentsMargins(6, 0, 0, 0);
    list->setSpacing(0);

    addSettingLink(list, "preferences-desktop-accessibility",
                   "Use the computer without a display", kAccess);
    addSettingLink(list, "zoom-in",
                   "Make the computer easier to see", kMagnifier);
    addSettingLink(list, "input-mouse",
                   "Use the computer without a mouse or keyboard", kAccess);
    addSettingLink(list, "input-mouse",
                   "Make the mouse easier to use", kMouse);
    addSettingLink(list, "input-keyboard",
                   "Make the keyboard easier to use", kAccess);
    addSettingLink(list, "audio-volume-high",
                   "Use text or visual alternatives for sounds", kAccess);
    addSettingLink(list, "preferences-desktop-cursors",
                   "Change the size of mouse pointers", kCursorTheme);

    contentV->addLayout(list);
    contentV->addStretch(1);
}
