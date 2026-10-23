#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "ui/control/PageId.h"
#include <QList>
#include <QHash>

class QScrollArea;
class QVBoxLayout;
class QButtonGroup;
class QAbstractButton;
class QRadioButton;
class QLabel;
class QObject;
class QEvent;

namespace ControlPanel {

// The "Power Options" detail page, a Linux-flavoured take on the Windows 7
// "Select a power plan" screen
//
// On machines that run power-profiles-daemon (PPD), the same service KDE's and
// GNOME's power widgets drive, the radio plans are wired live to it over the
// system bus:
//   * Reading  net.hadess.PowerProfiles.Profiles / .ActiveProfile lists the
//     available plans and the active one.
//   * Selecting a plan writes .ActiveProfile (polkit lets the active session do
//     this without a password, exactly like `powerprofilesctl set`).
//   * A PropertiesChanged subscription keeps the selection in sync when the
//     profile is changed elsewhere (KDE tray, another app, on AC/battery)
//
// PPD's three standard profiles map onto the three Windows plans:
//   performance  -> "High performance"
//   balanced     -> "Balanced (recommended)"
//   power-saver  -> "Power saver"
//
// When PPD is absent (typical on a desktop with no profile switching) the page
// degrades to a single, selected "Balanced (recommended)" plan, mirroring what
// Windows shows on hardware that exposes no alternative plans
class PowerOptionsPage : public QWidget {
    Q_OBJECT

public:
    explicit PowerOptionsPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

protected:
    // Lets a click on a plan's name label select that plan, like Windows
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // One selectable plan, as presented to the user. `profileId` is the raw PPD
    // profile name ("balanced", …) used to read/write the daemon; empty for the
    // synthetic fall-back plan shown when PPD is unavailable
    struct Plan {
        QString profileId;
        // E.g. "Balanced (recommended)"
        QString name;
        // The grey line under the name
        QString description;
        // True -> lives under "Show additional plans"
        bool    additional;
    };

    // Query PPD over the system bus. Returns the profiles it advertises (in the
    // daemon's own low-to-high order) and the active one. `ok` is false when PPD is
    // not reachable, which triggers the static fall-back layout
    QStringList availableProfiles(bool &ok) const;
    QString     activeProfile() const;
    void        setActiveProfile(const QString &profileId);

    // Build the visual plan list and the additional-plans expander
    void buildPlans(const QStringList &profiles);
    QWidget *buildPlanRow(const Plan &plan);

    // Re-checks the radio matching the daemon's current ActiveProfile. Used both
    // on load and from the PropertiesChanged signal handler
    void syncSelection();

private slots:
    void onProfilesPropertiesChanged(const QString &interface,
                                     const QVariantMap &changed,
                                     const QStringList &invalidated);

private:
    // Holds the preferred-plan rows
    QVBoxLayout  *m_planList      = nullptr;
    // Holds the additional-plan rows
    QVBoxLayout  *m_additionalBox = nullptr;
    // Toggled by the expander
    QWidget      *m_additionalWrap = nullptr;
    // Heading row, hidden if empty
    QWidget      *m_additionalHeader = nullptr;
    QButtonGroup *m_group         = nullptr;
    bool          m_ppdAvailable  = false;
    // Guards programmatic re-checks
    bool          m_syncing       = false;

    // Maps each plan's name label to its radio, so clicking the name selects it
    QHash<QObject *, QRadioButton *> m_nameToRadio;
};

} // namespace ControlPanel
