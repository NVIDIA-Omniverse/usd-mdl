//
// SPDX-License-Identifier: Apache-2.0
//
// Shared exit helper for the usdMdl C++ test executables.
//
// Most C++ tests call std::_Exit (or TerminateProcess on Windows) instead
// of returning from main() because the MDL SDK's shutdown sequence can
// crash during late DLL teardown (see testMdlRuntimeShutdown.cpp for the
// full diagnosis -- it's specifically about ExitProcess re-entering MDL
// SDK code after the host has already destroyed its world).
//
// _Exit and TerminateProcess both skip atexit() handlers, which is the
// whole point on production runs.

#pragma once

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mdl_test {

/// Exit \p code without running C++ destructors or atexit handlers.
/// Replaces the inline `_Exit` / `TerminateProcess` calls that used to
/// live at the bottom of each test's `main()`.
[[noreturn]] inline void
FlushAndExit(int code)
{
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(),
                     static_cast<unsigned int>(code));
#else
    std::_Exit(code);
#endif

    // Unreachable; keeps the compiler happy about [[noreturn]] on
    // platforms where TerminateProcess isn't marked noreturn.
    for (;;) {}
}

} // namespace mdl_test
