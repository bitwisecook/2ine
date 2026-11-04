# Shared GTK browsing control

`tools/gnome_browser.py` now owns the native ColumnView/TreeListModel renderer.
Its adapters supply row dictionaries, stable identity, child lists, column text
and sort keys, row actions, optional checkbox state/toggle handlers, selection,
activation and optional context/drop handlers. Search/type capture, ancestry
pruning, expanders, row factories, row recycling, responsive columns, check
rendering and selection focus are shared. It uses GTK/libadwaita already shipped
by the GNOME frontend and introduces no third-party control.

Current adapters:

- `FileList` in gnome_files.py: guest drives, application selection and nested
  media. Executable/relevant-only filtering remains in callers and defaults on.
  Name collation, folders first, numeric size/date sorting, icons and validated
  directory drops remain adapter behavior. Filtering out the selected file
  clears the domain selection; a hidden non-executable cannot remain selected
  for the application chooser.
- `ComponentTree` in gnome_components.py: exact source choices and provider
  links. CheckedTree state stays independent of filtered presentation. Group
  requirements apply on group selection; manually unchecked leaves stay off.
  Whole-SOM configuration still requires all its eligible source values.
- ProfilePage/FileProfilePage in gnome_configuration.py: INI/profile values
  and registry values share columns and edit actions. Existing byte decoding,
  type labels, typed dialogs, revision checks, write APIs and busy guards remain
  in their original domain code. Profile row editing retains single-click
  activation through the renderer option.
- RegistryPage key hierarchy: same native renderer for the key sidebar and
  value table, with stable path focus, native expanders and fuzzy key search.
  Registry roots/path case behavior and typed editing are unchanged.
- Os2FilesPage: configuration/INI file selection uses the same searchable
  table instead of a separate ComboRow. CONFIG.SYS and text contents remain
  domain text editors. The existing GTK configuration integration script now
  selects files by stable path rather than obsolete ComboRow positions.

Ordinary settings forms, printer/type ComboRows, dialogs and text editors
remain their appropriate native controls; they are not file/tree browsers.
Application action-card menus and their context actions were not rewritten.

The GI-free `gnome_browser_model.py` implements fuzzy normalization, filtered
hierarchy views and optional check state. Seven tests cover accent-insensitive
ordered matching, retained default file filters, ancestor/row identity,
matching-group descendants, partial selection unaffected by search, explicit
leaf omissions, transitive/cyclic requirements, disabled/provider nodes and
package registration. These seven plus five lifecycle tests pass12/12.

The package staging list includes both shared browser modules. Linux package
integration explicitly asserts their presence. Ruff, model ty, py_compile and
diff checks pass; all eight modified browser/caller/test files pass the pinned
Ruff formatting check. Requested clipboard integration registration is present
exactly once in both global Ruff and ty include lists.

PyGObject is unavailable on this macOS host. Native GTK widget construction,
factory recycling, keyboard/mouse interaction, drag/drop, responsive rendering
and a staged Linux package were not executed here. The existing native GTK
integration tests preserve the typed edit/conflict checks for a Linux host;
source/model checks do not substitute for those display/interaction gates.
No actual user ball or guest process was modified during this refactor.

## Verified media, installation pages and provider navigation (2026-11-04)

GTK now has separate first-class **OS/2 System Components** and
**Application Installation** pages. They offer the existing native source
browser/extraction wizard and application installer dialog. The Applications
page remains the installed launcher list with Run/Add actions. Existing four
page keyboard shortcuts retain their targets.

`gnome_media_preferences.py` stores only successful `validate-os2-media`
results in application preferences (`lastOS2MediaSources` and
`lastOS2MediaFiles`), never a ball manifest. A ball's own remembered source
has priority; a new ball falls back to the last verified global source.
Virtual paths remain opaque and normalized by the core. Physical validated
paths give the native picker an initial file/folder. Invalid global reuse is
cleared while the source browser's Choose ISO/Archive and Choose Folder
controls remain available; the component overview also has a force-picker
Choose Different Media action. Preferences provides a global Forget action.
Dependency recovery validates supplied/reused media before remembering it.

The shared check model now inherits disabled ancestors, so Select All cannot
select original driver leaves under native-selected groups. Directory nodes
use folder icons and the same mixed checkbox state; selecting a directory
adds its files without adding a semantic parent's component requirements.
Native-disabled names use the theme's dim-label style. The parent provider
status has an active `2ine` / `2ine (Mesa)` button; duplicate setting children
are hidden in this adapter. The button opens the real Settings section and
briefly highlights/focuses its actual OpenGL, video or printer control.

Settings' **Install OS/2 System Component…** links return to the component
page/wizard and focus the matching group. OpenGL's link or choosing Original
saves that supported provider choice through normal undoable configuration,
replans the media tree, and selects the enabled original group. Native-only
printing/multimedia links explain that original drivers are not supported
and highlight the still-disabled group. Returning from Settings rebuilds
provider flags from the core instead of editing display labels alone.

