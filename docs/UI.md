# Shared UI styling

Jobman's `sources/ui.h` / `ui.cpp` adapt Stageviz's shared UI service for this
application. The service owns semantic palette/icon roles, font and icon sizes,
DPI-aware pixmaps, and stylesheet expansion. Jobman's artwork, neutral palette,
Designer geometry, and application behavior remain local to Jobman.

Use the application-owned `::ui()` accessor on the GUI thread. The explicit `::`
avoids collisions with a view's generated `Ui_*` member named `ui`.

## Views and icons

Keep geometry, labels, tooltips, and widget configuration in Designer `.ui` files.
Assign icons in the view's C++ initialization after `setupUi()`:

```cpp
#include "ui.h"

::ui()->setIcon(ui->start, UI::Start);
::ui()->setIcon(ui->openPreset, UI::File);
::ui()->setIcon(ui->load, UI::Folder);
::ui()->setPixmap(dropLabel, UI::Arrow, UI::Large);
```

Bindings update on theme refresh and screen/DPI changes. Retargeting a toggle to
another role replaces its binding rather than accumulating update callbacks:

```cpp
::ui()->setIcon(ui->togglePreset, expanded ? UI::Collapse : UI::Expand);
```

`UI::icon()` returns a cached `QPixmap`, matching the Stageviz API. `buttonIcon()`
returns a `QIcon` with both normal and disabled modes. `setIcon()` binds that icon
and its logical size to a button. `setPixmap()` binds a fixed-size label image.
Scales are Small (16), Medium (32), Large (64), and ExtraLarge (128) logical pixels.
The cache accounts for role, state, size, physical resolution, and device ratio.
Bindings use each widget's ratio, so views on different screens need not share
the primary screen's resolution.

Existing `*Disabled.png` artwork is used when available. Other disabled icons use
an opacity fallback. Resource paths are registered once in `ui.cpp`; views and
Designer files should not embed icon paths or make their own disabled variants.

## Stylesheets and palette

The UI service loads `Resources/App.css` beside the application using the same
platform path as before. If no external file is present, it uses the embedded
`:/style/resources/App.css`. The result is applied through `QApplication`, so
standalone dialogs, popup menus, and dynamically created views share the theme.

`App.css` uses Stageviz-style substitutions:

```css
QWidget { color: $color.text; }
QWidget:disabled { color: $color.disabledtext; }
QPushButton { font-size: $font.size.medium; }
QCheckBox::indicator:checked { image: url("$icon.checked"); }
QTreeView::branch:closed { image: url("$icon.branchclosed"); }
```

Color and icon roles are case-insensitive. Optional state suffixes such as
`$color.text.disabled` and `$icon.folder.disabled` select disabled presentation.
Colors support `.lightness(120)` and `.saturation(50)` modifiers. Literal legacy
`hsl(...)` values in existing external overrides are still supported. Unknown
tokens remain unchanged rather than silently mapping to an unrelated role.

```cpp
::ui()->setColor(UI::Text, QColor(200, 200, 200));
::ui()->setIconPath(UI::Folder, customIconPath);
::ui()->setFontSize(UI::Medium, 12);
::ui()->refresh();
```

Setters reapply the shared stylesheet and invalidate icon caches/bindings.
`styleSheet()` returns the source template; `QApplication::styleSheet()` returns
the expanded stylesheet. `resolveStyleSheet()` expands a template without
applying it. `loadStyleSheet()` reloads the external/embedded theme; passing an
explicit unreadable file returns false and retains the existing theme.

The existing Debug → Reload stylesheet action uses this service, including when
the main window moves between screens. Its `debugReloadStylesheet` action can
also be addressed through MCP Qt tools in Debug builds.

The monitor's custom status delegate uses shared Success, Running, Waiting,
Stopped, Error, and StatusText colors rather than separate hard-coded colors.
Color-space configuration also belongs to this service; the default remains sRGB.

## Verification

`jobman.ui_theme` checks all icon roles and disabled states, fractional/Retina
resolution, cache reuse/invalidation, palette/font changes across independent
windows, retargeted toggle bindings, and the embedded stylesheet. Existing
monitor, file-drop, and MCP regressions continue to exercise the actual views
and their signal/slot behavior. Run them through `test_app.sh` / `test_app.bat`.
