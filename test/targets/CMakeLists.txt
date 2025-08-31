function(add_debug_target name)
    add_executable(${name} ${name}.cpp)
    target_compile_options(${name} PRIVATE -g -gdwarf-4 -O0)
    add_custom_command(
        TARGET ${name} POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

add_debug_target(fixture)
target_compile_options(fixture PRIVATE -fno-omit-frame-pointer)

add_debug_target(threads)
target_compile_options(threads PRIVATE -fno-omit-frame-pointer)

add_debug_target(syscall)
add_debug_target(attach)

add_library(test_library SHARED library.cpp)
target_compile_options(test_library PRIVATE -g -gdwarf-4 -O0)
add_custom_command(
    TARGET test_library POST_BUILD
    COMMAND dsymutil $<TARGET_FILE:test_library>
    VERBATIM
)

add_debug_target(dynamic)
target_compile_definitions(
    dynamic PRIVATE SDB_TEST_LIBRARY="$<TARGET_FILE:test_library>"
)
add_dependencies(dynamic test_library)
