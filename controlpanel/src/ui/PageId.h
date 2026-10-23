#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <utility>

// ---------------------------------------------------------------------------
// Page identity.
//
// The Control Panel used to identify each detail page by its breadcrumb title
// string (e.g. "System and Security/Linux Update"), and sidebar links were
// plain display strings that MainWindow reverse-mapped back to those titles
// with a set of scattered look-up tables. That was fragile: renaming a page's
// visible title silently broke every link that pointed at it.
//
// PageId is the stable, canonical identity instead. The title/breadcrumb path
// is now just display metadata owned by PageRegistry (see PageRegistry.h), and
// sidebar links reference their destination *by id*, not by matching text.
// ---------------------------------------------------------------------------
enum class PageId {
    None,             // no page (a decorative/no-op link)
    Home,             // the Control Panel home view
    LinuxUpdate,
    SelectUpdates,    // the Linux Update "select updates to install" sub-view
    ProgramsFeatures,
    InstalledUpdates,
    NetworkSharing,
    Firewall,
    ActionCenter,
    PowerOptions,
    Performance,
    Personalization,
    Fonts,
    UserAccounts,
    EaseOfAccess,
    DevicesPrinters,
    System,
};

// Where a sidebar link goes when it is clicked. A link can navigate to another
// page (by id), return home, launch an external command, or open an in-app
// applet dialog. `None` is a decorative link that renders but does nothing.
struct LinkTarget {
    enum Kind { None, Home, Page, Command, Applet };

    Kind        kind = None;
    PageId      page = PageId::None;   // used when kind == Page
    QStringList command;               // used when kind == Command
    QString     applet;                // used when kind == Applet
};

// One entry in a page's left-nav sidebar: the text to show plus its target.
struct SidebarLink {
    QString    text;
    LinkTarget target;
};

// Concise constructors so a page can declare its sidebar as plain data, e.g.
//   return {
//       Nav::plain("Turn Linux Firewall on or off"),
//       Nav::to("Network and Sharing Center", PageId::NetworkSharing),
//       Nav::command("Change adapter settings", kcm("kcm_networkmanagement")),
//   };
namespace Nav {

// A link that navigates to another detail page.
inline SidebarLink to(const QString &text, PageId page)
{
    LinkTarget t;
    t.kind = LinkTarget::Page;
    t.page = page;
    return { text, t };
}

// A link back to the Control Panel home view.
inline SidebarLink home(const QString &text)
{
    LinkTarget t;
    t.kind = LinkTarget::Home;
    return { text, t };
}

// A link that launches an external program (e.g. a KDE settings module).
inline SidebarLink command(const QString &text, QStringList cmd)
{
    LinkTarget t;
    t.kind    = LinkTarget::Command;
    t.command = std::move(cmd);
    return { text, t };
}

// A link that opens an in-app applet dialog, e.g. "datetime" or "sound".
inline SidebarLink applet(const QString &text, QString id)
{
    LinkTarget t;
    t.kind   = LinkTarget::Applet;
    t.applet = std::move(id);
    return { text, t };
}

// A decorative link that renders like the rest but has no destination yet.
inline SidebarLink plain(const QString &text)
{
    return { text, LinkTarget{} };
}

} // namespace Nav
