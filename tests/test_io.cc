//
// test_io.cc
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "tests.hh"
#include "crouton/io/HTTPParser.hh"
#include "crouton/io/apple/NWConnection.hh"
#include "crouton/io/mbed/TLSSocket.hh"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#else
#   include <sys/socket.h>
#endif

using namespace crouton;
using namespace crouton::io;


TEST_CASE("URLs", "[uv]") {
    {
        URL url("http://example.com:8080/~jens?foo=bar");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "example.com");
        CHECK(url.port == 8080);
        CHECK(url.path == "/~jens");
        CHECK(url.query == "foo=bar");
        CHECK(url.unescapedPath() == "/~jens");
        CHECK(url.queryValueForKey("foo") == "bar");
        CHECK(url.queryValueForKey("baz") == "");
    }
    {
        URL url("http://example.com");
        CHECK(url.scheme == "http");
        CHECK(url.hostname == "example.com");
        CHECK(url.port == 0);
        CHECK(url.path == "");
    }
    {
        URL url("/some/%22thing%22?foo=bar&baz=17&wow");
        CHECK(url.scheme == "");
        CHECK(url.hostname == "");
        CHECK(url.port == 0);
        CHECK(url.path == "/some/%22thing%22");
        CHECK(url.query == "foo=bar&baz=17&wow");
        CHECK(url.unescapedPath() == "/some/\"thing\"");
        CHECK(url.queryValueForKey("foo") == "bar");
        CHECK(url.queryValueForKey("baz") == "17");
        CHECK(url.queryValueForKey("wow") == "wow");
    }
    {
        URL url("wss", "example.com", 1234, "/path", "x=y");
        CHECK(url.scheme == "wss");
        CHECK(url.hostname == "example.com");
        CHECK(url.port == 1234);
        CHECK(url.path == "/path");
        CHECK(url.query == "x=y");
        CHECK(string(url) == "wss://example.com:1234/path?x=y");
    }
}


static Future<string> readFileSlowly(string const& path) {
    string contents;
    FileStream f(path);
    Result<void> r = AWAIT NoThrow(f.open());
    if (!r.ok())
        RETURN r.error();
    char buffer[100];
    while (true) {
        auto readFuture = f.read({&buffer[0], sizeof(buffer)});
        int64_t len = AWAIT readFuture;
        if (len < 0)
            cerr << "File read error " << len << endl;
        if (len <= 0)
            break;
        cerr << "(" << len << "bytes) ";
        contents.append(buffer, len);
    }
    cerr << endl;
    RETURN contents;
}


Future<string> ReadFile(string const& path) {
    io::FileStream fs{string(path)};
    AWAIT fs.open();
    RETURN AWAIT fs.readAll();
}


