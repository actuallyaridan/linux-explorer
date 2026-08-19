#include "sounds.h"

#include <QCoreApplication>
#include <QHash>
#include <QSoundEffect>
#include <QUrl>

namespace Aero {

namespace {

QString &themePath()
{
    static QString path = QStringLiteral("/usr/share/sounds/Windows 7/og");
    return path;
}

} // namespace

void playSound(const QString &name)
{
    // A QSoundEffect has to outlive the play call, and the dialogs these come
    // from run their own event loop and then go away
    static QHash<QString, QSoundEffect *> effects;
    QSoundEffect *&effect = effects[name];
    if (!effect) {
        effect = new QSoundEffect(qApp);
        effect->setSource(QUrl::fromLocalFile(
            QStringLiteral("%1/%2.wav").arg(themePath(), name)));
        effect->setVolume(1.0f);
    }
    effect->play();
}

void setSoundThemePath(const QString &directory)
{
    themePath() = directory;
}

} // namespace Aero
