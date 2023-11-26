#include "crouton/Crouton.hh"
#include "crouton/io/blip/BLIP.hh"
#include "io/blip/Codec.hh"

#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_chip_info.h>
#include <esp_event.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_pthread.h>
#include <lwip/dns.h>
#include <nvs_flash.h>

#include <thread>

static void initialize();

using namespace std;
using namespace crouton;
using namespace crouton::io;


static void testCodec() {
    using namespace crouton::io::blip;
    string input = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec sed tortor enim. Nullam nec quam sed odio consequat venenatis et id tortor. Phasellus vitae porttitor tortor. Ut eu diam ullamcorper odio egestas pharetra nec quis diam. Nulla quis dolor consectetur sem bibendum posuere. Phasellus commodo ut mauris ac mollis. Aenean volutpat vulputate ultrices. Phasellus varius purus et augue finibus, at ultrices ante volutpat. Mauris luctus massa a tincidunt posuere. Etiam tincidunt, orci in condimentum malesuada, leo sem luctus dolor, sed dapibus ante justo sed libero. Pellentesque id viverra arcu. Duis lorem mi, eleifend eu iaculis at, laoreet at augue. Curabitur condimentum, augue sit amet varius consequat, lorem odio interdum sem, at venenatis nibh ipsum sit amet velit. Proin consectetur ipsum non arcu commodo ultricies.\n\n\
    Nunc ac ex in felis commodo bibendum nec et lacus. Ut tincidunt imperdiet nibh id venenatis. Cras risus purus, rhoncus a auctor nec, posuere at enim. Sed laoreet nisi quis sapien pulvinar, in efficitur massa venenatis. Proin sollicitudin odio a lacus mattis, sit amet imperdiet magna imperdiet. Donec pulvinar pretium augue, vel fringilla massa scelerisque vitae. Integer tristique lorem quis fringilla vehicula. Pellentesque dignissim pretium velit, sed interdum tellus tristique at. Cras rhoncus ex quam, sit amet accumsan tellus pretium non. In semper tellus dui, rutrum pulvinar leo pellentesque cursus. Fusce sed ex eleifend, semper nulla a, ultrices arcu. Praesent condimentum, elit eu rutrum ornare, quam purus tempus lacus, non porttitor nisi augue sit amet massa.\n\n\
    Sed sit amet interdum sapien, ac efficitur nulla. Suspendisse dignissim suscipit est, non vehicula metus sagittis id. Ut in consectetur diam, eget commodo lorem. Nulla leo odio, euismod vitae ex in, fringilla ornare nulla. Donec purus massa, maximus sit amet finibus eget, rutrum nec velit. Quisque iaculis ex sit amet libero imperdiet euismod facilisis at diam. Aenean venenatis neque et orci pellentesque mattis.\n\n\
    Nulla aliquam mauris eu mi vestibulum pellentesque. Aenean non mauris facilisis, tristique ligula ut, auctor libero. Ut vitae tortor quis dui laoreet bibendum. Curabitur lobortis, augue at euismod mattis, odio odio suscipit elit, id vehicula lacus libero sodales magna. In ac nulla nibh. Nullam varius dictum tellus in ultricies. Nunc sit amet massa odio. Donec efficitur risus pulvinar, bibendum quam nec, convallis felis. Vivamus auctor quam nec metus faucibus, vel condimentum diam condimentum. Nam et risus molestie, lacinia lorem vel, volutpat elit.\
    Donec maximus erat ligula, nec rutrum ante tristique vel. Integer et lacus elementum, molestie ipsum et, consectetur lacus. Quisque dapibus maximus suscipit. Sed egestas ut est ac ultrices. Aliquam mollis gravida est, vel consequat risus congue eget. In ligula est, condimentum hendrerit mattis accumsan, tempor at libero. Nunc ut leo at velit eleifend semper. Sed ut sem sit amet neque feugiat dapibus. Maecenas sagittis ex ut mi molestie, vel dictum nisi fermentum. Fusce a ex id risus dictum fringilla.";

    vector<string> compressed;
    size_t compressedSize = 0;

    {
        auto def = Codec::newDeflater();
        ConstBytes inputBuf(input);
        while (!inputBuf.empty()) {
            char buffer[400];
            MutableBytes outputBuf(buffer, sizeof(buffer));
            auto mode = Codec::Mode::SyncFlush;// inputBuf.empty() ? Codec::Mode::Finish : Codec::Mode::NoFlush;
            MutableBytes written = def->write(inputBuf, outputBuf, mode);
            if (!written.empty()) {
                // SyncFlush always ends the output with the 4 bytes 00 00 FF FF.
                // We can remove those, then add them when reading the data back in.
                assert(written.size() >= 4);
                assert(memcmp(written.endByte() - 4, "\x00\x00\xFF\xFF", 4) == 0);
            }
            def->writeChecksum(outputBuf);
            compressed.emplace_back((char*)written.data(), written.size() + 4);
            compressedSize += written.size() + 4;
        }
    }
    Log->info("Compresed {} bytes to {} frames of total size {}",
              input.size(), compressed.size(), compressedSize);
    string decompressed;
    {
        auto inf = Codec::newInflater();
        for (string frame : compressed) {
            ConstBytes inputBuf(frame.data(), frame.size() - 4);
            char buffer[1000];
            MutableBytes outputBuf(buffer, sizeof(buffer));
            MutableBytes written = inf->write(inputBuf, outputBuf, Codec::Mode::SyncFlush);
            assert(inputBuf.empty());
            decompressed.append((char*)written.data(), written.size());
            inputBuf = ConstBytes(frame.data() + frame.size() - 4, 4);
            inf->readAndVerifyChecksum(inputBuf);
        }
    }
    assert(decompressed == input);
}


