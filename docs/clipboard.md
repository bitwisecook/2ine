# Clipboard

Guest Copy and Paste commands share these formats with the host clipboard:

- Plain text, converted between UTF-8 and OS/2 codepages 437, 850 or 1004.
- Registered Rich Text Format and HTML Format data.
- OS/2 bitmap handles, transferred as PNG and BMP images.
- Registered PNG data, preserving the original bytes and alpha channel.

One copy can retain text, rich text, HTML and image representations together.
Applications choose the format they can paste. OS/2 bitmap handles contain RGB
pixels; transparency remains available through the PNG representation.
Private application formats remain available within the guest clipboard.

Text pasted into an OS/2 codepage replaces characters that codepage cannot
represent with `?`. OS/2 metafiles and multimedia/private formats are retained
as guest data and are not converted into host images or documents.

The clipboard bridge does not log clipboard contents. Automated clipboard
tests use SDL's dummy video backend and controlled fixture data, without
accessing the desktop clipboard.
