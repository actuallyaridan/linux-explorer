#include "AboutDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Plain labels at the application font, the chrome's text helpers being avoided
// here since they set a size and colour of their own
constexpr int kDialogWidth = 340;
constexpr int kIconSize = 64;

// The version this box reports, which is not the one the command line prints
const char *const kVersion = "Version: 0.1";

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About File Explorer"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedWidth(kDialogWidth);

    auto *iconLabel = new QLabel;
    iconLabel->setFixedSize(kIconSize, kIconSize);
    iconLabel->setPixmap(windowIcon().pixmap(kIconSize, kIconSize));
    iconLabel->setAlignment(Qt::AlignCenter);

    auto *nameLabel = new QLabel(QStringLiteral("File Explorer"));
    auto *companyLabel = new QLabel(QStringLiteral("@actuallyaridan"));
    auto *versionLabel = new QLabel(QLatin1String("Version: 0.1"));

    auto *infoLayout = new QVBoxLayout;
    infoLayout->addWidget(nameLabel);
    infoLayout->addWidget(companyLabel);
    infoLayout->addWidget(versionLabel);
    infoLayout->addStretch();

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(iconLabel);
    topLayout->addSpacing(8);
    topLayout->addLayout(infoLayout);
    topLayout->addStretch();

    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto *descLabel = new QLabel(
        QStringLiteral("You can use the File Explorer to browse the files and "
                       "folders on your computer, and to open, organize, copy, "
                       "move and search them."));
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descLabel->setContentsMargins(4, 4, 4, 4);

    auto *creditsLabel = new QLabel(
        QStringLiteral("Replicated in Linux with Qt6 and KDE Frameworks, with "
                       "every file operation handled by KIO. Best enjoyed with "
                       "AeroThemePlasma. Any Microsoft branding is used solely "
                       "for referential use only, and does not aim to usurp "
                       "copyrights from Microsoft."));
    creditsLabel->setWordWrap(true);
    creditsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    creditsLabel->setContentsMargins(4, 4, 4, 4);

    auto *okButton = new QPushButton(QStringLiteral("OK"));
    okButton->setFixedWidth(80);
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(separator);
    mainLayout->addWidget(descLabel);
    mainLayout->addWidget(creditsLabel);
    mainLayout->addSpacing(4);
    mainLayout->addLayout(buttonLayout);

    layout()->setSizeConstraint(QLayout::SetFixedSize);
}
