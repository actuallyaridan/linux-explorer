#pragma once

#include <QList>
#include <QWidget>

#include <KFileItem>

class QLabel;

// Windows 7's preview pane: the strip down the right-hand side showing a large
// rendering of whatever single item is selected. Toggled with Alt+P.
//
// The rendering is KIO::PreviewJob's, the same machinery behind the file list's
// thumbnails, so anything the desktop can preview appears here without this
// class knowing the formats. It falls back to the item's icon, as Windows does
// for a file type with no handler.
class PreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPane(QWidget *parent = nullptr);

    // Renders `items` when it holds exactly one entry; anything else gets a
    // placeholder, as in Windows.
    void setItems(const QList<KFileItem> &items);

private:
    void showPlaceholder(const QString &text);

    QLabel *m_image = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_detail = nullptr;

    // What the running preview job belongs to, so one landing after the
    // selection moved on can be discarded.
    QUrl m_pending;
};
