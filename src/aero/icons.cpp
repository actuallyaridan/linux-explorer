#include "icons.h"

namespace Aero {

namespace {

QStringList &fallbacks()
{
    static QStringList names;
    return names;
}

} // namespace

QIcon tryIconName(const QString &name)
{
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(name + QStringLiteral("-symbolic"));
}

QIcon themeIcon(std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        QIcon icon = tryIconName(QString::fromLatin1(n));
        if (!icon.isNull()) return icon;
    }
    for (const QString &n : std::as_const(fallbacks())) {
        QIcon icon = QIcon::fromTheme(n);
        if (!icon.isNull()) return icon;
    }
    return QIcon();
}

void setIconFallbacks(const QStringList &names)
{
    fallbacks() = names;
}

} // namespace Aero
