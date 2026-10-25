#pragma once

#include <QDialog>
#include <QPointer>

#include <KFileItem>
#include <KService>

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace KIO {
class DirectorySizeJob;
}

// Win7's Properties for a single file or folder, laid out as Windows lays it
// out, with only the rows that mean something on Linux
class PropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PropertiesDialog(const KFileItem &item, QWidget *parent = nullptr);
    ~PropertiesDialog() override;

private:
    QWidget *buildGeneralTab();

    // The icon and the name box above the first line
    QWidget *buildHeader();

    // Files only, the program's icon and name with Change... beside them
    QWidget *buildOpensWith();

    // Handed the program rather than looking it up, since KDE's cache of
    // programs takes a moment to notice a new default after Change...
    void showOpensWith(const KService::Ptr &service);

    // Win7's Change... sets the program for every file of this type at once,
    // without waiting for Apply
    void changeOpensWith();

    // A "Label:  value" row, returning the value so it can be filled in later
    QLabel *addRow(QGridLayout *grid, int &row, const QString &key,
                   const QString &value);

    // The line Win7 draws between groups of rows
    void addSeparator(QGridLayout *grid, int &row);

    // Folders only, Size, Size on disk and Contains counting up as the
    // folder is walked in the background
    void startCounting();
    void showCount();

    // OK and Apply both land here, OK closing the dialog once everything
    // went through and staying open on an error so it can be corrected
    void applyChanges(bool closeWhenDone);

    // A rename runs as a job, so applying finishes here once it is done
    void finishApplying(bool closeWhenDone);

    // Greys the buttons while a rename is running, so OK cannot start a second
    void setBusy(bool busy);

    KFileItem m_item;

    QLineEdit *m_name = nullptr;
    QLabel *m_opensWithIcon = nullptr;
    QLabel *m_opensWithName = nullptr;
    QCheckBox *m_readOnly = nullptr;
    QCheckBox *m_hidden = nullptr;

    QPushButton *m_ok = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_apply = nullptr;

    // Filled in as the count goes, so kept to be written to
    QLabel *m_sizeValue = nullptr;
    QLabel *m_sizeOnDiskValue = nullptr;
    QLabel *m_containsValue = nullptr;

    // A QPointer turns itself to null when the job deletes itself at the end,
    // so the destructor can tell whether there is still a job to stop
    QPointer<KIO::DirectorySizeJob> m_sizeJob;
    QTimer *m_countTimer = nullptr;
};
