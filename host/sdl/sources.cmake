# Public domain. No rights reserved. Wholly AI-authored.
# SDL3 adapter for windows, input, audio, clipboard and the fatal dialog.
twoine_host_function(window window/display.c window/window.c window/render.c)
twoine_host_function(input input/events.c input/pointer.c)
twoine_host_function(audio audio/stream.c)
twoine_host_function(clipboard clipboard/clipboard.c clipboard/image_codec.c)
twoine_host_function(fatal_dialog fatal/dialog.c)
foreach(
    _twoine_function
    window
    input
    audio
    clipboard
    fatal_dialog
)
    twoine_host_requires(${_twoine_function} LINK SDL3::SDL3)
endforeach()
