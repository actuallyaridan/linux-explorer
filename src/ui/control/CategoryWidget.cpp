#include "ui/control/CategoryWidget.h"
#include "ui/control/IconHelper.h"
#include "data/control/Branding.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QFont>
#include <QCursor>
#include <QStyle>
#include <QEvent>
#include <QMouseEvent>
#include "aero/palette.h"
#include "aero/text.h"

namespace ControlPanel {

CategoryWidget::CategoryWidget(const CategoryItem &item, QWidget *parent)
    : QWidget(parent)
    , m_title(item.title)
{
    setContentsMargins(0, 0, 0, 0);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 4, 10, 4);
    root->setSpacing(14);
    root->setAlignment(Qt::AlignTop);

    // Icon
    auto *iconLabel = new QLabel;
    iconLabel->setPixmap(resolveIcon(item.iconName).pixmap(48, 48));
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    root->addWidget(iconLabel, 0, Qt::AlignTop);

    // Text block
    auto *textBlock = new QVBoxLayout;
    textBlock->setSpacing(0);
    textBlock->setContentsMargins(0, 0, 0, 0);

    // Display the branded wording; m_title stays canonical for routing
    auto *titleLabel = new QLabel(Branding::brand(item.title));
    Aero::setPointSize(titleLabel, 11);
    titleLabel->setCursor(Qt::PointingHandCursor);
    titleLabel->setStyleSheet(
        "QLabel { color: #1E7B1E; }"
        "QLabel:hover { color: #145214; }"
    );
    titleLabel->installEventFilter(this);
    m_titleLabel = titleLabel;
    textBlock->addWidget(titleLabel);

    for (const QString &task : item.tasks) {
        auto *taskLabel = new QLabel(Branding::brand(task));
        Aero::setPointSize(taskLabel, 9);
        taskLabel->setCursor(Qt::PointingHandCursor);
        taskLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; }"
            "QLabel:hover { color: %2; }")
                .arg(QLatin1String(Aero::Palette::LinkText),
                     QLatin1String(Aero::Palette::LinkHover))
        );
        taskLabel->installEventFilter(this);
        m_taskLabels.insert(taskLabel, task);
        textBlock->addWidget(taskLabel);
    }

    root->addLayout(textBlock, 1);
    setLayout(root);
}

bool CategoryWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *label = qobject_cast<QLabel *>(watched)) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            QFont font = label->font();
            font.setUnderline(event->type() == QEvent::Enter);
            label->setFont(font);
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                if (label == m_titleLabel) {
                    Q_EMIT titleActivated(m_title);
                } else {
                    auto it = m_taskLabels.constFind(label);
                    if (it != m_taskLabels.constEnd())
                        Q_EMIT taskActivated(m_title, it.value());
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace ControlPanel
