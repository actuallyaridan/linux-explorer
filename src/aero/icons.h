#pragma once

// Ask by a list of names, best first, and get the first one the running theme
// actually has

#include <QIcon>
#include <QString>
#include <QStringList>

#include <initializer_list>

namespace Aero {

// The name as given, then its symbolic variant
QIcon tryIconName(const QString &name);

// The first icon found from the preference list, then the application's fallbacks
QIcon themeIcon(std::initializer_list<const char *> names);

// Set once at startup, and empty by default, so an unresolvable name yields null
void setIconFallbacks(const QStringList &names);

} // namespace Aero
