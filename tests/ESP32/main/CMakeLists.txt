idf_component_register(
    SRCS
        "test_esp32.cc"
    PRIV_INCLUDE_DIRS
        "../../../src/"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-UNDEBUG"
    "-Wno-unknown-pragmas"      # ignore Xcode `#pragma mark` in source code
)
