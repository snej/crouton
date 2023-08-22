//
// UVBase.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "EventLoop.hh"
#include <stdexcept>
#include <functional>

struct uv_loop_s;
struct uv_timer_s;

namespace snej::coro {
    class Task;
}

namespace snej::coro::uv {

    /** Low-level struct pointing to the destination of a read. Binary compatible with uv_buf_t. */
    struct ReadBuf {
        void*   base = nullptr;
        size_t  len = 0;
    };

    /** Low-level struct pointing to the source of a write. Binary compatible with uv_buf_t. */
    struct WriteBuf {
        const void* base = nullptr;
        size_t      len = 0;
    };
    //TODO //FIXME: uv_buf_t's fields are in the opposite order on Windows. Deal with that.


    /** Exception thrown by libuv errors. */
    class UVError : public std::runtime_error {
    public:
        explicit UVError(const char* what, int err);
        const char* what() const noexcept override;

        int err; ///< libuv error code
    private:
        mutable std::string _message;
    };


    /** Main process function that runs an event loop. */
    int UVMain(int argc, const char * argv[], std::function<void()>);

    /// Process arguments, as captured by UVMain.
    extern std::vector<std::string_view> UVArgs;


    class UVEventLoop final : public EventLoop {
    public:
        UVEventLoop();
        void run() override;
        void runOnce(bool waitForIO =true) override;
        void stop() override;
        void perform(std::function<void()>) override;

        uv_loop_s* uvLoop() {return _loop.get();}
    private:
        void _run(int mode);

        std::unique_ptr<uv_loop_s> _loop;
    };


    /** A repeating or one-shot timer. */
    class Timer {
    public:
        Timer(std::function<void()> fn);
        ~Timer();

        /// Calls the function once after a delay.
        void once(double delaySecs)                         {_start(delaySecs, 0);}

        /// Calls the function repeatedly.
        void start(double intervalSecs)                     {_start(intervalSecs, intervalSecs);}

        /// Calls the function repeatedly after a delay.
        void start(double delaySecs, double intervalSecs)   {_start(delaySecs, intervalSecs);}

        /// Stops any future calls. The timer's destruction also stops calls.
        void stop();

        /// Static method, that calls the given function after the given delay.
        static void after(double delaySecs, std::function<void()> fn);

    private:
        void _start(double delaySecs, double repeatSecs);

        std::function<void()>   _fn;
        uv_timer_s*             _handle = nullptr;
        bool                    _deleteMe = false;
    };


    /// Calls the given function on the next iteration of this thread's libuv event loop.
    void OnEventLoop(Scheduler&, std::function<void()>);
    void OnEventLoop(std::function<void()> fn);


    /// Calls the given function on a background thread managed by libuv.
    [[nodiscard]] Future<void> OnBackgroundThread(std::function<void()> fn);


    /// Calls the given function on a background thread managed by libuv,
    /// returning its value (or exception) asynchronously.
    template <typename T>
    [[nodiscard]] Future<T> OnBackgroundThread(std::function<T()> fn) {
        std::optional<T> result;
        AWAIT OnBackgroundThread([&]() -> void {
            result = fn();
        });
        RETURN std::move(result.value());
    }
}