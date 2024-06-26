set(root "../../..")
set(src  "../..")

idf_component_register(
    SRCS
        "${src}/Coroutine.cc"
        "${src}/CoCondition.cc"
        "${src}/CoroLifecycle.cc"
        "${src}/Coroutine.cc"
        "${src}/Error.cc"
        "${src}/Future.cc"
        "${src}/Internal.hh"
        "${src}/Scheduler.cc"
        "${src}/Select.cc"
        "${src}/Task.cc"
        "${src}/io/HTTPConnection.cc"
        "${src}/io/HTTPHandler.cc"
        "${src}/io/HTTPParser.cc"
        "${src}/io/ISocket.cc"
        "${src}/io/IStream.cc"
        "${src}/io/Process.cc"
        "${src}/io/URL.cc"
        "${src}/io/WebSocket.cc"
        "${src}/io/mbed/TLSSocket.cc"
        "${src}/support/Backtrace.cc"
        "${src}/support/betterassert.cc"
        "${src}/support/Logging.cc"
        "${src}/support/Memoized.cc"
        "${src}/support/MiniFormat.cc"
        "${src}/support/MiniLogger.cc"
        "${src}/support/MiniOStream.cc"
        "${src}/support/StringUtils.cc"
        "${src}/support/Varint.cc"

        "${src}/io/blip/Connection.cc"
        "${src}/io/blip/BLIPIO.cc"
        "${src}/io/blip/Dispatcher.cc"
        "${src}/io/blip/Message.cc"
        "${src}/io/blip/MessageBuilder.cc"
        "${src}/io/blip/MessageOut.cc"
        "${src}/io/blip/Codec.cc"

        "${root}/vendor/llhttp/src/api.c"
        "${root}/vendor/llhttp/src/http.c"
        "${root}/vendor/llhttp/src/llhttp.c"

        "Backtrace+ESP32.cc"
        "ESPBase.cc"
        "ESPEventLoop.cc"
        "ESPAddrInfo.cc"
        "ESPTCPSocket.cc"

    INCLUDE_DIRS
        "${root}/include/"

    PRIV_INCLUDE_DIRS
        "${src}/"
        "${src}/support/"
        "${root}/vendor/llhttp/include/"
        "."

    REQUIRES
        lwip
        mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-Wno-unknown-pragmas"      # ignore Xcode `#pragma mark` in source code
)
