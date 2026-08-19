#pragma once

#include <QList>
#include <QWidget>

#include <KFileItem>

class QLabel;

// Win7's preview pane, whose rendering is the desktop's own, so anything it can
// preview appears here, falling back to the item's icon where it cannot
class PreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPane(QWidget *parent = nullptr);

    // Anything but a single item gets a placeholder, as in Windows
    void setItems(const QList<KFileItem> &items);

private:
    void showPlaceholder(const QString &text);

    QLabel *m_image = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_detail = nullptr;

    // So a job landing after the selection moved on can be discarded
    QUrl m_pending;
};
