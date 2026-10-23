#pragma once

#include "PageId.h"

#include <QString>

// ---------------------------------------------------------------------------
// The single source of truth mapping each PageId to its canonical breadcrumb
// path (e.g. PageId::LinuxUpdate <-> "System and Security/Linux Update").
//
// The path is still what history and the crumb bar store and what navigateTo
// routes on, but it is now derived from the page's id here rather than being
// duplicated as free-standing string constants throughout MainWindow.
// ---------------------------------------------------------------------------
namespace PageRegistry {

// The canonical breadcrumb path for a page. Home/None map to an empty string.
QString pathFor(PageId id);

// The page whose canonical path equals `path`, or PageId::None if unknown.
// An empty string maps to PageId::Home.
PageId idForPath(const QString &path);

} // namespace PageRegistry
