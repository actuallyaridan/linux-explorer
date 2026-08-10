#include "PreviewPane.h"
#include "Win7Ui.h"

#include <KIO/Global>
#include <KIO/PreviewJob>

#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

namespace {

// The pane's default width, and the size previews are requested at; Windows
// renders into roughly a 256px box.
constexpr int kPaneWidth = 250;
constexpr int kPreviewSize = 220;

} // namespace

PreviewPane::PreviewPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("win7PreviewPane"));
    setStyleSheet("#win7PreviewPane { background: #FFFFFF; "
                  "border-left: 1px solid #D9D9D9; }");
    setMinimumWidth(160);
    resize(kPaneWidth, height());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 16, 12, 12);
    root->setSpacing(10);

    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumHeight(kPreviewSize);
    m_image->setStyleSheet("background: transparent;");
    root->addWidget(m_image, 0, Qt::AlignHCenter);

    m_name = Win7::label(QString(), 9, "#1F1F1F");
    m_name->setWordWrap(true);
    m_name->setAlignment(Qt::AlignHCenter);
    root->addWidget(m_name);

    m_detail = Win7::label(QString(), 9, "#5A5A5A");
    m_detail->setWordWrap(true);
    m_detail->setAlignment(Qt::AlignHCenter);
    root->addWidget(m_detail);

    root->addStretch(1);
    showPlaceholder(tr("Select a file to preview."));
}

void PreviewPane::showPlaceholder(const QString &text)
{
    m_image->setPixmap(QPixmap());
    m_name->setText(text);
    m_detail->clear();
}

void PreviewPane::setItems(const QList<KFileItem> &items)
{
    if (items.isEmpty()) {
        m_pending.clear();
        showPlaceholder(tr("Select a file to preview."));
        return;
    }
    if (items.size() > 1) {
        m_pending.clear();
        showPlaceholder(tr("%1 items selected.").arg(items.size()));
        return;
    }

    const KFileItem &item = items.first();
    m_pending = item.url();

    m_name->setText(item.text());
    m_detail->setText(item.isDir()
        ? item.mimeComment()
        : QStringLiteral("%1  •  %2").arg(item.mimeComment(),
                                          KIO::convertSize(item.size())));

    // Up immediately so the pane is never blank while the preview renders; the
    // thumbnail replaces it if one arrives.
    m_image->setPixmap(QIcon::fromTheme(item.iconName()).pixmap(64, 64));

    KIO::PreviewJob *job = new KIO::PreviewJob(
        KFileItemList({item}), QSize(kPreviewSize, kPreviewSize));
    job->setIgnoreMaximumSize(false);
    job->setScaleType(KIO::PreviewJob::ScaledAndCached);

    connect(job, &KIO::PreviewJob::gotPreview, this,
            [this](const KFileItem &previewed, const QPixmap &pixmap) {
        // A slow preview landing late would otherwise draw the previous file
        // over the current one.
        if (previewed.url() == m_pending)
            m_image->setPixmap(pixmap);
    });
    // No handler for failed(): the icon above is already the fallback, and more
    // informative than an error would be.
    job->start();
}