staticASYNC<void> testBLIP() {
    // Send HTTP request:
    auto ws = make_unique<ws::ClientWebSocket>("ws://work.local:4985/travel-sample/_blipsync");
    ws->setHeader("Sec-WebSocket-Protocol", "BLIP_3+CBMobile_2");
    AWAIT ws->connect();

    Blocker<void> gotChanges;

    blip::Connection blip(std::move(ws), false, {
        {"changes", [&](blip::MessageInRef msg) {
            Log->info("*** testBLIP received {}", *msg);
            Log->info("*** {}", msg->body());
            if (msg->canRespond()) {
                blip::MessageBuilder response;
                response << "[]";
                msg->respond(response);
            }
            gotChanges.notify();
        } }
    });
    blip.start();

    blip::MessageBuilder msg("subChanges");
    blip::MessageInRef reply = AWAIT blip.sendRequest(msg);

    Log->info("*** demo_blipclient got reply to its `subChanges`: {}", *reply);

    AWAIT gotChanges;

    Log->info("Closing...");
    AWAIT blip.close();
    RETURN noerror;
}


static Generator<int64_t> fibonacci(int64_t limit, bool slow = false) {
    int64_t a = 1, b = 1;
    YIELD a;
    while (b <= limit) {
        YIELD b;
        tie(a, b) = pair{b, a + b};
        if (slow)
            AWAIT Timer::sleep(0.1);
    }
}


Task mainTask() {
    initialize();

    printf("---------- TESTING CROUTON ----------\n\n");
    esp_log_level_set("Crouton", ESP_LOG_VERBOSE);
    log::logger::load_env_levels("Net=debug,BLIP=debug");

#if 0
    Log->info("---------- Testing Generator");
    {
        Generator<int64_t> fib = fibonacci(100, true);
        vector<int64_t> results;
        Result<int64_t> result;
        while ((result = AWAIT fib)) {
            printf("%lld ", result.value());
            results.push_back(result.value());
        }
        printf("\n");
    }

    Log->info("---------- Testing AddrInfo -- looking up example.com");
    {
        io::AddrInfo addr = AWAIT io::AddrInfo::lookup("example.com");
        printf("Addr = %s\n", addr.primaryAddressString().c_str());
        auto ip4addr = addr.primaryAddress();
        postcondition(ip4addr.type == IPADDR_TYPE_V4);
        postcondition(addr.primaryAddressString() == "93.184.216.34");
    }

    Log->info("---------- Testing TCPSocket with TLS");
    {
        auto socket = io::ISocket::newSocket(true);
        AWAIT socket->connect("example.com", 443);

        Log->info("-- Connected! Test Writing...");
        AWAIT socket->stream()->write(string_view("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n"));

        Log->info("-- Test Reading...");
        string result = AWAIT socket->stream()->readAll();

        Log->info("Got HTTP response");
        printf("%s\n", result.c_str());
        postcondition(result.starts_with("HTTP/1.1 "));
        postcondition(result.size() > 1000);
        postcondition(result.size() < 2000);
    }

    Log->info("---------- Testing Codec");
    testCodec();
#endif
    
    Log->info("---------- Testing BLIP");
    AWAIT testBLIP();

    Log->info("---------- End of tests");
    postcondition(Scheduler::current().assertEmpty());

    printf("\n---------- END CROUTON TESTS ----------\n");

    
    printf("Minimum heap space was %ld bytes\n", esp_get_minimum_free_heap_size());
    printf("Restarting in a jillion seconds...");
    fflush(stdout);
    for (int i = 9999999; i >= 0; i--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
//        printf(" %d ...", i);
//        fflush(stdout);
    }
    RETURN;
}

CROUTON_MAIN(mainTask)


// entry point of the `protocol_examples_common` component
extern "C" esp_err_t example_connect(void);

// Adapted from ESP-IDF "hello world" example
static void initialize() {
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%luMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Heap space: %ld bytes ... internal %ld bytes\n",
           esp_get_free_heap_size(), esp_get_free_internal_heap_size());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("example_common", ESP_LOG_WARN);
    ESP_ERROR_CHECK(example_connect());
}


