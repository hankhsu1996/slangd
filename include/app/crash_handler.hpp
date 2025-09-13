#pragma once

namespace app {

/// Initialize crash signal handlers for debugging
/// Installs handlers for SIGSEGV, SIGFPE, SIGILL, and SIGBUS that print
/// stack traces with symbol demangling when available
void InitializeCrashHandlers();

/// Wait for debugger attachment if WAIT_FOR_GDB environment variable is set
/// Raises SIGSTOP to pause execution for debugger attachment
void WaitForDebuggerIfRequested();

}  // namespace app