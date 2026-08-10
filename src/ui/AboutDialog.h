#pragma once

#include <QDialog>

// "About Explorer", laid out like the Device Manager's About box so the two
// applications read as one family: icon and name block across the top, a rule,
// what the application is for, the credits, and OK.
//
// This is what the command bar's Help button opens.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
