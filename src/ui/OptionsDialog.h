#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;
class QRadioButton;
class QTreeWidget;
class QTreeWidgetItem;

// Win7's folder options, laid out as Windows lays it out
//
// Only settings that actually do something are present, and nothing is written
// until OK or Apply, which is what makes Cancel mean something
class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    // Raised on Apply and OK, the window owning what a change means
    void applied();

    // Both are about the view mode, which the window holds
    void applyViewToAllFolders();
    void resetAllFolders();

private:
    QWidget *buildGeneralTab();
    QWidget *buildViewTab();
    QWidget *buildSearchTab();

    // Restoring defaults writes them in first and then calls this
    void load();

    // Called by Apply and OK, never before
    void save();

    // Whichever tab is on show, as Windows does it, and controls only, since
    // nothing is stored until Apply or OK
    void restoreDefaults();

    QTreeWidgetItem *addCheck(QTreeWidgetItem *parent, const QString &text,
                              const QString &tooltip = QString());

    // A tree has no radio state, so the item changed handler enforces it
    void addRadioPair(QTreeWidgetItem *parent, const QString &offText,
                      const QString &onText, QTreeWidgetItem **offItem,
                      QTreeWidgetItem **onItem);

    class QTabWidget *m_tabs = nullptr;

    // General
    QRadioButton *m_sameWindow = nullptr;
    QRadioButton *m_ownWindow = nullptr;
    QRadioButton *m_singleClick = nullptr;
    QRadioButton *m_doubleClick = nullptr;

    // View
    QTreeWidget *m_advanced = nullptr;
    QTreeWidgetItem *m_hiddenOff = nullptr;
    QTreeWidgetItem *m_hiddenOn = nullptr;
    QTreeWidgetItem *m_hideExtensions = nullptr;
    QTreeWidgetItem *m_checkBoxes = nullptr;
    QTreeWidgetItem *m_alwaysShowMenus = nullptr;
    QTreeWidgetItem *m_friendlyMode = nullptr;
    QTreeWidgetItem *m_windowsNames = nullptr;

    // Search
    QRadioButton *m_namesOnly = nullptr;
    QRadioButton *m_namesAndContents = nullptr;
    QCheckBox *m_includeSubfolders = nullptr;

    QPushButton *m_apply = nullptr;
};
