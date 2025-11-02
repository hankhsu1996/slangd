#include "app/crash_handler.hpp"

#include <cstdlib>

#ifdef __linux__
#include <csignal>
#include <iostream>
#include <stacktrace>
#include <unistd.h>
#endif

namespace app {

#ifdef __linux__
static void HandleFatalSignal(int sig) noexcept {
  std::signal(sig, SIG_DFL);

  // Print stack trace using C++23 std::stacktrace
  std::cerr << "\nFatal signal received\n";
  std::cerr << "Signal: " << sig << "\n";
  std::cerr << "Stack trace:\n";

  try {
    auto trace = std::stacktrace::current();
    std::cerr << trace << "\n";
  } catch (...) {
    std::cerr << "(Failed to capture stack trace)\n";
  }

  std::cerr.flush();

  ::raise(sig);
}
#endif

void InitializeCrashHandlers() {
#ifdef __linux__
  // Install crash handlers for various fatal signals
  std::signal(SIGSEGV, HandleFatalSignal);  // Segmentation fault
  std::signal(SIGFPE, HandleFatalSignal);   // Floating point exception
  std::signal(SIGILL, HandleFatalSignal);   // Illegal instruction
  std::signal(SIGBUS, HandleFatalSignal);   // Bus error
#endif
  // On non-Linux platforms, crash handlers are not implemented
}

void WaitForDebuggerIfRequested() {
#ifdef __linux__
  if (std::getenv("WAIT_FOR_GDB") != nullptr) {
    std::raise(SIGSTOP);
  }
#endif
  // On non-Linux platforms, debugger wait is not implemented
}

}  // namespace app
