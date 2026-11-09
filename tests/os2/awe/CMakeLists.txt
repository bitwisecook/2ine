# Public domain. No rights reserved. Wholly AI-authored.
# Open32 (PMWINX): unit tests and the guest contracts paired with them.
if(TARGET pmwinx)
    add_executable(twoine_open32_atomic_tests test_open32_atomic.c)
    target_link_libraries(twoine_open32_atomic_tests PRIVATE pmwinx_interlocked)
    twoine_application_target(twoine_open32_atomic_tests)
    target_link_libraries(twoine_open32_atomic_tests PRIVATE 2ine)
    add_test(NAME open32_atomic_backing COMMAND twoine_open32_atomic_tests)
    add_test(
        NAME open32_atomic
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_atomic.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work
            "${PROJECT_BINARY_DIR}/open32-atomic-tests"
    )
    set_tests_properties(open32_atomic PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_adjust_window_tests test_open32_adjust_window.c)
    target_link_libraries(twoine_open32_adjust_window_tests PRIVATE pmwinx_adjust_window)
    twoine_application_target(twoine_open32_adjust_window_tests)
    add_test(NAME open32_adjust_window COMMAND twoine_open32_adjust_window_tests)
    add_executable(twoine_open32_rectangle_tests test_open32_rectangles.c)
    twoine_application_target(twoine_open32_rectangle_tests)
    target_link_libraries(twoine_open32_rectangle_tests PRIVATE pmwinx pmmerge 2ine)
    add_test(NAME open32_rectangles COMMAND twoine_open32_rectangle_tests)
    add_executable(twoine_open32_load_menu_tests test_open32_load_menu.c)
    twoine_application_target(twoine_open32_load_menu_tests)
    target_link_libraries(
        twoine_open32_load_menu_tests
        PRIVATE pmwinx pmmerge pmgpi 2ine SDL3::SDL3
    )
    add_test(NAME open32_load_menu_ownership COMMAND twoine_open32_load_menu_tests)
    add_test(
        NAME open32_load_menu
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_load_menu.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work-dir
            "${PROJECT_BINARY_DIR}/open32-load-menu-tests"
    )
    set_tests_properties(open32_load_menu PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_unicode_tests test_open32_unicode.c)
    target_link_libraries(twoine_open32_unicode_tests PRIVATE pmwinx_unicode)
    twoine_application_target(twoine_open32_unicode_tests)
    target_link_libraries(twoine_open32_unicode_tests PRIVATE 2ine)
    add_test(NAME open32_unicode_conversion COMMAND twoine_open32_unicode_tests)
    add_test(
        NAME open32_unicode
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_unicode.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work
            "${PROJECT_BINARY_DIR}/open32-unicode-tests"
    )
    set_tests_properties(open32_unicode PROPERTIES TIMEOUT 45)
    add_executable(twoine_open32_menu_metric_tests test_open32_menu_metrics.c)
    target_link_libraries(twoine_open32_menu_metric_tests PRIVATE pmwinx_system_metrics)
    twoine_application_target(twoine_open32_menu_metric_tests)
    add_test(NAME open32_menu_metric_dimensions COMMAND twoine_open32_menu_metric_tests)
    add_test(
        NAME open32_menu_metrics
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_menu_metrics.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work-dir
            "${PROJECT_BINARY_DIR}/open32-menu-metric-tests"
    )
    set_tests_properties(open32_menu_metrics PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_pointer_tests test_open32_pointers.c)
    target_link_libraries(twoine_open32_pointer_tests PRIVATE pmwinx_cursors)
    twoine_application_target(twoine_open32_pointer_tests)
    add_test(NAME open32_pointer_coordinates COMMAND twoine_open32_pointer_tests)
    add_test(
        NAME open32_pointers
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_pointers.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work-dir
            "${PROJECT_BINARY_DIR}/open32-pointer-tests"
    )
    set_tests_properties(open32_pointers PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_priority_tests test_open32_priority.c)
    target_link_libraries(twoine_open32_priority_tests PRIVATE pmwinx_priority)
    twoine_application_target(twoine_open32_priority_tests)
    target_link_libraries(twoine_open32_priority_tests PRIVATE 2ine)
    add_test(NAME open32_priority_mapping COMMAND twoine_open32_priority_tests)
    add_test(
        NAME open32_priority
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_priority.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work
            "${PROJECT_BINARY_DIR}/open32-priority-tests"
    )
    set_tests_properties(open32_priority PROPERTIES TIMEOUT 30)
    if(APPLE)
        add_executable(twoine_open32_priority_host_tests test_open32_priority.c)
        target_link_libraries(twoine_open32_priority_host_tests PRIVATE pmwinx_priority)
        twoine_application_target(twoine_open32_priority_host_tests)
        target_compile_definitions(
            twoine_open32_priority_host_tests
            PRIVATE TWOINE_PRIORITY_REAL_HOST=1
        )
        target_link_libraries(twoine_open32_priority_host_tests PRIVATE 2ine)
        add_test(NAME open32_priority_host COMMAND twoine_open32_priority_host_tests)
    endif()
    add_test(
        NAME open32_accelerators
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_accelerators.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(open32_accelerators PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_accelerator_tests test_open32_accelerators.c)
    twoine_application_target(twoine_open32_accelerator_tests)
    target_link_libraries(twoine_open32_accelerator_tests PRIVATE pmwinx 2ine SDL3::SDL3)
    add_test(
        NAME open32_accelerator_ownership
        COMMAND twoine_open32_accelerator_tests $<TARGET_FILE:pmwinx>
    )
    set_tests_properties(open32_accelerator_ownership PROPERTIES TIMEOUT 20)
    add_test(
        NAME open32_tls
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_tls.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(open32_tls PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_tls_cleanup_tests test_open32_tls_cleanup.c)
    twoine_application_target(twoine_open32_tls_cleanup_tests)
    target_link_libraries(twoine_open32_tls_cleanup_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME open32_tls_cleanup COMMAND twoine_open32_tls_cleanup_tests $<TARGET_FILE:pmwinx>)
    set_tests_properties(open32_tls_cleanup PROPERTIES TIMEOUT 20)
endif()