TEST_CASE("Read a file", "[uv]") {
    RunCoroutine([]() -> Future<void> {
        string contents = AWAIT readFileSlowly("README.md");
        //cerr << "File contents: \n--------\n" << contents << "\n--------"<< endl;
        CHECK(contents.size() > 500);
        CHECK(contents.size() < 10000);
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Fail to read a file", "[uv][error]") {
    RunCoroutine([]() -> Future<void> {
        Result<string> contents = AWAIT NoThrow(readFileSlowly("nosuchfile"));
        cout << "Returned: " << contents.error() << endl;
        CHECK(contents.isError());
        CHECK(contents.error().domain() == "libuv");
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("DNS lookup", "[uv]") {
    RunCoroutine([]() -> Future<void> {
        AddrInfo addr = AWAIT AddrInfo::lookup("example.com");
        cerr << "Addr = " << addr.primaryAddressString() << endl;
        auto ip4addr = addr.primaryAddress(4);
        REQUIRE(ip4addr);
        CHECK(ip4addr->sa_family == AF_INET);
        CHECK(addr.primaryAddressString().starts_with("93.184."));
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Read a socket", "[uv]") {
    RunCoroutine([]() -> Future<void> {
        auto socket = ISocket::newSocket(false);
        cerr << "-- Test Connecting...\n";
        AWAIT socket->connect("example.com", 80);

        cerr << "-- Connected! Test Writing...\n";
        AWAIT socket->stream()->write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

        cerr << "-- Test Reading...\n";
        string result = AWAIT socket->stream()->readAll();

        cerr << "HTTP response:\n" << result << endl;
        CHECK(result.starts_with("HTTP/1.1 "));
        CHECK(result.size() > 1000);
        CHECK(result.size() < 2000);
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("Read a TLS socket", "[uv]") {
    RunCoroutine([]() -> Future<void> {
        cerr << "-- Creating TLSStream\n";
        auto tlsSock = mbed::TLSSocket::create();
        tlsSock->bind("example.com", 443);

        cerr << "-- Test Connecting...\n";
        AWAIT tlsSock->open();
        auto tlsStream = tlsSock->stream();

        cerr << "-- Test connected! Writing...\n";
        AWAIT tlsStream->write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

        cerr << "-- Test Reading...\n";
        string result = AWAIT tlsStream->readAll();
        cerr << "-- Test Read: " << result << endl;

        AWAIT tlsStream->close();
        RETURN noerror;
    });
}


TEST_CASE("WebSocket", "[uv]") {
    auto test = []() -> Future<void> {
        ws::ClientWebSocket ws("wss://ws.postman-echo.com/raw");
        cerr << "-- Test Connecting...\n";
        try {
            AWAIT ws.connect();
        } catch (std::exception const& x) {
            cerr << "EXCEPTION: " << x.what() << endl;
            for (auto &h : ws.responseHeaders())
                cout << '\t' << h.first << ": " << h.second << endl;
            FAIL();
        }
        for (auto &h : ws.responseHeaders())
            cout << '\t' << h.first << ": " << h.second << endl;
        cerr << "-- Test Sending Message...\n";
        AWAIT ws.send(ConstBytes("This is a test of WebSockets in Crouton."), ws::Message::Text);

        Generator<ws::Message> receiver = ws.receive();
        cerr << "-- Test Receiving Message...\n";
        auto msg = AWAIT receiver;
        REQUIRE(msg);
        cerr << "-- Received type " << int(msg->type) << ": " << msg << endl;
        CHECK(msg->type == ws::Message::Text);
        CHECK(*msg == "This is a test of WebSockets in Crouton.");

        cerr << "-- Closing...\n";
        AWAIT ws.send(ws::Message(ws::CloseCode::Normal, "bye"));
        msg = AWAIT receiver;
        REQUIRE(msg);
        CHECK(msg->type == ws::Message::Close);
        CHECK(msg->closeCode() == ws::CloseCode::Normal);

        CHECK(ws.readyToClose());
        AWAIT ws.close();
        RETURN noerror;
    };
    test().waitForResult();
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("readdir", "[uv]") {
    cerr << "Dir is " << fs::realpath(".") << endl;
    auto dir = fs::readdir(".");
    while (auto ent = dir.next()) {
        cerr << ent->name << " (" << int(ent->type) << ")\n";
    }
}


#ifdef __APPLE__

static ASYNC<string> readNWSocket(const char* hostname, bool tls) {
    cerr << "Connecting...\n";
    auto socket = apple::NWConnection::create();
    socket->bind(hostname, (tls ? 443 : 80));
    socket->useTLS(tls);
    AWAIT socket->open();

    cerr << "Writing...\n";
    auto stream = socket->stream();
    AWAIT stream->write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    cerr << "Reading...\n";
    string result = AWAIT stream->readAll();
    AWAIT stream->close();
    RETURN result;
}


TEST_CASE("NWConnection", "[nw]") {
    {
        string contents = readNWSocket("example.com", true).waitForResult();
        cerr << "HTTP response:\n" << contents << endl;
        CHECK(contents.starts_with("HTTP/1.1 "));
        CHECK(contents.size() > 1000);
        CHECK(contents.size() < 2000);
    }
    REQUIRE(Scheduler::current().assertEmpty());
}


TEST_CASE("NWConnection TLS", "[nw]") {
    {
        string contents = readNWSocket("example.com", true).waitForResult();
        cerr << "HTTP response:\n" << contents << endl;
        CHECK(contents.starts_with("HTTP/1.1 "));
        CHECK(contents.size() > 1000);
        CHECK(contents.size() < 2000);
    }
    REQUIRE(Scheduler::current().assertEmpty());
}

#endif // __APPLE__
