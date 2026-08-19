#include "DetailsPane.h"
#include "Assets.h"
#include "ComputerModel.h"
#include "aero/icons.h"
#include "aero/strips.h"
#include "aero/text.h"

#include <KIO/Global>

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

DetailsPane::DetailsPane(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    Aero::Strip *panel = Aero::statusPanel(Explorer::Art::DetailsBar, kPaneHeight);
    QHBoxLayout *bar = panel->row();

    m_icon = new QLabel;
    m_icon->setStyleSheet("background: transparent;");
    m_icon->setFixedSize(32, 32);
    m_icon->setAlignment(Qt::AlignCenter);
    bar->addWidget(m_icon, 0, Qt::AlignVCenter);

    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(1);
    m_primary = Aero::label(QString(), 9, Aero::Palette::SoftText);
    m_secondary = Aero::label(QString(), 9, Aero::Palette::MutedText);
    text->addWidget(m_primary);
    text->addWidget(m_secondary);
    bar->addLayout(text, 1);

    root->addWidget(panel);
}

void DetailsPane::setContent(const QIcon &icon, const QString &primary,
                             const QString &secondary)
{
    m_icon->setPixmap(icon.pixmap(32, 32));
    m_primary->setText(primary);
    m_secondary->setText(secondary);
}

void DetailsPane::showFolderSummary(int itemCount, const QString &freeSpace)
{
    setContent(Aero::themeIcon({"folder"}),
               itemCount == 1 ? tr("1 item")
                              : tr("%1 items").arg(itemCount),
               freeSpace);
}

void DetailsPane::showDrive(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QString name = index.data(Qt::DisplayRole).toString();

    QString detail = index.data(ComputerModel::RemovableRole).toBool()
        ? tr("Removable Disk") : tr("Local Disk");
    if (index.data(ComputerModel::SizeKnownRole).toBool()) {
        const auto total = index.data(ComputerModel::TotalSizeRole).toULongLong();
        const auto available = index.data(ComputerModel::AvailableSizeRole).toULongLong();
        // Both figures, as Win7 reports a drive
        detail += QStringLiteral("  •  %1 free of %2")
                      .arg(KIO::convertSize(available), KIO::convertSize(total));
    }

    setContent(icon, name, detail);
}

void DetailsPane::showMessage(const QString &message)
{
    setContent(Aero::themeIcon({"dialog-warning", "dialog-error"}), message, QString());
}

void DetailsPane::showSelection(const QList<KFileItem> &items)
{
    if (items.isEmpty())
        return;

    if (items.size() == 1) {
        const KFileItem &item = items.first();
        // A directory's size on disk is not the size of what it contains, so
        // Win7 shows the type instead
        const QString detail = item.isDir()
            ? item.mimeComment()
            : QStringLiteral("%1  •  %2").arg(item.mimeComment(),
                                              KIO::convertSize(item.size()));
        setContent(QIcon::fromTheme(item.iconName()), item.text(), detail);
        return;
    }

    KIO::filesize_t total = 0;
    bool anyDirs = false;
    for (const KFileItem &item : items) {
        if (item.isDir())
            anyDirs = true;
        else
            total += item.size();
    }

    // With a folder in the selection the total covers only the files
    const QString detail = anyDirs
        ? QStringLiteral("%1 (files only)").arg(KIO::convertSize(total))
        : KIO::convertSize(total);
    setContent(Aero::themeIcon({"edit-select-all", "folder"}),
               QStringLiteral("%1 items selected").arg(items.size()), detail);
}