Verification: 20 focused pure search/checkbox/preferences/lifecycle tests
pass, global Ruff check and formatting pass (143 registered files), focused
ty and Python compile checks pass. Packaging registers the preference helper
and its presence is asserted by Linux package integration. GI is unavailable
on this macOS host; the native GTK runtime/interaction tests remain a Linux
verification gate. No user ball, user preferences file, or active guest app
was modified while implementing these changes.

## Inline component operations (2026-11-04)

The GTK component workflow now uses `OS2ComponentsPane(Gtk.Box)` directly
inside the System Components page. The media selector/browser and extraction
tree remain visible together. The old component `Adw.Dialog` controller is
removed; the only component popups are asynchronous native file/folder
choosers initiated by the user. Checkboxes select files. Explicit inline
Install Selected and Remove Selected buttons start their respective jobs;
unticking does not delete a file.

Each ball retains its pane and job across page or ball changes. Component I/O
uses a dedicated executor; metadata/progress/cancellation use another executor,
leaving the existing window/navigation worker available. Jobs do not set the
window's global busy flag or disable its sidebar. Selected-ball mutation and
launch actions, package rename/trash/export, configuration writes, and
undo/redo are guarded while that ball has an active component job. Other
balls and read navigation remain usable. A component spinner on the ball's
sidebar row remains visible even after navigating away. Closing the window
while an owned component job is active is declined with a toast directing
the user to finish or cancel it in the component page.

`NativeBrowser` has an optional row activity callback. Component progress
marks each active source/path and all ancestor groups/directories with native
spinners. The inline job row exposes phase and cooperative Cancel Job. The
approved core `os2-component-state` action supplies path-only installed and
ownership state without media planning, ISO reopening or extraction; polling
reads only newly completed/active paths, with one full known-file status
refresh at completion. File status distinguishes installed managed,
installed modified/preserved, installed not managed, protected and available.
The Remove button enables only explicit `status == managed`, `present`,
`managed` records. The core revalidates ownership and identity during removal;
modified/unowned/protected files are excluded even if an old UI selection
exists. Per-file state and job progress are adapter state, separate from file
checkbox selection and source metadata.

Verification: 27 pure model/preferences/lifecycle tests pass; global Ruff
check/format (146 files), focused ty, compile and GTK-source diff checks pass.
The package includes `gnome_component_jobs.py`. A new native integration gate,
`tests/integration/gnome_components.py --build BUILD`, creates two disposable
balls and verified fixture media, holds component I/O on an event, checks
inline/no-dialog behavior and navigation to the other ball while the first
job remains active, then verifies managed status and preservation of a
modified CMD during removal. That native gate is authored and type/compile
checked but **not run here**: PyGObject/GTK is unavailable on this macOS host.
No user ball, preferences file or active guest was changed for this work.

Additional adapter/core contract checks used
`build-component-operations-current/libtwoine_ball.dylib` with temporary balls
and synthetic CMD/SYSLEVEL/message fixture media. Validation succeeded; exact
selection installed three files, all reported `managed` and removable by the
UI model, with progress `complete`. A second probe modified installed CMD and
added an unowned help file, then submitted all four paths for removal: two
unchanged owned files were removed, modified CMD and unowned help retained
their exact bytes, and final states were modified/missing/missing/unowned.
These checks exercised the real core and Python state projection, not GTK.

An ownership-state entry's `source` and `category` are provenance, so the
adapter deliberately preserves the current plan's source/ID/category and
stores provenance separately as `installedSource`. The extra regression
brings focused pure coverage to 28 tests. The existing
`tests/integration/gnome_native.py` extraction scenario has also been updated
from dialog stages to the inline pane/background job contract.

### Exact-source state and removal finalization

GTK now uses `selection: [{source, path}]` for both complete status refreshes
and live polling. The source is the current plan's exact opaque ID; its path
remains the canonical planned destination. Returned records are grouped by
`selectedSource`. Actual ownership destinations are stored separately in
`actualPath`, `actualPaths` and `actualFiles` and shown in file tooltips.
Overlays never overwrite planned path/source/ID/category. A missing alternate
cannot borrow another source's canonical ownership. Multiple owned records
retain individual managed/modified/protected verdicts; the Remove count
includes only unchanged managed destinations. GUI removal submits exact
`fileSelection` plus its original `sources`, allowing the core to resolve
retained variants and preserve other sources' files.

Four additional pure regressions cover retained actual destinations, absent
alternates sharing a canonical path, multiple actual records, and distinct
source selections sharing one planned path (32 focused tests total). A real
core + Python model disposable probe installed two variants at distinct
retained destinations, preserved the canonical planned path during overlay,
removed one exact source and verified the other variant's bytes remained.
Global Ruff check/format, focused ty, Python compile and source diff checks
pass. This completes the bounded GTK source-aware state adapter update;
native GTK interaction qualification remains a Linux gate.
