add_executable(sdb sdb.cpp)
target_link_libraries(sdb PRIVATE sdb::libsdb PkgConfig::libedit fmt::fmt)

include(GNUInstallDirs)
install(
    TARGETS sdb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_custom_command(TARGET sdb POST_BUILD COMMAND codesign --force --sign - --entitlements ${PROJECT_SOURCE_DIR}/cmake/debugger-entitlements.plist $<TARGET_FILE:sdb> VERBATIM)
