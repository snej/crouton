//
// tests.cc
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
#include "crouton/Actor.hh"
#include "crouton/Misc.hh"
#include "crouton/Producer.hh"
#include "crouton/util/Backtrace.hh"
#include "crouton/util/Relation.hh"
#include "crouton/io/uv/UVBase.hh"


static void test_backtrace() {
    {
        cout << "Backtrace with default constructor:\n";
        Backtrace bt;
        bt.writeTo(cout);
        cout << endl;
    }
    {
        cout << "Backtrace with capture:\n";
        auto bt = Backtrace::capture();
        bt->writeTo(cout);
        cout << endl;
    }
}


TEST_CASE("Backtrace") {
    test_backtrace();
}


TEST_CASE("Randomize") {
    uint8_t buf[10];
    ::memset(buf, 0, sizeof(buf));
    for (int pass = 0; pass < 5; ++pass) {
        Randomize(buf, sizeof(buf));
        for (int i = 0; i < 10; ++i)
            printf("%02x ", buf[i]);
        printf("\n");
    }
}


TEST_CASE("Result") {
    Result<bool> result = true;
    CHECK(result.ok());
    CHECK(result);
    CHECK(!result.isError());
    CHECK(result.error() == noerror);
    
    Result<bool> r2 = result;
    CHECK(r2.ok());
}


TEST_CASE("Empty Error", "[error]") {
    Error err;
    CHECK(!err);
    CHECK(err.code() == 0);
    CHECK(err.domain() == "");
    CHECK(err.brief() == "(no error)");
    CHECK(err.description() == "(no error)");
    err.raise_if("shouldn't raise");
}


TEST_CASE("Error", "[error]") {
    Error err(CroutonError::LogicError);
    CHECK(err);
    CHECK(err.code() == errorcode_t(CroutonError::LogicError));
    CHECK(err.domain() == "Crouton");
    CHECK(err.brief() == "Crouton error 6");
    CHECK(err.description() == "internal error (logic error)");
    CHECK(err.is<CroutonError>());
    CHECK(!err.is<io::http::Status>());
    CHECK(err == CroutonError::LogicError);
    CHECK(err.as<CroutonError>() == CroutonError::LogicError);
    CHECK(err.as<io::http::Status>() == io::http::Status{0});
    CHECK(err != io::http::Status::OK);

    Exception x(err);
    CHECK(x.error() == err);
    CHECK(x.what() == string_view("internal error (logic error)"));
}


TEST_CASE("Error Types", "[error]") {
    // Make sure multiple error domains can be registered and aren't confused with each other.
    Error croutonErr(CroutonError::LogicError);
    Error httpError(io::http::Status::NotFound);
    Error wsError(io::ws::CloseCode::ProtocolError);
    CHECK(croutonErr == croutonErr);
    CHECK(httpError != croutonErr);
    CHECK(wsError != httpError);
    CHECK(croutonErr.domain() == "Crouton");
    CHECK(httpError.domain() == "HTTP");
    CHECK(httpError.brief() == "HTTP error 404");
    CHECK(wsError.domain() == "WebSocket");
    CHECK(wsError.brief() == "WebSocket error 1002");
}


TEST_CASE("Exception To Error", "[error]") {
    Error xerr(std::runtime_error("oops"));
    CHECK(xerr);
    CHECK(xerr.domain() == "exception");
    CHECK(xerr.code() == errorcode_t(CppError::runtime_error));
    CHECK(xerr == CppError::runtime_error);
    CHECK(xerr.as<CppError>() == CppError::runtime_error);
}


TEST_CASE("OneToOne") {
    struct Bar;
    struct Foo {
        Foo(string name) :_name(name), _bar(this) { }
        string _name;
        util::OneToOne<Foo,Bar> _bar;
    };
    struct Bar {
        Bar(int size) :_size(size), _foo(this) { }
        int _size;
        util::OneToOne<Bar,Foo> _foo;
    };

    Foo foo("FOO");
    {
        Bar bar(1337);

        CHECK(foo._bar.other() == nullptr);
        CHECK(bar._foo.other() == nullptr);

        foo._bar = &bar._foo;
        CHECK(foo._bar.other() == &bar);
        CHECK(bar._foo.other() == &foo);

        CHECK(foo._bar->_size == 1337);
        CHECK(bar._foo->_name == "FOO");

        Bar bar2(std::move(bar));
        CHECK(foo._bar.other() == &bar2);
        CHECK(bar2._foo.other() == &foo);
    }
    CHECK(foo._bar.other() == nullptr);
}


