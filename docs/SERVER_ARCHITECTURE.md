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
- **LanguageService**: Concrete implementation using SessionManager for lifecycle
- **SessionManager**: Centralized session creation, caching, and invalidation
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

**File-scoped elaboration**: `SemanticIndex::FromCompilation()` calls `forceElaborate()` on each instance, populating `compilation.diagMap` with semantic diagnostics. This is file-scoped (not full design elaboration).

**On-demand extraction**: Diagnostics live in `compilation.diagMap` and are extracted when requested via `ComputeDiagnostics()`:

- `ExtractParseDiagnostics()`: Parse/syntax errors from syntax trees
- `ExtractCollectedDiagnostics()`: Semantic errors from diagMap (already populated)

**Two-phase channels**: `SessionManager` signals `compilation_ready` after elaboration (Phase 1) and `session_ready` after indexing (Phase 2). Diagnostics wait on Phase 1 only (faster).

**Why this works**: `forceElaborate()` caches symbol resolutions that `visit()` reuses for indexing. No duplicate work, and diagnostics get faster response by not waiting for full indexing.

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

### Session Lifecycle Management

**Ownership Hierarchy**:

```
SlangdLspServer (LSP protocol layer)
    │ owns
    └── LanguageService (business logic - public API)
            │ owns (private implementation detail)
            └── SessionManager (resource lifecycle management)
                    │ creates/caches
                    └── OverlaySession (data: compilation + index)
```

**Responsibilities**:

- **SlangdLspServer**: LSP protocol, delegates to LanguageService
- **LanguageService**: Public API, feature implementations, owns SessionManager
- **SessionManager**: Session lifecycle (create/cache/invalidate), returns OverlaySession
- **OverlaySession**: Data class (Compilation + SemanticIndex + SourceManager)

**Architecture**: SessionManager centralizes all session lifecycle (create/cache/invalidate). LanguageService features are read-only consumers.

**Event-driven model**:

```
SlangdLspServer
  ├─ Document Events (protocol-level API)
  │   ├─ OnDidOpen(uri, content) → LanguageService.OnDocumentOpened()
  │   ├─ OnDidSave(uri, content) → LanguageService.OnDocumentSaved()
  │   ├─ OnDidClose(uri) → LanguageService.OnDocumentClosed()
  │   └─ OnDidChange(uri) → (no action - typing is fast!)
  │
  └─ LSP Feature Handlers (read-only)
      ├─ OnDocumentSymbols(uri) → LanguageService.GetDocumentSymbols(uri)
      ├─ OnDefinition(uri, pos) → LanguageService.GetDefinitionsForPosition(uri, pos)
      └─ OnDiagnostics(uri) → LanguageService.ComputeDiagnostics(uri)

LanguageService (implementation)
  ├─ OnDocumentOpened/Saved → SessionManager.UpdateSession() (private)
  ├─ OnDocumentClosed → (lazy removal - keeps session in cache)
  └─ OnDocumentsChanged → SessionManager.InvalidateSessions() (private)

SessionManager
  ├─ active_sessions_: map<uri, CacheEntry{session, version}>  ← Version-aware cache
  ├─ pending_sessions_: map<uri, PendingCreation>  ← Being created
  ├─ access_order_: vector<uri>  ← LRU tracking (MRU first)
  └─ Cache key: URI + LSP document version (not content hash)
```

**Caching strategy**:

- **Version comparison**: Reuses session if LSP document version unchanged (0ms cache hit)
- **Close/reopen optimization**: Closed files stay in cache (no RemoveSession call)
- **LRU eviction**: Automatically removes oldest entries when limit exceeded (16 files)
- **No content hashing**: LSP provides version tracking - no need to hash content

**Why this design**:

1. **Typing performance**: URI+version stable during typing (0ms), save triggers rebuild (~500ms)
2. **Close/reopen efficiency**: Reopening file reuses cache if version matches (no ~500ms rebuild)
3. **Memory management**: LRU eviction prevents unbounded growth
4. **Simplicity**: No content hashing overhead, rely on LSP version tracking

### Two-Phase Session Creation

**Problem**: Diagnostics need compilation (~126ms) but go-to-def/symbols need indexing (+455ms). Waiting for full session delays diagnostic feedback.

**Solution**: SessionManager uses two-phase channels to signal completion at different stages.

```cpp
struct PendingCreation {
  channel<CompilationState> compilation_ready;  // Phase 1: After elaboration
  channel<OverlaySession> session_ready;        // Phase 2: After indexing
};

// Diagnostics: Wait for compilation only (fast)
auto state = co_await pending->compilation_ready.receive();  // ~126ms

// Symbols/definitions: Wait for full session (slower)
auto session = co_await pending->session_ready.receive();    // ~581ms
```

**Key benefits**:

- **Fast diagnostics**: 126ms vs 581ms (4.6x faster)
- **No duplicate work**: Multiple requests wait on same channels
- **Event-driven coordination**: Async channels avoid polling

## Extension Patterns

### Adding New LSP Features

1. Add method to `LanguageServiceBase` interface (domain operations only, not lifecycle events)
2. Implement in `LanguageService` (may use background dispatch)
3. Add handler to `SlangdLspServer` (coordinate with strand)
4. Register handler in base `LspServer` framework

**Note**: Only add domain operations to the base interface (e.g., ComputeDiagnostics, GetDefinitions). Document lifecycle events (OnDocumentOpened, OnDocumentSaved, etc.) are already defined and should not be extended.

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
