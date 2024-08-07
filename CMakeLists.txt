cmake_minimum_required(VERSION 3.9)

# MacOS deployment target must be set before the `project` call
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3 CACHE INTERNAL "")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

project( Crouton
    VERSION     0.1.0
    DESCRIPTION "A C++20 coroutine runtime and libuv-based I/O library"
    LANGUAGES   C CXX
)


set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # For JetBrains IDE


# Set this if building for iOS; it disables executables & tests that don't work on that platform.
option(CROUTON_IOS        "Building for iOS"   OFF)

# Option to build the BLIP source files as a separate library.
# Disabled by default because that code is not Apache-licensed; see src/io/BLIP/README.md.
option(CROUTON_BUILD_BLIP "Build BLIP library" OFF)

#### CMAKE SUBPROJECTS


# libuv options:
set(LIBUV_BUILD_SHARED      OFF CACHE INTERNAL "")

# llhttp options:
set(BUILD_STATIC_LIBS       ON CACHE INTERNAL "")
set(BUILD_SHARED_LIBS       OFF CACHE INTERNAL "")

# mbedTLS options:
set(ENABLE_PROGRAMS         OFF CACHE INTERNAL "")
set(ENABLE_TESTING          OFF CACHE INTERNAL "")
set(MBEDTLS_FATAL_WARNINGS  OFF CACHE INTERNAL "") # Work around doc-comment syntax in 3.4

add_subdirectory(vendor/cpptrace)
add_subdirectory(vendor/libuv)
add_subdirectory(vendor/llhttp)
add_subdirectory(vendor/mbedtls)
add_subdirectory(vendor/spdlog)


#### CROUTON CONFIG


# Build spdlog as compiled code, not header-only
add_definitions(-DSPDLOG_COMPILED_LIB=1)

if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
    add_compile_options(
        "/wd4068;/wd4100;/wd4244;/wd4267"
       # /W4         # I think this means 'turn on lots of warnings'
       # /wd4068     # ignore unknown pragma
       # /wd4100     # ignore unused fn parameters
       # /wd4244     # ignore implicit sign conversion
       # /wd4267     # ignore implicit integer truncation
    )
else()
    # Clang & GCC:
    add_compile_options(
        -Werror
        -Wall
        -Wpedantic
        -Wno-assume             # Lots of bogus "argument to '__builtin_assume' has side effects"
        -Wno-unknown-pragmas
    )
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # GCC-specific:
        add_compile_options(
            -Wno-psabi          # suppress annoying GCC ABI warning
            -Wno-sign-compare   # apparently comes with `pedantic` in GCC
            -D_FORTIFY_SOURCE=2 # static+dynamic buffer-overflow checks
        )
    else()
        # Clang-specific:
        add_compile_options(
            -Wno-gnu-zero-variadic-macro-arguments
            -Wno-gnu-conditional-omitted-operand    # Allow `x ?: y`
            -Wno-gnu-statement-expression           # Allow `({...})`
            -Wno-gnu-statement-expression-from-macro-expansion
        )
    endif()
endif()


#### CROUTON LIBRARY


include_directories( SYSTEM
    vendor/cpptrace/include/
    vendor/libuv/include/
    vendor/llhttp/include/
    vendor/mbedtls/include/
    vendor/spdlog/include/
)


add_library( LibCrouton STATIC
    src/CoCondition.cc
    src/CoroLifecycle.cc
    src/Coroutine.cc
    src/Error.cc
    src/Future.cc
    src/Scheduler.cc
    src/Select.cc
    src/Task.cc

    src/io/Framer.cc
    src/io/HTTPConnection.cc
    src/io/HTTPHandler.cc
    src/io/HTTPParser.cc
    src/io/ISocket.cc
    src/io/IStream.cc
    src/io/Process.cc
    src/io/URL.cc
    src/io/WebSocket.cc

    src/io/mbed/TLSSocket.cc

    src/io/uv/AddrInfo.cc
    src/io/uv/FileStream.cc
    src/io/uv/Filesystem.cc
    src/io/uv/LocalSocket.cc
    src/io/uv/Pipe.cc
    src/io/uv/Stream.cc
    src/io/uv/TCPServer.cc
    src/io/uv/TCPSocket.cc

    src/io/uv/UVBase.cc

    src/support/Backtrace.cc
    src/support/Backtrace_cpptrace.cc
    src/support/betterassert.cc
    src/support/Logging.cc
    src/support/Memoized.cc
    src/support/MiniFormat.cc
    src/support/MiniLogger.cc
    src/support/MiniOStream.cc
    src/support/StringUtils.cc
    src/support/Varint.cc
)