TEST_CASE("ToMany") {
    struct Member;
    struct Band {
        Band(string name) :_name(name), _members(this) { }
        string _name;
        util::ToMany<Band,Member> _members;
    };
    struct Member {
        Member(string name) :_name(name), _band(this) { }
        string _name;
        util::ToOne<Member,Band> _band;
    };

    Band beatles("The Beatles");
    CHECK(beatles._members.empty());

    {
        Member ringo("Ringo");
        CHECK(ringo._band.other() == nullptr);

        beatles._members.push_back(ringo._band);
        CHECK(!beatles._members.empty());
        CHECK(ringo._band.other() == &beatles);

        Member john("John");
        Member paul("Paul");
        Member george("George");

        beatles._members.push_back(john._band);
        beatles._members.push_back(paul._band);
        beatles._members.push_back(george._band);
        CHECK(john._band.other() == &beatles);
        CHECK(paul._band.other() == &beatles);
        CHECK(george._band.other() == &beatles);

        std::vector<string> names;
        for (auto& member : beatles._members)
            names.push_back(member._name);
        CHECK(names == std::vector<string>{"Ringo", "John", "Paul", "George"});

        Band wings("Wings");
        paul._band = &wings._members;
        CHECK(paul._band.other() == &wings);

        names.clear();
        for (auto& member : beatles._members)
            names.push_back(member._name);
        CHECK(names == std::vector<string>{"Ringo", "John", "George"});

        beatles._members.erase(john._band);
        CHECK(john._band.other() == nullptr);

        names.clear();
        for (auto& member : beatles._members)
            names.push_back(member._name);
        CHECK(names == std::vector<string>{"Ringo", "George"});
    }
    CHECK(beatles._members.empty());
}


void RunCoroutine(std::function<Future<void>()> test) {
    InitLogging();
    Future<void> f = test();
    Scheduler::current().runUntil([&]{return f.hasResult();});
    f.result(); // check exception
}


TEST_CASE("Producer Consumer") {
    RunCoroutine([]() -> Future<void> {
        std::optional<SeriesProducer<int>> producer;
        producer.emplace();
        std::unique_ptr<SeriesConsumer<int>> consumer = producer->make_consumer();

        auto producerTaskFn = [&]() -> Task {
            for (int i = 1; i <= 10; i++) {
                cerr << "Produce " << i << "...\n";
                bool ok = AWAIT producer->produce(i);
                CHECK(ok);
            }
            cerr << "Produce EOF...\n";
            bool ok = AWAIT producer->produce(noerror);
            CHECK(!ok);
            cerr << "END producer\n";
            producer = std::nullopt;
        };

        Task producerTask = producerTaskFn();

        int expected = 1;
        while (true) {
            Result<int> r = AWAIT *consumer;
            cerr << "Received " << r << endl;
            if (!r.ok()) {
                CHECK(r.error() == noerror);
                break;
            }
            CHECK(r.value() == expected++);
        }
        CHECK(expected == 11);
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}


#if 0
static ASYNC<void> waitFor(chrono::milliseconds ms) {
    FutureProvider<void> f;
    Timer::after(ms.count() / 1000.0, [f] {
        cerr << "\tTimer fired\n";
        f.setResult();
    });
    return f;
}


static Generator<int> waiter(string name) {
    for (int i = 1; i < 4; i++) {
        cerr << "waiter " << name << " generating " << i << endl;
        YIELD i;
        cerr << "waiter " << name << " waiting..." << endl;

        AWAIT waitFor(500ms);
    }
    cerr << "waiter " << name << " done" << endl;
}


TEST_CASE("Waiter coroutine") {
    {
        Generator<int> g1 = waiter("first");

        for (int i : g1)
            cerr << "--received " << i << endl;
    }
    REQUIRE(Scheduler::current().assertEmpty());
}


static ASYNC<int> futuristicSquare(int n) {
    AWAIT waitFor(500ms);
    RETURN n * n;
}

TEST_CASE("Future coroutine") {
    {
        auto f = futuristicSquare(12);
        int sqr = f.waitForValue();
        CHECK(sqr == 144);
    }
    REQUIRE(Scheduler::current().assertEmpty());
}
#endif


#ifdef __clang__    // GCC 12 doesn't seem to support ActorImpl ctor taking parameters

// An example Generator of Fibonacci numbers.
static Generator<int64_t> fibonacci(int64_t limit) {
    int64_t a = 1, b = 1;
    YIELD a;
    while (b <= limit) {
        YIELD b;
        std::tie(a, b) = std::pair{b, a + b};
    }
}


class TestActor : public Actor {
public:
    Future<int64_t> fibonacciSum(int n) const {
        cerr << "---begin fibonacciSum(" << n << ")\n";
        int64_t sum = 0;
        auto fib = fibonacci(INT_MAX);
        for (int i = 0; i < n; i++) {
            AWAIT Timer::sleep(0.1);
            Result<int64_t> f;
            if ((f = AWAIT fib))
                sum += f.value();
            else
                break;
        }
        cerr << "---end fibonacciSum(" << n << ") returning " << sum << "\n";
        RETURN sum;
    }
};


TEST_CASE("Actor") {
    RunCoroutine([]() -> Future<void> {
        auto actor = std::make_shared<TestActor>();
        cerr << "actor = " << (void*)actor.get() << endl;
        Future<int64_t> sum10 = actor->fibonacciSum(10);
        Future<int64_t> sum20 = actor->fibonacciSum(20);
        cerr << "Sum10 is " << (AWAIT sum10) << endl;
        cerr << "Sum20 is " << (AWAIT sum20) << endl;
        RETURN noerror;
    });
    REQUIRE(Scheduler::current().assertEmpty());
}

#endif // __clang__
