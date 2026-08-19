#include "taskdialog.h"

#include "palette.h"
#include "text.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Aero {

TaskDialog::TaskDialog(QWidget *parent, const QString &title, const QIcon &icon)
    : QDialog(parent)
{
    if (!title.isEmpty())
        setWindowTitle(title);
    setModal(true);
    setSizeGripEnabled(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("aeroTaskContent"));
    // ID scoped, a declaration only sheet matching everything and dragging
    // every field in the dialog into the stylesheet engine
    content->setStyleSheet(QStringLiteral("#aeroTaskContent { background: %1; }")
                               .arg(QLatin1String(Palette::Surface)));

    m_content = new QVBoxLayout(content);
    m_content->setContentsMargins(16, 16, 16, 16);
    m_content->setSpacing(10);

    if (!icon.isNull()) {
        // The warning shape, icon at the top left with the message beside it
        m_content->setContentsMargins(18, 18, 18, 18);

        auto *body = new QHBoxLayout;
        body->setSpacing(14);

        auto *iconLabel = new QLabel;
        iconLabel->setStyleSheet(QStringLiteral("background: transparent;"));
        iconLabel->setPixmap(icon.pixmap(32, 32));
        body->addWidget(iconLabel, 0, Qt::AlignTop);

        m_lines = new QVBoxLayout;
        m_lines->setSpacing(10);
        body->addLayout(m_lines, 1);

        m_content->addLayout(body);
    }

    root->addWidget(content, 1);

    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("aeroTaskFooter"));
    footer->setStyleSheet(QStringLiteral("#aeroTaskFooter { background: %1;"
                                         " border-top: 1px solid %2; }")
                              .arg(QLatin1String(Palette::FooterSurface),
                                   QLatin1String(Palette::FooterRule)));

    m_footer = new QHBoxLayout(footer);
    m_footer->setContentsMargins(12, 10, 12, 10);
    m_footer->setSpacing(8);
    m_footer->addStretch(1);

    root->addWidget(footer);
}

void TaskDialog::setContentMargins(const QMargins &margins)
{
    m_content->setContentsMargins(margins);
}

void TaskDialog::addLine(QLabel *line)
{
    line->setWordWrap(true);
    // Without an icon there is no stack, so fall back to the plain column
    (m_lines ? m_lines : m_content)->addWidget(line);
}

QPushButton *TaskDialog::addButton(const QString &text)
{
    auto *button = new QPushButton(text);
    setPointSize(button, 9);
    // Win7 draws no button narrower than 75px
    button->setMinimumWidth(75);
    m_footer->addWidget(button);
    return button;
}

void TaskDialog::addFooterWidget(QWidget *w)
{
    m_footer->insertWidget(m_footerLeft++, w);
}

void TaskDialog::lockSize(int width)
{
    setFixedWidth(width);
    layout()->activate();
    setFixedHeight(sizeHint().height());
}

} // namespace Aero
