//
// UVBase.cc
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

#include "io/UVBase.hh"
#include "EventLoop.hh"
#include "Logging.hh"
#include "Task.hh"
#include "UVInternal.hh"
#include <charconv>
#include <cmath>

namespace crouton {
    ConstBytes::ConstBytes(uv_buf_t buf) :Bytes((byte*)buf.base, buf.len) { }
    MutableBytes::MutableBytes(uv_buf_t buf) :Bytes((byte*)buf.base, buf.len) { }

    ConstBytes::operator uv_buf_t() const   { return uv_buf_init((char*)data(), unsigned(size())); }
    MutableBytes::operator uv_buf_t() const { return uv_buf_init((char*)data(), unsigned(size())); }

    string ErrorDomainInfo<io::UVError>::description(errorcode_t code) {
        switch (code) {
            case UV__EAI_NONAME:    return "unknown host";
            default:                return uv_strerror(code);
        }
    };
}

namespace crouton::io {
    using namespace std;

    
    /** Implementation of EventLoop for libuv.*/
    class UVEventLoop final : public EventLoop {
    public:
        UVEventLoop();
        void run() override;
        bool runOnce(bool waitForIO =true) override;
        void stop(bool threadSafe) override;
        void perform(std::function<void()>) override;

        ASYNC<void> sleep(double delaySecs);

        void ensureWaits();
        uv_loop_s* uvLoop() {return _loop.get();}
    private:
        bool _run(int mode);

        std::unique_ptr<uv_loop_s> _loop;
        std::unique_ptr<uv_async_s> _async;
        std::unique_ptr<uv_timer_s> _distantFutureTimer;
    };


    UVEventLoop::UVEventLoop()
    :_loop(make_unique<uv_loop_t>())
    ,_async(make_unique<uv_async_t>())
    {
        check(uv_loop_init(_loop.get()), "initializing the event loop");
        _loop->data = this;

        uv_async_cb stopCallback = [](uv_async_t *async) {
            uv_stop((uv_loop_t*)async->data);
        };
        check(uv_async_init(_loop.get(), _async.get(), stopCallback), "initializing the event loop");
        _async->data = _loop.get();
    }

    void UVEventLoop::ensureWaits() {
        // Create a timer with an extremely long period,
        // so the event loop always has something to wait on.
        _distantFutureTimer = make_unique<uv_timer_t>();
        uv_timer_init(uvLoop(), _distantFutureTimer.get());
        auto callback = [](uv_timer_t *handle){ };
        uv_timer_start(_distantFutureTimer.get(), callback, 1'000'000'000, 1'000'000'000);
    }

    bool UVEventLoop::_run(int mode)  {
        NotReentrant nr(_running);
        LLoop->debug("Running as {}blocking (alive={})",
                    (mode==UV_RUN_NOWAIT ? "non" : ""), (bool)uv_loop_alive(_loop.get()));
        auto ns = uv_hrtime();
        int status = uv_run(_loop.get(), uv_run_mode(mode));
        ns = uv_hrtime() - ns;
        LLoop->debug("...stopped after {}ms, status={}", (ns / 1000000), status);
        return status != 0;
    }

    void UVEventLoop::run()  {
        _run(UV_RUN_DEFAULT);
    }

    bool UVEventLoop::runOnce(bool waitForIO)  {
        return _run(waitForIO ? UV_RUN_ONCE : UV_RUN_NOWAIT);
    }

    void UVEventLoop::stop(bool threadSafe) {
        if (threadSafe)
            uv_async_send(_async.get());
        else
            uv_stop(_loop.get());
    }

    void UVEventLoop::perform(std::function<void()> fn) {
        struct uvAsyncFn : public uv_async_t {
            uvAsyncFn(std::function<void()> &&fn) :_fn(std::move(fn)) { }
            std::function<void()> _fn;
        };

        LLoop->info("Scheduler::onEventLoop()");
        auto async = new uvAsyncFn(std::move(fn));
        check(uv_async_init(_loop.get(), async, [](uv_async_t *async) noexcept {
            auto self = static_cast<uvAsyncFn*>(async);
            try {
                self->_fn();
            } catch (...) {
                LLoop->error("*** Caught unexpected exception in onEventLoop callback ***");
            }
            closeHandle(self);
        }), "making an async call");
        check(uv_async_send(async), "making an async call");
    }



    uv_loop_s* curLoop() {
        return ((UVEventLoop&)Scheduler::current().eventLoop()).uvLoop();
    }


    void Randomize(void* buf, size_t len) {
        uv_random_t req;
        check(uv_random(curLoop(), &req, buf, len, 0, nullptr), "generating random bytes");
    }

}


namespace crouton {
    using namespace std;

    EventLoop* Scheduler::newEventLoop() {
        auto loop = new io::UVEventLoop();
        loop->ensureWaits();
        return loop;
    }


    static uint64_t ms(double secs){
        return uint64_t(::round(max(secs, 0.0) * 1000.0));
    }


    Timer::Timer(std::function<void()> fn)
    :_fn(std::move(fn))
    ,_handle(new uv_timer_t)
    {
        uv_timer_init(io::curLoop(), _handle);
        _handle->data = this;
    }


    Timer::~Timer() {
        uv_timer_stop(_handle);
        io::closeHandle(_handle);
    }


    void Timer::_start(double delaySecs, double repeatSecs) {
        auto callback = [](uv_timer_t *handle) noexcept {
            auto self = (Timer*)handle->data;
            try {
                self->_fn();
            } catch (...) {
                LLoop->error("*** Caught unexpected exception in Timer callback ***");
            }
            if (self->_deleteMe)
                delete self;
        };
        uv_timer_start(_handle, callback, ms(delaySecs), ms(repeatSecs));
    }


    void Timer::stop() {
        uv_timer_stop(_handle);
    }


    /*static*/ void Timer::after(double delaySecs, std::function<void()> fn) {
        auto t = new Timer(std::move(fn));
        t->_deleteMe = true;
        t->once(delaySecs);
    }


    Future<void> Timer::sleep(double delaySecs) {
        auto provider = make_shared<FutureState<void>>();
        Timer::after(delaySecs, [provider]{provider->setResult();});
        return Future<void>(provider);
    }


    struct QueuedWork : public uv_work_t {
        FutureProvider<void>    provider = std::make_shared<FutureState<void>>();
        std::function<void()>   fn;
        std::exception_ptr      exception;
    };

    Future<void> OnBackgroundThread(std::function<void()> fn) {
        auto work = new QueuedWork{.fn = std::move(fn)};
        io::check(uv_queue_work(io::curLoop(), work, [](uv_work_t *req) noexcept {
            auto work = static_cast<QueuedWork*>(req);
            try {
                work->fn();
            } catch (...) {
                work->exception = std::current_exception();
            }
        }, [](uv_work_t *req, int status) noexcept {
            auto work = static_cast<QueuedWork*>(req);
            if (work->exception)
                work->provider->setResult(Error(work->exception));
            else
                work->provider->setResult();
            delete work;
        }), "making a background call");
        return Future<void>(work->provider);
    }

}