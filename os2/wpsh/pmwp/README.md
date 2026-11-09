<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMWP.DLL

Workplace Shell classes and Win*Object APIs.

- IBM source: Inferred `wpsh\pmwp`; wpobject.c and cam.c are evidenced.
- Evidence: Strings in the unpacked PMWP.DLL.
- In Warp 4.52: Real code.
- In 2ine: The Win*Object API over a profile-backed object store, and the
  Workplace classes for guest SOM.
- Exports: `pmwp.def`.

## Units

| 2ine unit | IBM unit | Contents |
|---|---|---|
| `module.c` | — | Export table, *NewClass entries, exported class data |
| `store.c` | unknown | Object store and the pmwpStore* accessors |
| `objects.c` | unknown | WinCreateObject, WinSetObjectData, WinDestroyObject, WinQueryObject(Path), WinMove/Copy/SaveObject, WinCreateShadow, window positions, file icons |
| `classes.c` | unknown | WinRegister/Deregister/Replace/EnumObjectClasses |
| `open.c` | unknown | WinOpenObject, folder views, associated programs |
| `wpobject.c` | `wpobject.c` (inferred by name) | WPObject lifetime, state and persistence |
| `som_views.c` | unknown | WPObject views, menus, drag, settings, help, shadows |
| `som_classes.c` | unknown | M_WPObject class methods; file system, folder, disk, program and data file classes |
| `som_extended.c` | unknown | Methods reached by name: icons, appearance, object semaphores, file attributes, state, container, class manager |
| `som_runtime.c`, `som_tables.c`, `som_tokens.h` | — | Binding to the guest SOM.DLL, class layouts and method tokens |

`som_classes.c` and `som_extended.c` each register their method bodies in one
table (`pmwpClassBodies`, `pmwpExtendedBodies`) that names their static
methods; splitting them by class needs per-class tables.
