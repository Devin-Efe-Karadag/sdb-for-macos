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
