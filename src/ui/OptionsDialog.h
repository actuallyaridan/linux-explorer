#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;
class QRadioButton;
class QTreeWidget;
class QTreeWidgetItem;

// Windows 7's Folder Options, laid out as Windows lays it out: three tabs
// (General, View, Search), group boxes with radio pairs, the View tab's
// scrolling tree of advanced settings, a Restore Defaults on every tab, and
// OK / Cancel / Apply along the bottom.
//
// Titled "Options" rather than "Folder Options": it also carries the
// Windows-friendly naming switches, which have no Windows counterpart.
//
// Only settings that actually do something are present. Reproducing Windows'
// full list with the extras inert would invite the user to change a checkbox
// and then wonder what they broke.
//
// Nothing is written until OK or Apply, which is what makes Cancel mean
// something: the settings are read into the widgets once and pushed back out
// in one go rather than written as each control is touched.
class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    // Raised on Apply and OK. The window owns what a change means, most of them
    // needing something repainted, re-read or re-listed.
    void applied();

    // "Apply to Folders" and "Reset Folders". Both are about the view mode,
    // which the window holds, so the dialog only asks.
    void applyViewToAllFolders();
    void resetAllFolders();

private:
    QWidget *buildGeneralTab();
    QWidget *buildViewTab();
    QWidget *buildSearchTab();

    // Fills the controls from the stored settings; Restore Defaults writes the
    // defaults in first and calls this.
    void load();

    // Writes the controls back out. Called by Apply and OK, never before.
    void save();

    // Restores the defaults for whichever tab is on show, as Windows' per-tab
    // button does. Controls only: nothing is stored until Apply or OK.
    void restoreDefaults();

    // A checkable row in the View tab's tree, with `parent` as its heading.
    QTreeWidgetItem *addCheck(QTreeWidgetItem *parent, const QString &text,
                              const QString &tooltip = QString());

    // Two mutually exclusive rows, as Win7 renders "Hidden files and folders".
    // QTreeWidget has no radio state, so the item-changed handler enforces it.
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
