#pragma once

#include <QWidget>
#include <QString>
#include <QHash>

#include "Categories.h"

class QLabel;
class QVBoxLayout;
class QEvent;

class CategoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit CategoryWidget(const CategoryItem &item, QWidget *parent = nullptr);

Q_SIGNALS:
    void titleActivated(const QString &title);
    void taskActivated(const QString &category, const QString &task);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString  m_title;
    QLabel  *m_titleLabel = nullptr;
    // Task sub-links, mapped to their label text so a click can report which
    // task was activated.
    QHash<QLabel *, QString> m_taskLabels;
};
