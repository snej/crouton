//
// Created by Jens Alfke on 1/1/26.
//

#include "crouton/io/Subprocess.hh"
#include "crouton/io/Stream.hh"
#include "UVInternal.hh"
#include <unistd.h>


namespace crouton::io {
    using namespace std;
    using namespace crouton::io::uv;


    static_assert(int(stdio::in) == STDIN_FILENO);
    static_assert(int(stdio::out) == STDOUT_FILENO);
    static_assert(int(stdio::err) == STDERR_FILENO);


    struct Subprocess::impl : uv_process_s {
        explicit impl(Subprocess* parent)
        :uv_process_s{}
        ,_parent(parent)
        { }

        static void onExit(uv_process_t* process, int64_t exit_status, int /*term_signal*/) {
            auto self = static_cast<impl*>(process);
            self->onExit(exit_status);
            closeHandle(self);
        }

        void onExit(int64_t exit_status) {
            if (_parent) {
                _parent->_process = nullptr;
                _parent = nullptr;
            }
            _futureP->setResult(exit_status);
        }

        Subprocess*             _parent;
        FutureProvider<int64_t> _futureP = Future<int64_t>::provider();
    };


    Subprocess::Subprocess(string path)
    :_path(std::move(path)) {
        args.push_back(_path);
    }


    Subprocess::Subprocess(string path, std::initializer_list<string> args_)
    :args(std::move(args_))
    ,_path(std::move(path)) {
        args.insert(args.begin(), _path);
    }


    Subprocess::~Subprocess() {
        if (_process)
            _process->_parent = nullptr;
    }


    void Subprocess::inherit(stdio io) {
        _inherit[int(io)] = true;
        _redirect[int(io)] = nullptr;
    }


    void Subprocess::redirect(stdio io, Stream* s) {
        precondition(s->isOpen());
        _redirect[int(io)] = s;
        _inherit[int(io)] = false;
    }


    Future<int64_t> Subprocess::execute() {
        precondition(!isRunning());
        precondition(!args.empty());

        vector<const char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& arg : args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        uv_process_options_t options = {
            .exit_cb = &impl::onExit,
            .file    = _path.c_str(),
            .args    = const_cast<char**>(argv.data()),
        };

        uv_stdio_container_t child_stdio[3] = {};
        static_assert(UV_IGNORE == 0);
        for (int fd = 0; fd < 3; fd++) {
            if (_inherit[fd]) {
                child_stdio[fd].flags = UV_INHERIT_FD;
                child_stdio[fd].data.fd = fd;
            } else if (_redirect[fd]) {
                child_stdio[fd].flags = UV_INHERIT_STREAM;
                auto stream = _redirect[fd]->uv_stream();
                precondition(stream != nullptr);
                child_stdio[fd].data.stream = stream;
            }
        };
        options.stdio = child_stdio;
        options.stdio_count = std::size(child_stdio);

        auto process = new impl(this);
        if (int err = uv_spawn(curLoop(), process, &options)) {
            closeHandle(process);
            return Error(UVError(err), "launching a subprocess");
        }
        process->data = this;
        _process = process;
        return Future(_process->_futureP);
    }


    int Subprocess::pid() const noexcept {
        return _process ? _process->pid : -1;
    }


    void Subprocess::kill(int signalNumber) {
        precondition(isRunning());
        int err = uv_process_kill(_process, signalNumber);
        if (err != UV_ESRCH)
            check(err, "killing subprocess");
    }

}
