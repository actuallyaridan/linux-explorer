#pragma once

// The Windows 7 column list, a flat borderless tree used as a table

class QTreeView;

namespace Aero {

// Styling is scoped to the header, so the body keeps the real Qt style
void configureListTree(QTreeView *tree);

} // namespace Aero
