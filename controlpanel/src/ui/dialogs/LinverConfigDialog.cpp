#include "LinverConfigDialog.h"
#include "Branding.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QIcon>

LinverConfigDialog::LinverConfigDialog(const QString &distroName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Control Panel Settings"));
    setWindowIcon(Branding::windowsLogo());
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // ---- Branding style row ----------------------------------------------
    auto *styleRow = new QHBoxLayout;
    styleRow->setSpacing(10);
    styleRow->addWidget(new QLabel(QStringLiteral("Branding style to use:")));

    m_styleCombo = new QComboBox;
    // Index order mirrors Branding::Style: 0 = distro, 1 = Windows 7.
    m_styleCombo->addItem(distroName.isEmpty() ? QStringLiteral("Linux")
                                               : distroName);
    m_styleCombo->addItem(QStringLiteral("Windows 7"));
    m_styleCombo->setCurrentIndex(
        Branding::style() == Branding::Style::Windows7 ? 1 : 0);
    styleRow->addWidget(m_styleCombo, 1);
    root->addLayout(styleRow);

    // ---- Toggles ----------------------------------------------------------
    // Choosing the Windows 7 style already swaps in the fake edition/logo, so no
    // separate "fake version" toggle is needed. This switch renames every
    // "Linux X" feature mention to "Windows X" and is independent of the style.
    m_windowsNames = new QCheckBox(
        QStringLiteral("Use Windows names for system features "
                       "(Update, Firewall, and so on)"));
    m_windowsNames->setChecked(Branding::useWindowsNames());
    root->addWidget(m_windowsNames);

    // ---- Customize hint + (stub) button ----------------------------------
    auto *custRow = new QHBoxLayout;
    custRow->setSpacing(10);
    auto *hint = new QLabel(
        QStringLiteral("To customize texts, branding image and\n"
                       "other options, click customize."));
    custRow->addWidget(hint, 1);

    auto *customize = new QPushButton(QStringLiteral("Customize..."));
    customize->setEnabled(false);   // reserved for a future customization panel
    custRow->addWidget(customize, 0, Qt::AlignBottom);
    root->addLayout(custRow);

    // ---- OK / Cancel ------------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyToSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    setSizeGripEnabled(false);
    setFixedSize(sizeHint().expandedTo(QSize(420, 0)));
}

void LinverConfigDialog::applyToSettings()
{
    Branding::setStyle(m_styleCombo->currentIndex() == 1
                           ? Branding::Style::Windows7
                           : Branding::Style::Distro);
    Branding::setUseWindowsNames(m_windowsNames->isChecked());
}
