#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>
#include "ui/control/PageId.h"

class QGraphicsOpacityEffect;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace ControlPanel {

class LinuxUpdatePage;

// The Control Panel's pages, shown inside an Explorer window the way Win7
// shows them. It has no chrome of its own: the window around it owns the
// address bar, history and back button, and this only draws the page for a
// path and asks to go somewhere else when a link is clicked
//
// A path is a page's breadcrumb, such as "System and Security/System", and the
// empty path is the Control Panel's home page
class View : public QWidget {
    Q_OBJECT

public:
    explicit View(QWidget *parent = nullptr);

    // Draws the page for a path without asking anyone, which is what the
    // window calls once it has decided to go there, back and forward included
    void showPath(const QString &path);

    QString currentPath() const;

    // The page a path really shows: itself when it has a page, otherwise its
    // category, otherwise the home page. Typed or remembered paths can name a
    // page that does not exist, so the window passes them through this first
    static QString resolvePath(const QString &path);

    // How one segment of a path is shown, the address bar's crumbs included,
    // with "Linux" read as "Windows" when the branding says so
    static QString displayName(const QString &segment);

    // The categories under the home page, in the order its sidebar lists them,
    // for the address bar's arrow menu
    static QStringList categories();

signals:
    // A link was clicked. Nothing changes until the window calls showPath
    void pathRequested(const QString &path);

    // Linux Update's select view was closed, which Win7 treats as going back
    void backRequested();

    // The branding changed, so every displayed name, the address bar's
    // included, has to be drawn again
    void reloadRequested();

    // Folder Options belongs to Explorer, so the window opens it, on the View
    // tab for "view" and on the General tab otherwise
    void folderOptionsRequested(const QString &tab);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The shared chrome of a left-nav pane: a width-clipped scroll area, the
    // grey navPane frame, and the text layout (already seeded with the
    // "Control Panel Home" link) that callers fill with their own entries
    struct Sidebar {
        QScrollArea *clip;
        QWidget     *textWrap;
        QVBoxLayout *navV;
    };
    Sidebar buildSidebarShell(int initialWidth);

    QWidget *buildHomePage();
    QWidget *buildCategoryPage(const QString &category);
    QScrollArea *buildNavSidebar(const QString &currentCategory);
    QScrollArea *buildSubpageSidebar(const QList<SidebarLink> &links,
                                     const QList<SidebarLink> &seeAlso = {});

    // Point a freshly built sidebar label at the destination its link declares
    // (another page, home, an external command, or an in-app applet)
    void registerLinkTarget(QLabel *label, const LinkTarget &target);

    // What every link calls, asking the window to go to a path rather than
    // going there itself
    void requestPath(const QString &path);

    // Open an in-app applet dialog (e.g. "datetime", "datetime:additional"),
    // the Windows-style modal popup for a Control Panel item
    void openApplet(const QString &id);

    QScrollArea *m_scroll;

    // The path whose page is showing, kept so a reload can draw it again
    QString m_path;

    // Maps each left-nav link label to the category it navigates to
    QHash<QObject *, QString> m_navLinks;

    // Maps group-title labels to full subpage paths (e.g. "System and Security/Linux Update")
    QHash<QObject *, QString> m_subpageLinks;

    // Maps task/title labels that launch an external program to the command to
    // run, stored as {program, arg, arg, ...} (e.g. the Desktop Gadgets links
    // that open KDE Plasma's widget panels)
    QHash<QObject *, QStringList> m_commandLinks;

    // Maps task/title labels to an in-app applet id opened as a modal dialog
    // (e.g. "datetime"). See openApplet
    QHash<QObject *, QString> m_appletLinks;

    // Opacity effect on the current category sidebar's text wrap; used to fade
    // out the text before navigating to a subcategory
    QGraphicsOpacityEffect *m_sidebarTextEffect = nullptr;

    // Active Linux Update page and its "Check for updates" sidebar label
    LinuxUpdatePage *m_updatePage         = nullptr;
    QLabel            *m_checkUpdatesLabel  = nullptr;
};

} // namespace ControlPanel
