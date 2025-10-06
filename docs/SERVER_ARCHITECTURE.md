# Server Architecture

## Design Principles

**Layered Architecture**: Clean separation between protocol, domain logic, and business operations enables extensibility and testability.

**Async-First**: All operations are awaitable to prevent blocking the main event loop, essential for responsive LSP servers.

**Dependency Injection**: Components depend on abstractions, not implementations, allowing different service strategies.

## Component Layers

```
JSON-RPC Transport ← VSCode
         ↓
   Generic LSP Server (protocol handling)
         ↓
SystemVerilog LSP Server (domain-specific handlers)
         ↓
  Language Service Interface (business operations)
         ↓
   Background Thread Pool (heavy computation)
```

### Transport & Protocol Layer

- **FramedPipeTransport**: Handles JSON-RPC communication with VSCode
- **RpcEndpoint**: Generic JSON-RPC request/response processing
- **lsp::LspServer**: Base LSP protocol implementation with file tracking

### Domain Layer

- **SlangdLspServer**: SystemVerilog-specific LSP handlers
- **Strand coordination**: Ensures thread-safe LSP state management
- **Request debouncing**: Prevents excessive diagnostic requests

### Business Logic Layer

- **LanguageServiceBase**: Abstract interface for all LSP operations
- **LanguageService**: Concrete implementation using overlay sessions
- **Extensible design**: Supports future implementations (e.g., persistent indexing)

### Computation Layer

- **Background thread pool**: Isolates expensive SystemVerilog compilation
- **Async dispatch**: Operations complete without blocking main thread
- **Result coordination**: Thread-safe handoff of computed results

## Request Flow Pattern

```
VSCode Request → JSON-RPC → LSP Handler → Language Service → Background Pool
                                                              ↓
VSCode Response ← JSON-RPC ← LSP Handler ← Result ← Computation Complete
```

**Key insight**: Main thread handles protocol coordination while background threads handle computation. This prevents LSP timeouts during heavy SystemVerilog processing.

## Diagnostic Publishing Strategy

**Two-phase approach**: Parse diagnostics (syntax errors) publish immediately, then full diagnostics (semantic analysis) publish after elaboration.

```
File Save → Parse Diagnostics (fast) → Publish → Full Diagnostics (slow) → Publish
```

### Key Architectural Decisions

**Lazy elaboration**: OverlaySession creation does NOT trigger elaboration. Semantic indexing uses `compilation.getDefinitions()` and `compilation.getPackages()` - simple map lookups, no analysis.

**On-demand extraction**: Diagnostics extracted only when requested, not pre-computed during session creation. Two extraction methods:

- `getParseDiagnostics()`: Fast, no elaboration triggered
- `getAllDiagnostics()`: Triggers elaboration for semantic analysis

**Cache reuse**: Both diagnostic phases use the same cached OverlaySession. First phase may create session, second phase reuses it.

**Why this works**: Separating diagnostic extraction from session creation follows Slang's lazy evaluation pattern. Users get immediate syntax error feedback while semantic analysis runs in background.

## Executor & Threading Model

### Main Event Loop (io_context)

- **Single-threaded**: All LSP protocol handling, file tracking, and cache management
- **Strand coordination**: `asio::strand` serializes access to shared state
- **Non-blocking**: Uses `co_await` for all potentially slow operations

### Background Thread Pool Pattern

```cpp
// Dispatch expensive work to background threads
auto result = co_await asio::co_spawn(
    thread_pool_->get_executor(),
    [data]() -> asio::awaitable<Result> {
        // Heavy computation runs here (SystemVerilog compilation)
        co_return DoExpensiveWork(data);
    },
    asio::use_awaitable);

// Post result back to main thread for cache/protocol handling
co_await asio::post(main_executor_, asio::use_awaitable);
```

### Why This Pattern Works

- **Isolation**: Background threads only access immutable data (no shared state)
- **Coordination**: Results posted back to main thread for cache updates
- **Responsiveness**: Main thread stays available for new LSP requests
- **Concurrency**: Multiple SystemVerilog files can compile simultaneously

### Async Operation Flow

1. **LSP Request**: Arrives on main thread via JSON-RPC
2. **Cache Check**: Main thread checks LRU cache (fast path)
3. **Background Dispatch**: Cache miss triggers `co_spawn` to thread pool
4. **Compilation**: SystemVerilog parsing/analysis runs on background thread
5. **Result Handoff**: `asio::post` switches context back to main thread
6. **Cache Update**: Main thread adds result to LRU cache
7. **LSP Response**: Main thread sends response to VSCode

**Critical insight**: The `co_await` + `asio::post` pattern enables true async without breaking thread safety.

## State Management

### Thread Safety Strategy

- **Main thread**: All LSP protocol state (open files, configurations)
- **Strand serialization**: Ensures consistent state access across async operations
- **Background isolation**: Computation threads access only immutable data

### Caching Pattern

- **LRU overlay cache**: Avoids recompilation of frequently accessed files
- **Version-aware keys**: Invalidates cache when content or configuration changes
- **Main thread coordination**: All cache operations happen on protocol thread

### Pending Session Tracking

**Problem**: Concurrent requests for the same file create duplicate sessions. When a file save triggers parse diagnostics and VSCode simultaneously requests document symbols, both check cache (miss), both dispatch session creation to thread pool, resulting in duplicate 500ms compilations.

**Solution**: Track pending session creations using `asio::experimental::channel` for async signaling.

```cpp
// First request: registers pending creation
pending_creations_[key] = PendingCreation{channel};
auto session = co_await CreateOverlaySession(...);
channel->try_send(session);  // Signal all waiters
channel->close();

// Second request: waits for completion
if (pending = pending_creations_[key]) {
    auto session = co_await channel->async_receive();  // Suspends until signaled
    return session;
}
```

**Key benefits**:

- **Event-driven waiting**: Uses channel signaling instead of polling, true async coordination
- **No duplicate work**: Second request receives result from first, saving 500ms compilation time
- **Thread-safe**: All pending tracking happens on main strand, channel provides async notification
- **Minimal overhead**: Waiters resume immediately when channel signals completion

**Design rationale**: Follows async-first principle by leveraging ASIO's channel primitive for coroutine coordination. The channel acts as a one-shot completion event, allowing multiple waiters to efficiently suspend until session creation completes.

## Extension Patterns

### Adding New LSP Features

1. Add method to `LanguageServiceBase` interface
2. Implement in `LanguageService` (may use background dispatch)
3. Add handler to `SlangdLspServer` (coordinate with strand)
4. Register handler in base `LspServer` framework

### Alternative Service Implementations

The `LanguageServiceBase` abstraction enables different strategies:

- **Current**: Per-request overlay compilation with caching
- **Future**: Persistent global index with incremental updates
- **Hybrid**: Fast local cache with background global indexing

## Why This Architecture?

**Responsiveness**: Background compilation prevents LSP timeouts during heavy processing

**Scalability**: Thread pool enables concurrent processing of multiple files

**Maintainability**: Clear layer separation makes features easier to add and test

**Flexibility**: Abstract interfaces enable architectural evolution without breaking changes
