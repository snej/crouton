//
// Created by Jens Alfke on 1/1/26.
//

#pragma once
#include "crouton/Future.hh"

struct uv_process_s;

namespace crouton::io {
    class Stream;

    enum class stdio {in = 0, out = 1, err = 2};

    /** Executes a process, like the `exec` family of system calls. */
    class Subprocess {
    public:
        explicit Subprocess(string path);
        explicit Subprocess(string path, std::initializer_list<string> args);

        virtual ~Subprocess();

        /// The command-line arguments (`argv`) passed to the process.
        /// Initialized with one element, the path. You may append more before calling `execute`.
        std::vector<string> args;

        /// Causes the subprocess to inherit the corresponding stdio stream;
        /// by default they all redirect to `/dev/null`.
        void inherit(stdio);

        /// Redirects a stdio file descriptor in the subprocess to an existing open Stream instance.
        void redirect(stdio, Stream*);

        /// Launches the process, asynchronously. The returned Future resolves when the process exits,
        /// and its value is the process's exit status, 0 on success.
        /// This Subprocess object may be deleted / go out of scope after this method is called;
        /// it doesn't affect the child process or the Future.
        /// @warning  It is illegal to call this while a subprocess is already running.
        Future<int64_t> execute();

        /// True if the subprocess is running.
        bool isRunning() const noexcept {return _process != nullptr;}

        /// The subprocess's process ID, or -1 if not running.
        int pid() const noexcept;

        /// Sends a signal to the subprocess, which may or may not cause it to exit.
        void kill(int signalNumber);

    private:
        struct impl;

        string const            _path;
        std::array<bool,3>      _inherit = {};
        std::array<Stream*,3>   _redirect = {};
        impl*                   _process = nullptr;
    };

}
