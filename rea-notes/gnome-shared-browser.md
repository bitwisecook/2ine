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
