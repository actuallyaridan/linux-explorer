#pragma once

#include <QList>
#include <QModelIndex>
#include <QWidget>

#include <KFileItem>

class QLabel;

// Windows 7's details pane, along the bottom of the window: a summary of the
// folder when nothing is selected ("14 items"), and of the selection when
// something is ("3 items selected", with the combined size).
class DetailsPane : public QWidget {
    Q_OBJECT

public:
    explicit DetailsPane(QWidget *parent = nullptr);

    // `freeSpace` is the note about the volume the folder sits on, which Windows
    // keeps on show whenever nothing is selected. Empty for anything with no
    // volume behind it, leaving only the count.
    void showFolderSummary(int itemCount, const QString &freeSpace = QString());
    void showSelection(const QList<KFileItem> &items);

    // A drive picked on the Computer page. A ComputerModel index rather than a
    // KFileItem: its figures are model roles, not anything KFileItem reports.
    void showDrive(const QModelIndex &index);

    // Listing errors land here rather than in a status bar, Win7's Explorer
    // having none. Overwritten by the next selection change or load.
    void showMessage(const QString &message);

private:
    // Measured from a Win7 window: a 32px icon with two 9pt lines beside it.
    static constexpr int kPaneHeight = 52;

    void setContent(const QIcon &icon, const QString &primary,
                    const QString &secondary);

    QLabel *m_icon = nullptr;
    QLabel *m_primary = nullptr;
    QLabel *m_secondary = nullptr;
};
