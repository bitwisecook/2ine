# Public domain. No rights reserved. Wholly AI-authored.
# The guest ABI: checked USE16 argument marshalling and generation-checked OS/2
# handles. Each consumer compiles the sources with its own ABI flags (the native
# lib2ine is i386), so these helpers add sources rather than a library.
function(twoine_add_abi_arguments target)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/arguments.c")
endfunction()

function(twoine_add_abi_handles target)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/handles.c")
endfunction()
