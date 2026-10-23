#pragma once

#include <QWidget>
#include <QStringList>
#include "PageId.h"

class QScrollArea;
class QVBoxLayout;

// The "Ease of Access Center" detail page, a Control-Panel rendering of the
// Windows 7 Ease of Access Center.
//
// No standalone accessibility apps (magnifier, narrator, on-screen keyboard) are
// assumed to be installed, so, like the real Control Panel, whose entries open
// sub-dialogs, every tool and setting here opens the matching KDE settings
// module (kcm_access, kcm_kwin_effects for the screen magnifier, kcm_colors for
// high contrast, kcm_mouse, kcm_cursortheme), which are always present with
// Plasma.
class EaseOfAccessPage : public QWidget {
    Q_OBJECT

public:
    explicit EaseOfAccessPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // Append an icon + blue link that opens `cmd` when clicked.
    void addSettingLink(QVBoxLayout *into, const QString &iconName,
                        const QString &text, const QStringList &cmd);
};
