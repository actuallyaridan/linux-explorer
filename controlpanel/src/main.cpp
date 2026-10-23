#include <QApplication>
#include <QEvent>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include "MainWindow.h"
#include "IconHelper.h"
#include <AeroQt/stylesheet.h>

// AeroQt's application stylesheet skins QScrollBar with the Win7 look. We want
// the desktop theme's native scroll bars instead, and they cannot be rescued
// per-widget: while an app-wide stylesheet is active, Qt wraps even an
// explicitly setStyle()'d widget back into the stylesheet engine. The only
// real bypass is to remove the QScrollBar rules from the application sheet
// itself, with no rules matching, the stylesheet engine delegates scroll-bar
// rendering to the real Qt style (Breeze on KDE).
static QString withoutScrollBarRules(QString qss)
{
    // Drop comments first so a brace inside one can't derail the block scan.
    static const QRegularExpression comment(
        QStringLiteral(R"(/\*.*?\*/)"),
        QRegularExpression::DotMatchesEverythingOption);
    qss.remove(comment);

    // QSS has no nested braces: walk "selectors { body }" blocks and drop the
    // selectors mentioning QScrollBar, keeping any others sharing the block.
    QString out;
    out.reserve(qss.size());
    int pos = 0;
    while (pos < qss.size()) {
        const int open = qss.indexOf(QLatin1Char('{'), pos);
        const int close = open < 0 ? -1 : qss.indexOf(QLatin1Char('}'), open);
        if (close < 0) {                       // trailing non-block text
            out += QStringView(qss).mid(pos);
            break;
        }

        QStringList kept;
        const QStringList selectors = qss.mid(pos, open - pos).split(QLatin1Char(','));
        for (const QString &sel : selectors) {
            if (!sel.contains(QLatin1String("QScrollBar")))
                kept << sel;
        }
        if (!kept.isEmpty())
            out += kept.join(QLatin1Char(',')) + qss.mid(open, close - open + 1);
        pos = close + 1;
    }
    return out;
}

// Strips the QScrollBar rules now and again whenever they reappear: AeroQt
// re-applies its sheet when the desktop theme flips between Aero and
// non-Aero, which reaches us as StyleChange events. The re-strip is deferred
// with a queued single-shot: mutating the application stylesheet while a
// StyleChange is still being delivered would re-enter the style engine, and
// is a no-op once the rules are gone, so it cannot loop.
class ScrollBarUnstyler : public QObject {
public:
    explicit ScrollBarUnstyler(QApplication *app) : QObject(app), m_app(app)
    {
        strip();
        app->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::StyleChange && !m_pending
            && m_app->styleSheet().contains(QLatin1String("QScrollBar"))) {
            m_pending = true;
            QTimer::singleShot(0, this, [this]() {
                m_pending = false;
                strip();
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void strip()
    {
        const QString qss = m_app->styleSheet();
        const QString filtered = withoutScrollBarRules(qss);
        if (filtered != qss)
            m_app->setStyleSheet(filtered);
    }

    QApplication *m_app;
    bool m_pending = false;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("controlpanel");
    app.setApplicationName("controlpanel");
    // No setApplicationDisplayName: Qt appends it to every window/dialog title.
    app.setWindowIcon(themeIcon({"preferences-system"}));

    Aero::registerStylesheet(&app);
    new ScrollBarUnstyler(&app);   // native scroll bars; owned by the app

    MainWindow w;
    w.show();
    return app.exec();
}
