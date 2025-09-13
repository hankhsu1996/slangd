#include "app/crash_handler.hpp"

#ifdef __linux__
#include <array>
#include <csignal>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <memory>
#include <string_view>
#include <unistd.h>
#endif

#include <cstdlib>

namespace app {

#ifdef __linux__
static void PrintFuncNames(int skip = 1, int max_frames = 32) noexcept {
  std::array<void*, 128> frames{};
  const int n = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  if (n <= 0) {
    return;
  }

  constexpr std::string_view kHdr = "Fatal signal received\n";
  (void)(::write(STDERR_FILENO, kHdr.data(), kHdr.size()) == 0);

  const int begin = std::clamp(skip, 0, n);
  const int count = std::max(0, std::min(max_frames, n - begin));
  auto used =
      std::span<void*>{frames}
          .first(static_cast<size_t>(n))
          .subspan(static_cast<size_t>(begin), static_cast<size_t>(count));

  size_t idx = 0;
  for (void* addr : used) {
    Dl_info info{};
    if (::dladdr(addr, &info) != 0 && info.dli_sname != nullptr) {
      // Try to demangle into a preallocated buffer to avoid heap allocs.
      std::array<char, 1024> dem_buf{};
      size_t dem_size = dem_buf.size();
      int status = 0;
      char* raw = abi::__cxa_demangle(
          info.dli_sname, dem_buf.data(), &dem_size, &status);
      // Own only if __cxa_demangle allocated a new buffer.
      std::unique_ptr<char, void (*)(void*)> guard(
          ((raw != nullptr) && raw != dem_buf.data()) ? raw : nullptr,
          std::free);
      const char* name =
          (status == 0 && (raw != nullptr)) ? raw : info.dli_sname;

      std::array<char, 512> line{};
      int written =
          ::snprintf(line.data(), line.size(), "  [%zu] %s\n", idx, name);
      if (written > 0) {
        const size_t len =
            static_cast<size_t>(std::min<int>(written, line.size()));
        (void)(::write(STDERR_FILENO, line.data(), len) == 0);
      }
    } else {
      std::array<char, 64> line{};
      int written = ::snprintf(line.data(), line.size(), "  [%zu] ??\n", idx);
      if (written > 0) {
        const size_t len =
            static_cast<size_t>(std::min<int>(written, line.size()));
        (void)(::write(STDERR_FILENO, line.data(), len) == 0);
      }
    }
    ++idx;
  }
}

static void HandleSegfault(int sig) noexcept {
  std::signal(sig, SIG_DFL);
  PrintFuncNames(1, 24);
  ::raise(sig);
}
#endif

void InitializeCrashHandlers() {
#ifdef __linux__
  // Install crash handlers for various fatal signals
  std::signal(SIGSEGV, HandleSegfault);  // Segmentation fault
  std::signal(SIGFPE, HandleSegfault);   // Floating point exception
  std::signal(SIGILL, HandleSegfault);   // Illegal instruction
  std::signal(SIGBUS, HandleSegfault);   // Bus error
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