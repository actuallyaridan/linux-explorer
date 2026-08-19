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
#include "Assets.h"
#include "aero/buttons.h"
#include "aero/icons.h"
#include "Locations.h"
#include <AeroQt/stylesheet.h>

// Native scroll bars rather than the theme's skinned ones
//
// They cannot be rescued one widget at a time, since while an application wide
// stylesheet is active Qt wraps even an explicitly restyled widget back into the
// stylesheet engine, so removing the rules is the only bypass
static QString withoutScrollBarRules(QString qss)
{
    // Comments first, so a brace inside one cannot derail the block scan
    static const QRegularExpression comment(
        QStringLiteral(R"(/\*.*?\*/)"),
        QRegularExpression::DotMatchesEverythingOption);
    qss.remove(comment);

    // A stylesheet has no nested braces, so walk the blocks and drop the
    // selectors naming a scroll bar
    QString out;
    out.reserve(qss.size());
    int pos = 0;
    while (pos < qss.size()) {
        const int open = qss.indexOf(QLatin1Char('{'), pos);
        const int close = open < 0 ? -1 : qss.indexOf(QLatin1Char('}'), open);
        if (close < 0) {                       // trailing text outside any block
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

// The theme reapplies its sheet whenever the desktop changes, which arrives as
// a style change, and the strip is deferred since mutating the stylesheet
// during delivery would reenter the style engine
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

// Bare paths are accepted alongside locations, an argument from a shell being
// far more likely to be a relative path
static QList<QUrl> urlsFrom(const QStringList &arguments)
{
    QList<QUrl> urls;
    urls.reserve(arguments.size());
    for (const QString &argument : arguments) {
        // A quoted home shorthand arrives unexpanded and would otherwise be
        // read as a relative path of that name
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
    // No display name is set, Qt appending one to every title where Explorer's
    // is just the folder name
    app.setWindowIcon(Aero::themeIcon({"system-file-manager", "folder"}));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("The Windows 7 Explorer, on Linux."));
    parser.addHelpOption();
    parser.addVersionOption();
    // Windows spells this select
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

    // Normalised before anything else sees them, the handoff and the bus
    // interface both speaking locations, and this process's working directory
    // being the only place a relative path can correctly resolve
    QStringList uris;
    uris.reserve(urls.size());
    for (const QUrl &url : urls)
        uris.append(url.toString());

    // One Explorer per session, so a second launch hands its arguments to the
    // one holding the bus name and gets out of the way
    auto *service = new FileManagerService(&app);
    if (!service->claim()) {
        if (FileManagerService::forward(uris, reveal))
            return 0;
        // The running instance did not answer, so open a window here
    }

    Aero::registerStylesheet(&app);
    new ScrollBarUnstyler(&app);   // native scroll bars, owned by the app

    // The chrome has no opinion on what its icons degrade to
    Aero::setIconFallbacks({QStringLiteral("system-file-manager"),
                            QStringLiteral("folder")});

    // Likewise the pill, which the chrome draws and this application supplies
    Aero::MenuButton::setPillArt(QLatin1String(Explorer::Art::CommandHover),
                                 QLatin1String(Explorer::Art::CommandPress));

    const QString startupId = FileManagerService::startupId();
    if (reveal)
        service->ShowItems(uris, startupId);
    else
        service->ShowFolders(uris, startupId);

    // A reveal naming only files that no longer exist can get here with no
    // window, which would be a process the user cannot see or quit
    if (MainWindow::openWindowCount() == 0)
        MainWindow::openWindow(Locations::computer(), {}, startupId);

    return app.exec();
}
