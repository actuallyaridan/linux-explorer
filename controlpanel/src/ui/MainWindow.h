#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QHash>
#include <QSoundEffect>
#include <QGraphicsOpacityEffect>
#include "PageId.h"

class QScrollArea;
class QLineEdit;
class QToolButton;
class QPushButton;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class CategoryWidget;
class LinuxUpdatePage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The shared chrome of a left-nav pane: a width-clipped scroll area, the
    // grey navPane frame, and the text layout (already seeded with the
    // "Control Panel Home" link) that callers fill with their own entries.
    struct Sidebar {
        QScrollArea *clip;
        QWidget     *textWrap;
        QVBoxLayout *navV;
    };
    Sidebar buildSidebarShell(int initialWidth);

    void buildCrumbBar();
    QWidget *buildHomePage();
    QWidget *buildCategoryPage(const QString &category);
    QScrollArea *buildNavSidebar(const QString &currentCategory);
    QScrollArea *buildSubpageSidebar(const QList<SidebarLink> &links,
                                     const QList<SidebarLink> &seeAlso = {});

    // Point a freshly built sidebar label at the destination its link declares
    // (another page, home, an external command, or an in-app applet).
    void registerLinkTarget(QLabel *label, const LinkTarget &target);

    void setCrumbTrail(const QStringList &trail);
    void navigateHome();
    void navigateTo(const QString &path);

    // Open an in-app applet dialog (e.g. "datetime", "datetime:additional"),
    // the Windows-style modal popup for a Control Panel item.
    void openApplet(const QString &id);

    // History-aware rendering. An empty string represents the home page.
    void pushHistory(const QString &entry);
    void showEntry(const QString &entry);
    void goBack();
    void goForward();
    void updateNavButtons();

    QWidget     *m_crumbBar;
    QHBoxLayout *m_pathLayout = nullptr;
    QLineEdit   *m_searchBox;
    QScrollArea *m_scroll;

    QPushButton *m_backBtn = nullptr;
    QPushButton *m_forwardBtn = nullptr;

    QStringList m_history;
    int         m_historyIndex = -1;

    // Maps each left-nav link label to the category it navigates to.
    QHash<QObject *, QString> m_navLinks;

    // Maps group-title labels to full subpage paths (e.g. "System and Security/Linux Update").
    QHash<QObject *, QString> m_subpageLinks;

    // Maps task/title labels that launch an external program to the command to
    // run, stored as {program, arg, arg, ...} (e.g. the Desktop Gadgets links
    // that open KDE Plasma's widget panels).
    QHash<QObject *, QStringList> m_commandLinks;

    // Maps task/title labels to an in-app applet id opened as a modal dialog
    // (e.g. "datetime"). See openApplet.
    QHash<QObject *, QString> m_appletLinks;

    // Maps intermediate crumb labels to their navigation paths.
    QHash<QObject *, QString> m_crumbNavLinks;

    // "Control Panel" crumb; a QLabel (not a button) so AeroQt's glass-bar
    // QAbstractButton texture never applies. Clicks go to the home view.
    QLabel *m_homeCrumb = nullptr;

    QSoundEffect m_navSound;

    // Opacity effect on the current category sidebar's text wrap; used to fade
    // out the text before navigating to a subcategory.
    QGraphicsOpacityEffect *m_sidebarTextEffect = nullptr;

    // Active Linux Update page and its "Check for updates" sidebar label.
    LinuxUpdatePage *m_updatePage         = nullptr;
    QLabel            *m_checkUpdatesLabel  = nullptr;
};
