#pragma once

#include <QList>
#include <QModelIndex>
#include <QWidget>

#include <KFileItem>

class QLabel;

// Win7's details pane along the bottom, summarising the folder when nothing is
// selected and the selection when something is
class DetailsPane : public QWidget {
    Q_OBJECT

public:
    explicit DetailsPane(QWidget *parent = nullptr);

    // The free space line is empty for anything with no volume behind it
    void showFolderSummary(int itemCount, const QString &freeSpace = QString());
    void showSelection(const QList<KFileItem> &items);

    // A drive model index, whose figures are model roles rather than anything
    // a file item reports
    void showDrive(const QModelIndex &index);

    // Win7's Explorer has no status bar, so listing errors land here
    void showMessage(const QString &message);

private:
    // A 32px icon with two 9pt lines beside it
    static constexpr int kPaneHeight = 52;

    void setContent(const QIcon &icon, const QString &primary,
                    const QString &secondary);

    QLabel *m_icon = nullptr;
    QLabel *m_primary = nullptr;
    QLabel *m_secondary = nullptr;
};