target_include_directories( LibCrouton PUBLIC
    include/
)

target_include_directories( LibCrouton PRIVATE
    src/
)

target_link_libraries( LibCrouton INTERFACE
    cpptrace::cpptrace
    llhttp_static
    mbedtls
    spdlog
    uv_a
)

set_property(TARGET LibCrouton  PROPERTY OUTPUT_NAME Crouton)

if (APPLE)
    target_sources( LibCrouton PRIVATE
        src/io/apple/NWConnection.cc
    )
    target_link_libraries( LibCrouton INTERFACE
        "-framework CoreFoundation"
        "-framework Network"
        "-framework Security"
    )
endif()

if (MSVC)
    target_sources( LibCrouton PRIVATE
        src/support/asprintf.c
        src/support/vasprintf-msvc.c
    )
endif()


#### BLIP

if (CROUTON_BUILD_BLIP)
    add_library( BLIP STATIC
        src/io/blip/BLIPIO.cc
        src/io/blip/Codec.cc
        src/io/blip/Connection.cc
        src/io/blip/Dispatcher.cc
        src/io/blip/Message.cc
        src/io/blip/MessageBuilder.cc
        src/io/blip/MessageOut.cc

        vendor/miniz/miniz_tdef.c
        vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz.c
    )

    target_include_directories( BLIP PUBLIC
        include/
        src/io/blip/
    )

    target_include_directories( BLIP PRIVATE
        src/
        src/io/
        src/support/
        vendor/miniz/
    )

    target_link_libraries( BLIP
        LibCrouton          # Seems redundant, but w/o it executables get link errors on Linux
        z
    )
endif()


#### DEMO TOOLS


if (NOT CROUTON_IOS)
    add_executable( demo_client
        tests/demo_client.cc
    )
    target_link_libraries( demo_client
        LibCrouton
    )

    add_executable( demo_server
        tests/demo_server.cc
    )
    target_link_libraries( demo_server
        LibCrouton
    )

    if (CROUTON_BUILD_BLIP)
        add_executable( demo_blipclient
            tests/demo_blipclient.cc
        )
        target_link_libraries( demo_blipclient
            LibCrouton
            BLIP
        )
    endif()

endif()


#### TESTS


if (NOT CROUTON_IOS)
    add_executable( CroutonTests
        tests/tests.cc
        tests/test_mini.cc
        tests/test_generator.cc
        tests/test_io.cc
        tests/test_http.cc
        vendor/catch2/catch_amalgamated.cpp
        vendor/catch2/ConsoleReporterPlus.cc
    )

    target_include_directories( CroutonTests PRIVATE
        src/
        vendor/catch2/
    )

    target_link_libraries( CroutonTests
        LibCrouton
    )

    if (CROUTON_BUILD_BLIP)
        target_sources( CroutonTests PRIVATE
            tests/test_codec.cc
            tests/test_blip.cc
        )
        target_include_directories( CroutonTests PRIVATE
            vendor/miniz/
        )
        target_link_libraries( CroutonTests
            BLIP
        )
    endif()
endif()


#### DEPENDENCIES FOR XCODE BUILD


add_library( CroutonXcodeDependencies STATIC
    src/support/empty.c    # placeholder; CMake requires _something_ here
)

target_link_libraries( CroutonXcodeDependencies PUBLIC
    mbedtls
    spdlog
    uv_a
)
