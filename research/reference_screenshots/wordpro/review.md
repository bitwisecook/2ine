# Word Pro visual comparison

Compare the [stock final reopening](final-reopened.png),
[font selector](font-selector-0.png) and [Save As](save-as.png) with the
[retained native reopening](2ine/native/final-reopened.png) and
[Unicorn reopening](2ine/unicorn/final-reopened.png). Both `2ine/` subdirectories
retain the complete cycle and hashed receipts. The native capture uses privately
extracted Warp outline fonts; Unicorn uses the public Liberation substitutes.
The comparison report supports side-by-side views and opacity overlays. Stock
captures include the OS/2 title bar and desktop taskbar; 2ine captures the host
window's client area. Align the menu bars before assessing positions.

| Element | Stock Warp reference | 2ine result |
|---|---|---|
| Document cycle | Text and Courier/Helv changes survive two saves and reopenings | Verified on native Linux and Linux Unicorn with real keyboard/pointer input and application-written LWP files; reopened edits append in a new paragraph |
| SmartIcons toolbar | Visible document/editing icons and left grab area | Installed icons render with stock SmartIcons preferences; complete grab-area/frame behavior remains unqualified |
| Bottom button images | Gray control backgrounds surrounding colored images | Source-transparent bitmap copying preserves the control background instead of drawing the green transparency color |
| Page guides | Light gray lines with a subtle break at the text boundary | Present; page viewport translations apply before clipping. The native right-guide break is five pixels versus six in the stock master |
| Yellow strip | Alternating `#808000`/`#c0c0c0` pixels and a dark lower edge | Selected bitmap patterns and their reference points render; the same two colors and lower edge are retained |
| Font selection | Installed Warp and Lotus font families | Three exposed families with Courier and Helv selection; optional private Warp faces. Glyph shape, antialiasing and small metric differences are allowed |
| Document frame | Maximized within the application | Explicit maximize requests now fill the available parent area and restore geometry is retained. The 96 dpi 2ine profile versus the stock display's 120 dpi font resolution changes physical page width and surrounding workspace |
| Document menu | Adds the Text menu while a document is open | Text menu merging remains incomplete |
| Scrollbars | Horizontal and vertical stock controls | Stock scrollbar support remains incomplete |
| File dialogs | Directory/file lists, type selector, description and action buttons | Typed single-file save/open work; directory icons, scrolling and full stock styling remain incomplete |

Font lenience does not excuse missing controls, clipped content, incorrect state
or incorrect layout. These references qualify the bounded document cycle; they
do not establish complete Word Pro, printing, MDI or macOS application support.
