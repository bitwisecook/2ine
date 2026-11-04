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
