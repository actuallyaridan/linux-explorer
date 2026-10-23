#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"
#include <QList>
#include <QHash>

class QScrollArea;
class QVBoxLayout;
class QLabel;

// The "Fonts" detail page (Appearance and Personalization), a Control-Panel
// rendering of the Windows 7 Fonts folder.
//
// The installed font families come from `fc-list`; each row previews the family
// in its own typeface. Double-clicking a family opens a representative face in
// KDE's font viewer (`kfontview`), and "Font settings" opens the UI-fonts KCM.
class FontsPage : public QWidget {
    Q_OBJECT

public:
    explicit FontsPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

protected:
    // Row clicks select a family; a double-click opens it in the font viewer.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Family {
        QString name;
        QString file;   // a representative font file, opened by kfontview
    };

    static QList<Family> gatherFamilies();

    void selectRow(int index);
    void openSelected();

    QList<Family>         m_families;
    int                   m_selected = -1;
    QHash<QObject *, int> m_rowToIndex;
    QList<QWidget *>      m_rows;
};
