add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE sdb::libsdb)
add_custom_command(
    TARGET tests POST_BUILD
    COMMAND codesign --force --sign -
            --entitlements ${PROJECT_SOURCE_DIR}/cmake/debugger-entitlements.plist
            $<TARGET_FILE:tests>
    VERBATIM
)

add_subdirectory(targets)

foreach(case registers source watchpoint hardware missing arguments)
    add_test(NAME ${case} COMMAND tests $<TARGET_FILE:fixture> ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(case threads syscall dynamic attach)
    add_test(NAME ${case} COMMAND tests $<TARGET_FILE:${case}> ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 20)
endforeach()
