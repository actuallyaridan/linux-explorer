#include "UserAccountsPage.h"
#include "Commands.h"
#include "LinkLabel.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QFile>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <vector>

// Data gathering
UserAccountsPage::Account UserAccountsPage::gatherAccount()
{
    Account a;

    const uid_t uid = getuid();
    const passwd *pw = getpwuid(uid);
    if (pw) {
        a.userName = QString::fromLocal8Bit(pw->pw_name);
        // GECOS is a comma-separated list; the first field is the full name.
        a.fullName = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0);

        // The account is an "Administrator" if it belongs to the wheel or sudo
        // group, the standard admin groups a polkit/sudo policy grants rights to.
        int ngroups = 0;
        getgrouplist(pw->pw_name, pw->pw_gid, nullptr, &ngroups);
        if (ngroups > 0) {
            std::vector<gid_t> gids(ngroups);
            if (getgrouplist(pw->pw_name, pw->pw_gid, gids.data(), &ngroups) != -1) {
                for (gid_t g : gids) {
                    if (const group *gr = getgrgid(g)) {
                        const QString name = QString::fromLocal8Bit(gr->gr_name);
                        if (name == QLatin1String("wheel")
                            || name == QLatin1String("sudo")) {
                            a.accountType = QStringLiteral("Administrator");
                            break;
                        }
                    }
                }
            }
        }
    }
    if (a.userName.isEmpty())
        a.userName = QString::fromLocal8Bit(qgetenv("USER"));
    if (a.fullName.isEmpty())
        a.fullName = a.userName;
    if (a.accountType.isEmpty())
        a.accountType = QStringLiteral("Standard user");

    // The account picture: user dotfiles first, then the AccountsService icon
    // that KDE/GDM login screens use.
    const QString home = QString::fromLocal8Bit(qgetenv("HOME"));
    const QStringList candidates = {
        home + QStringLiteral("/.face.icon"),
        home + QStringLiteral("/.face"),
        QStringLiteral("/var/lib/AccountsService/icons/") + a.userName,
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            a.picturePath = path;
            break;
        }
    }

    return a;
}

// Sidebar
QList<SidebarLink> UserAccountsPage::sidebarLinks()
{
    return {
        Nav::command("Manage another account", kcm("kcm_users")),
        Nav::command("Change User Account Control settings", kcm("kcm_users")),
    };
}

QList<SidebarLink> UserAccountsPage::sidebarSeeAlso()
{
    return {
        Nav::plain("Parental Controls"),
        Nav::command("Credential Manager", kcm("kcm_kwallet5")),
    };
}

// Render the avatar as a rounded, framed thumbnail; fall back to a theme icon
// when the user has no picture of their own.
static QPixmap avatarPixmap(const QString &path, int size)
{
    QPixmap src;
    if (!path.isEmpty())
        src.load(path);
    if (src.isNull())
        src = themeIcon({"user-identity", "avatar-default",
                         "system-users"}).pixmap(size, size);

    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), 6, 6);
    p.setClipPath(clip);
    const QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    p.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);

    p.setClipping(false);
    p.setPen(QPen(QColor("#9DA7B5"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), 6, 6);
    return out;
}

// Page
UserAccountsPage::UserAccountsPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    const Account acct = gatherAccount();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page title.
    contentV->addWidget(Win7::pageTitle("Make changes to your user account"));
    contentV->addSpacing(18);

    // Body: task links on the left, the account summary card on the right.
    auto *body = new QHBoxLayout;
    body->setContentsMargins(6, 0, 0, 0);
    body->setSpacing(24);

    auto *tasks = new QVBoxLayout;
    tasks->setContentsMargins(0, 0, 0, 0);
    tasks->setSpacing(12);

    // All of these edits are handled by KDE's user manager, which prompts for
    // authorization itself.
    auto addTask = [&](const QString &text) {
        auto *link = new LinkLabel(text);
        QObject::connect(link, &LinkLabel::clicked, this, [this]() {
            launchDetached(this, { "kcmshell6", "kcm_users" });
        });
        tasks->addWidget(link, 0, Qt::AlignLeft);
    };

    addTask("Change your password");
    addTask("Change your picture");
    addTask("Change your account name");
    addTask("Change your account type");
    addTask("Manage another account");
    addTask("Change User Account Control settings");

    tasks->addStretch(1);
    body->addLayout(tasks, 0);

    // Account summary card: avatar, then name / type / password state.
    auto *card = new QHBoxLayout;
    card->setContentsMargins(0, 0, 0, 0);
    card->setSpacing(14);

    auto *avatar = new QLabel;
    avatar->setFixedSize(96, 96);
    avatar->setPixmap(avatarPixmap(acct.picturePath, 96));
    avatar->setStyleSheet("background: transparent;");
    card->addWidget(avatar, 0, Qt::AlignTop);

    auto *summary = new QVBoxLayout;
    summary->setContentsMargins(0, 2, 0, 0);
    summary->setSpacing(2);
    summary->addWidget(Win7::label(acct.fullName, 11, "#1A3C7A"));
    summary->addWidget(Win7::label(acct.accountType));
    summary->addWidget(Win7::label("Password protected"));
    summary->addStretch(1);
    card->addLayout(summary, 0);

    body->addLayout(card, 0);
    body->addStretch(1);

    contentV->addLayout(body);
    contentV->addStretch(1);
}
