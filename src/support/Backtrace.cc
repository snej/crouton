//
// Backtrace.cc
//
// 
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

#include "crouton/util/Backtrace.hh"
#include "support/StringUtils.hh"
#include "crouton/util/betterassert.hh"
#include "crouton/util/MiniOStream.hh"
#include "crouton/util/MiniFormat.hh"
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>

namespace crouton {
    using namespace std;


    shared_ptr<Backtrace> Backtrace::capture(unsigned skipFrames, unsigned maxFrames) {
        // (By capturing afterwards, we avoid the (many) stack frames associated with make_shared)
        auto bt = make_shared<Backtrace>(0, 0);
        bt->_capture(skipFrames + 1, maxFrames);
        return bt;
    }


    // If any of these strings occur in a backtrace, suppress further frames.
    static constexpr const char* kTerminalFunctions[] = {
        "_C_A_T_C_H____T_E_S_T_",
        "Catch::(anonymous namespace)::TestInvokerAsFunction::invoke() const",
    };

    static constexpr struct {const char *old, *nuu;} kAbbreviations[] = {
        {"(anonymous namespace)",   "(anon)"},
        {"std::__1::",              "std::"},
        {"std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
                                    "string"},
    };


    bool Backtrace::writeTo(crouton::mini::ostream &out) const {
        size_t n = size();
        for (unsigned i = 0; i < n; ++i) {
            auto frame = getFrame(i);
            bool stop = false;

            if (i > 0)
                out << '\n';
            crouton::mini::format_to(out, "\t{:2d}  ", i);
            if (frame.library)
                crouton::mini::format_to(out, "{:<25s} ", frame.library);
            if (frame.function) {
                string name = Unmangle(frame.function);
                // Stop when we hit a unit test, or other known functions:
                for (auto fn : kTerminalFunctions) {
                    if (name.find(fn) != string::npos)
                        stop = true;
                }
                // Abbreviate some C++ verbosity:
                for (auto &abbrev : kAbbreviations)
                    crouton::replaceStringInPlace(name, abbrev.old, abbrev.nuu);
                out << name;
            }
            if (frame.filename)
                crouton::mini::format_to(out, " // {}:{}", frame.filename, frame.line);
            else
                crouton::mini::format_to(out, " + {}", frame.offset);
            if (stop) {
                out << "\n\t ... (" << (n - i - 1) << " more suppressed) ...";
                break;
            }
        }
        return true;
    }


    string Backtrace::toString() const {
        crouton::mini::stringstream out;
        writeTo(out);
        return out.str();
    }


    bool Backtrace::writeTo(std::ostream &out) const {
        out << toString();
        return true;
    }


#pragma mark - CRASH LOG:


    void Backtrace::writeCrashLog(std::ostream &out) {
        Backtrace bt(4);
        auto xp = current_exception();
        if (xp) {
            out << "Uncaught exception:\n\t";
            try {
                rethrow_exception(xp);
            } catch(const exception& x) {
#if __cpp_rtti
                const char *name = typeid(x).name();
                out << Unmangle(name) << ": " <<  x.what() << "\n";
#else
                out << x.what() << "\n";
#endif
            } catch (...) {
                out << "unknown exception type\n";
            }
        }
        out << "Backtrace:\n" << bt.toString();
    }


    void Backtrace::installTerminateHandler(function<void(const string&)> logger) {
        static once_flag sOnce;
        call_once(sOnce, [&] {
            static auto const sLogger = std::move(logger);
            static terminate_handler const sOldHandler = set_terminate([] {
                // ---- Code below gets called by C++ runtime on an uncaught exception ---
                if (sLogger) {
                    std::stringstream out;
                    writeCrashLog(out);
                    sLogger(out.str());
                } else {
                    crouton::mini::cerr << "\n\n******************** C++ fatal error ********************\n";
                    writeCrashLog(std::cerr);
                    crouton::mini::cerr << "\n******************** Now terminating ********************\n";
                }
                // Chain to old handler:
                sOldHandler();
                // Just in case the old handler doesn't abort:
                abort();
                // ---- End of handler ----
            });
        });
    }

}
