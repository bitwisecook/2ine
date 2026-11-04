# Media reuse and component navigation

The desktop preferences now retain `lastOS2MediaSources` and
`lastOS2MediaFiles`. Only a complete `validate-os2-media` result replaces this
machine-local suggestion. The first field preserves normalized nested media;
the second identifies outer images/archives for native file-picker defaults.
A ball's explicit remembered sources take priority. Existing selected-ball
sources can supply a lazy initial candidate without scanning media during app
startup. Preferences remain outside exported balls.

macOS now has separate OS/2 System Components and Application Installation
destinations. They reuse the existing extraction wizard and installer form
inline, with adaptive sizing. The navigation uses full native segments when
they fit and a native picker in narrower windows. GNOME provides matching
destinations using its native task dialogs. Installed Applications remains
its own list. New Mac balls start at System Components.

Provider buttons navigate to the actual Settings page and highlight its OpenGL,
multimedia or printer control. Native original-file rows use disabled system
text colours; the provider button stays enabled. Choosing original OpenGL, or
its Install OS/2 System Component button, saves the provider through the
existing undoable command path, returns to components, ticks the eligible
group, reveals it and selects its row. Native-only printing and video groups
remain disabled and explain their limits; no unsupported original decoder or
printer engine is advertised.

Destination directory rows include both native checkboxes and folder icons.
The common outline handles explicit row-focus requests by expanding ancestors,
scrolling to the target and selecting it. Requests are scoped to the ball and
applied once: subsequent manual file unchecks are not silently reversed.
Component drafts preserve exact source choices when navigating to Settings and
back. Drafts are discarded when their package is renamed or removed.

Validation:

- Complete shared-engine tooling suite: 241 tests pass in 63.292 seconds.
- Complete Swift suite: 67 tests pass (37 model and 30 BallCore).
- Final disabled-colour, provider-button, folder-icon and focus checks: 7 pass.
- Image/filesystem/installation/relocation/runtime-cache integration: 5 pass.
- Global Ruff lint/format, ty, ClangFormat, export generation and diff checks pass.

Logs use `/tmp/2ine-media-provider-*`. Generated, no-window light/dark captures
and archived harnesses are under `build-tree-current/provider-navigation-qa`.
The native controls were inspected without accessing the desktop clipboard or
modifying actual user balls or preferences. GTK display interaction and active
window selection/focus acceptance remain unverified on this macOS host.

The first complete Swift run exposed an overly short five-second test deadline
under parallel AppKit/model work. The same provider navigation test passes alone
in 0.207 seconds; the shared helper now permits fifteen seconds while retaining
a finite failure bound. The final suite passes with the real command/Undo and
preference-store paths. Initial checks also updated assertions for the new
first-class component destination and provider button label.

A final provider-navigation regression also checks media with an empty OpenGL
group before supplying actual files. Such a request remains pending until
eligible sources exist, then applies once. The real command/navigation/Undo
test passes in 0.218 seconds after this refinement. The initially packaged
build17 was not installed; a subsequent build contains this final correction.
