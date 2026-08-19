#pragma once

#include <QDialog>

// About Explorer, opened by the command bar's Help button
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
