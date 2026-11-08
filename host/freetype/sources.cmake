# Public domain. No rights reserved. Wholly AI-authored.
# FreeType font discovery and rasterisation.
twoine_host_function(font font/font.c)
twoine_host_requires(font LINK twoine_freetype)
twoine_host_requires(font DEFINE TWOINE_FONT_DIRECTORY="${TWOINE_RUNTIME_FONT_DIR}")
