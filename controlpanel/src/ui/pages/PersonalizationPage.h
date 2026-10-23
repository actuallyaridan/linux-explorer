#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"
#include <QList>
#include <QHash>
#include <QColor>

class QScrollArea;
class QGridLayout;
class QFrame;
class QPixmap;

// The "Personalization" detail page (Appearance and Personalization), a
// Control-Panel rendering of the Windows 7 theme picker.
//
// Each "theme" is a real KDE colour scheme discovered under the system and user
// color-schemes directories. A swatch is painted from the scheme's own window /
// titlebar / selection colours, and clicking one applies it live with
// `plasma-apply-colorscheme` (no privileges needed, it writes the user's own
// kdeglobals). The bottom row hands off to the matching KDE modules for
// wallpaper, colours, sounds and the screen locker.
class PersonalizationPage : public QWidget {
    Q_OBJECT

public:
    explicit PersonalizationPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

protected:
    // Clicking a swatch frame applies that scheme.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Scheme {
        QString id;         // file basename, the id plasma-apply-colorscheme wants
        QString name;       // display name from the scheme's [General] Name
        QColor  window;     // window background
        QColor  view;       // content/view background
        QColor  titlebar;   // active window titlebar
        QColor  accent;     // selection colour
        QColor  text;       // normal foreground
    };

    static QList<Scheme> gatherSchemes();
    static QString        currentSchemeId();

    QPixmap swatchPixmap(const Scheme &s) const;
    void    applyScheme(int index);
    void    refreshHighlight();

    QList<Scheme>         m_schemes;
    QString               m_currentId;
    QList<QFrame *>       m_frames;
    QHash<QObject *, int> m_frameToIndex;
};
