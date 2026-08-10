#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include "FileManagerService.h"
#include "MainWindow.h"
#include "IconHelper.h"
#include "Locations.h"
#include <AeroQt/stylesheet.h>

// We want the desktop theme's native scroll bars rather than AeroQt's skinned
// ones, and they cannot be rescued per-widget: while an app-wide stylesheet is
// active, Qt wraps even an explicitly setStyle()'d widget back into the
// stylesheet engine. Removing the rules is the only bypass; with nothing
// matching, the engine delegates to the real Qt style.
//
// Kept identical to the Control Panel's copy so the two apps scroll the same.
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

// Re-strips whenever the rules reappear: AeroQt re-applies its sheet when the
// desktop theme flips between Aero and non-Aero, which reaches us as
// StyleChange. Deferred through a queued single-shot, since mutating the
// stylesheet mid-delivery would re-enter the style engine; a no-op once the
// rules are gone, so it cannot loop.
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

// The locations named on the command line, which the .desktop entry's %U also
// arrives through. Bare paths are accepted alongside URLs: an argument from a
// shell is far more likely to be ./Downloads than a file:// URL.
static QList<QUrl> urlsFrom(const QStringList &arguments)
{
    QList<QUrl> urls;
    urls.reserve(arguments.size());
    for (const QString &argument : arguments) {
        // A quoted "~" and the .desktop entry's Open Home Folder action arrive
        // unexpanded, and QUrl would read it as a relative path named "~".
        QString path = argument;
        if (path == QLatin1String("~"))
            path = QDir::homePath();
        else if (path.startsWith(QLatin1String("~/")))
            path.replace(0, 1, QDir::homePath());

        const QUrl url = QUrl::fromUserInput(path, QDir::currentPath(),
                                             QUrl::AssumeLocalFile);
        if (url.isValid())
            urls.append(url);
    }
    return urls;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("explorer");
    app.setApplicationName("explorer");
    app.setApplicationVersion(QStringLiteral("0.1"));
    // No setApplicationDisplayName: Qt appends it to every window/dialog title,
    // and Explorer's title is just the folder name.
    app.setWindowIcon(themeIcon({"system-file-manager", "folder"}));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("The Windows 7 Explorer, on Linux."));
    parser.addHelpOption();
    parser.addVersionOption();
    // Windows spells this /select: open the containing folder with the file
    // already highlighted.
    QCommandLineOption selectOption(
        QStringList{QStringLiteral("s"), QStringLiteral("select")},
        QStringLiteral("Open each argument's parent folder with the argument "
                       "selected, instead of opening it."));
    parser.addOption(selectOption);
    parser.addPositionalArgument(
        QStringLiteral("url"),
        QStringLiteral("Folders to open, or files to reveal with --select."),
        QStringLiteral("[url...]"));
    parser.process(app);

    const bool reveal = parser.isSet(selectOption);
    const QList<QUrl> urls = urlsFrom(parser.positionalArguments());

    // Normalised to full URLs before anything else sees them: the handoff and
    // the D-Bus interface both speak URIs, and *this* process's working
    // directory is the only place a relative path can correctly resolve.
    QStringList uris;
    uris.reserve(urls.size());
    for (const QUrl &url : urls)
        uris.append(url.toString());

    // One Explorer per session, and it is the one holding the bus name. A
    // second launch is a request aimed at the first: hand the arguments over
    // and get out of the way. See FileManagerService for why this is not the
    // shared FileManager1 name.
    auto *service = new FileManagerService(&app);
    if (!service->claim()) {
        if (FileManagerService::forward(uris, reveal))
            return 0;
        // The running instance did not answer; carry on and open a window here
        // rather than leave the user staring at nothing.
    }

    Aero::registerStylesheet(&app);
    new ScrollBarUnstyler(&app);   // native scroll bars; owned by the app

    const QString startupId = FileManagerService::startupId();
    if (reveal)
        service->ShowItems(uris, startupId);
    else
        service->ShowFolders(uris, startupId);

    // ShowFolders opens Computer when handed nothing, so the plain launch is
    // covered; a --select naming only files that no longer exist can still get
    // here with no window, which would be a process the user cannot see or quit.
    if (MainWindow::openWindowCount() == 0)
        MainWindow::openWindow(Locations::computer(), {}, startupId);

    return app.exec();
}
