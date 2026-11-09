# Public domain. No rights reserved. Wholly AI-authored.
# macOS host adapter, by function. Shared POSIX code is in host/posix/.
set(_twoine_objc_arc "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
twoine_host_function(system system/system.c)
twoine_host_function(process process/self.c)
twoine_host_function(thread thread/sync_hooks.c)
twoine_host_function(fs fs/ranges.c fs/metadata.c)
twoine_host_function(net net/icmp.c net/interfaces.c)
twoine_host_requires(net LINK "-framework SystemConfiguration" "-framework CoreFoundation")
twoine_host_function(fatal fatal/report_hooks.c)
twoine_host_function(fatal_dialog fatal/dialog.m)
twoine_host_requires(fatal_dialog OPTION "${_twoine_objc_arc}")
twoine_host_requires(fatal_dialog LINK "-framework AppKit")
twoine_host_function(window window/chrome.m)
twoine_host_requires(window OPTION "${_twoine_objc_arc}")
twoine_host_requires(window LINK "-framework AppKit")
twoine_host_function(
    desktop
    desktop/session.m
    desktop/window_cycle.m
    desktop/app_icon.m
    desktop/bundle.m
    desktop/bundle_icon.m
)
twoine_host_requires(desktop OPTION "${_twoine_objc_arc}")
twoine_host_requires(desktop LINK twoine_formats "-framework AppKit" "-framework ImageIO")
twoine_host_requires(desktop INCLUDE "${PROJECT_BINARY_DIR}/generated")
twoine_host_function(print print/dialog.m)
twoine_host_requires(print OPTION "${_twoine_objc_arc}")
twoine_host_requires(print LINK "-framework AppKit" "-framework UniformTypeIdentifiers")
twoine_host_function(opengl opengl/driver_policy.c)
twoine_host_requires(opengl DEFINE TWOINE_KOSMICKRISP_ICD="${TWOINE_KOSMICKRISP_ICD}")

# The fallback dock icon for command-line guests: the 2ine ball of twine,
# embedded at configure time so a loader copied anywhere can present it.
if(NOT TWOINE_RUNTIME_ONLY)
    file(
        READ "${PROJECT_SOURCE_DIR}/apps/macos/Resources/Twoine.iconset/icon_256x256.png"
        _twoine_ball_icon_hex
        HEX
    )
    string(LENGTH "${_twoine_ball_icon_hex}" _twoine_ball_icon_length)
    math(EXPR _twoine_ball_icon_size "${_twoine_ball_icon_length} / 2")
    file(
        WRITE "${PROJECT_BINARY_DIR}/generated/twoine_ball_icon_png.h"
        "/* Generated from apps/macos/Resources/Twoine.iconset/icon_256x256.png. Do not edit. */\n"
        "#define TWOINE_BALL_ICON_PNG_SIZE ${_twoine_ball_icon_size}\n"
        "#define TWOINE_BALL_ICON_PNG_HEX \"${_twoine_ball_icon_hex}\"\n"
    )
    unset(_twoine_ball_icon_hex)
    unset(_twoine_ball_icon_length)
    unset(_twoine_ball_icon_size)
endif()
