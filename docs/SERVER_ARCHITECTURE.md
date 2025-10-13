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

**Hook-based extraction**: Diagnostics extracted during session creation using hooks registered with `UpdateSession()`.

```
Background thread:
  Compile → Elaborate → Store → Execute hook → Signal events
  Hook: Extract diagnostics (on strand) → Post to main → Publish
```

**Key properties**:
- Hook executes on strand after caching, before signaling events
- Session cannot be evicted while hook runs (guaranteed execution)
- Single-publish: full diagnostics (parse + semantic) to avoid visual flicker
- `forceElaborate()` populates `compilation.diagMap` during indexing (file-scoped)

## Executor & Threading Model

### Main Event Loop (io_context)

- **Single-threaded**: All LSP protocol handling, file tracking, and cache management
- **Strand coordination**: `asio::strand` serializes access to shared state
- **Non-blocking**: Uses `co_await` for all potentially slow operations

### Background Thread Pool Pattern

```
// Dispatch expensive work to background threads
result = co_await spawn_on_pool(heavy_computation)

// Post result back to main thread for cache/protocol handling
co_await post_to_main_thread()
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
- **SessionManager**: Session lifecycle (create/cache/invalidate), provides access via WithSession callbacks
- **OverlaySession**: Data class (Compilation + SemanticIndex + SourceManager)

**Architecture**: SessionManager centralizes all session lifecycle (create/cache/invalidate). LanguageService features are read-only consumers using callback pattern (no shared_ptr escape).

### Compilation Architecture

**Two-compilation design:**

- **PreambleManager**: Compiles ALL project files once → extracts packages, interfaces, module metadata

  - Built from ProjectLayoutService (config file + file discovery)
  - Shared across ALL OverlaySessions
  - Immutable snapshot with version tracking

- **OverlaySession**: Compiles current file + uses PreambleManager data
  - Current file buffer (in-memory, authoritative)
  - Packages from PreambleManager (read from disk via `SyntaxTree::fromFile`)
  - Interfaces from PreambleManager (read from disk via `SyntaxTree::fromFile`)
  - Per-file, cached by SessionManager (LRU eviction)

**Critical insight:** OverlaySession reads packages/interfaces from disk, not from PreambleManager compilation. PreambleManager only provides metadata (which files to include).

**Invalidation rules:**

| Event                   | PreambleManager | OverlaySession            | Reason                                                     |
| ----------------------- | ------------- | ------------------------- | ---------------------------------------------------------- |
| Config change           | Rebuild       | Invalidate + rebuild open | Config affects compilation settings (macros, search paths) |
| Closed SV file change   | No change     | Invalidate all            | OverlaySession reads fresh content from disk               |
| SV file created/deleted | Rebuild       | Invalidate all            | File discovery changes (new package/interface/module)      |
| Open file change        | No change     | Invalidate one            | Handled by text sync (didChange/didSave)                   |

**Config change behavior:** After rebuilding PreambleManager and invalidating all sessions, proactively rebuilds sessions for all open files to restore LSP features immediately (no on-demand lazy rebuild).

**Event-driven model**:

```
SlangdLspServer (protocol layer - thin delegates)
  ├─ Document Events (notifications from client - open files)
  │   ├─ OnDidOpen(uri, content, version)
  │   │   → LanguageService.OnDocumentOpened()
  │   │       └─ SessionManager.UpdateSession(uri, content, version, diagnostic_hook)
  │   │           └─ Hook extracts diagnostics → Publishes via callback
  │   │
  │   ├─ OnDidChange(uri, content, version)
  │   │   → LanguageService.OnDocumentChanged()
  │   │
  │   ├─ OnDidSave(uri)
  │   │   → LanguageService.OnDocumentSaved()
  │   │       └─ SessionManager.UpdateSession(uri, content, version, diagnostic_hook)
  │   │           └─ Hook extracts diagnostics → Publishes via callback
  │   │
  │   └─ OnDidClose(uri) → LanguageService.OnDocumentClosed()
  │
  ├─ File Watcher Events (external changes - closed files only)
  │   └─ OnDidChangeWatchedFiles(changes[])
  │       ├─ Filter open files (handled by text sync above)
  │       ├─ Config changes → HandleConfigChange() (rebuild PreambleManager)
  │       └─ SV file changes → HandleSourceFileChange() (invalidate sessions)
  │
  └─ LSP Feature Handlers (requests from client - respond with data)
      ├─ OnDocumentSymbols(uri) → LanguageService.GetDocumentSymbols(uri)
      └─ OnDefinition(uri, pos) → LanguageService.GetDefinitionsForPosition(uri, pos)

LanguageService (domain layer - state management + feature implementations)
  ├─ OpenDocumentTracker open_tracker_  ← Tracks which documents are open (shared)
  ├─ DocumentStateManager doc_state_  ← Document state (content + version)
  ├─ SessionManager session_manager_  ← Compilation cache + lifecycle
  ├─ DiagnosticPublisher diagnostic_publisher_  ← Callback from LSP server
  │
  ├─ Document Lifecycle
  │   ├─ OnDocumentOpened → doc_state_.Update() + SessionManager.UpdateSession(diagnostic_hook)
  │   ├─ OnDocumentChanged → doc_state_.Update() (no session rebuild - typing is fast!)
  │   ├─ OnDocumentSaved → doc_state_.Get() + SessionManager.UpdateSession(diagnostic_hook)
  │   ├─ OnDocumentClosed → doc_state_.Remove() (lazy session removal)
  │   └─ OnDocumentsChanged → SessionManager.InvalidateSessions()
  │
  └─ LSP Features (called by protocol layer)
      ├─ GetDocumentSymbols(uri) → Document symbol tree
      └─ GetDefinitionsForPosition(uri, pos) → Go-to-definition locations

OpenDocumentTracker (shared state)
  └─ open_documents_: set<uri>  ← Which documents are currently open

DocumentStateManager (synchronized storage)
  ├─ open_tracker_: shared reference  ← Updates on open/close
  ├─ documents_: map<uri, DocumentState{content, version}>  ← Domain state
  └─ strand_: asio::strand  ← Thread-safe access

SessionManager (compilation cache)
  ├─ open_tracker_: shared reference  ← Queries for eviction decisions
  ├─ session_strand_: asio::strand  ← Thread-safe access to maps
  ├─ active_sessions_: map<uri, CacheEntry{session, version}>  ← Version-aware cache
  ├─ pending_sessions_: map<uri, PendingCreation{hooks, events}>  ← Being created
  ├─ access_order_: vector<uri>  ← LRU tracking (MRU first)
  ├─ Cache key: URI + LSP document version (not content hash)
  └─ UpdateSession accepts optional hooks for server-push features

For detailed session management design including memory bounds, eviction policy, and
VSCode behavioral patterns, see SESSION_MANAGEMENT.md.
```

**Key LSP Patterns:**

**1. Document Events (Client → Server notifications):**

- Client notifies server of document changes (open/change/save/close)
- Server responds immediately, then MAY push diagnostics asynchronously
- Example: `textDocument/didSave` → server computes → `textDocument/publishDiagnostics`

**2. Feature Requests (Client → Server requests, Server → Client responses):**

- Client sends request and waits for response
- Server processes and returns data synchronously
- Example: `textDocument/definition` request → server responds with locations

**3. Push Notifications (Server → Client notifications):**

- Server pushes data to client proactively (no request needed)
- Diagnostics use this pattern - server decides when to send them
- Example: After save, server publishes diagnostics without client asking

**Caching strategy**:

- **Version comparison**: Reuses session if LSP document version unchanged (0ms cache hit)
- **Close/reopen optimization**: Closed files stay in cache (no RemoveSession call)
- **LRU eviction**: Automatically removes oldest entries when limit exceeded (16 files)
- **No content hashing**: LSP provides version tracking - no need to hash content

**Why this design**:

1. **Protocol layer is stateless**: Pure LSP translation (params → domain calls), zero state management
2. **Single source of truth**: Document state lives only in domain layer (`DocumentStateManager`)
3. **Consistent handlers**: All protocol handlers are 1-line thin delegates (no state access)
4. **No duplication**: Eliminated redundant storage between protocol and domain layers
5. **Typing performance**: `OnDocumentChanged` updates state only (no session rebuild), save triggers rebuild
6. **Close/reopen efficiency**: Reopening file reuses cache if version matches (no ~500ms rebuild)
7. **Memory management**: LRU eviction prevents unbounded growth
8. **Simplicity**: No content hashing overhead, rely on LSP version tracking

### Two-Phase Session Creation

**Phases**:
- **Phase 1 (compilation_ready)**: Elaboration complete, diagnostics available
  - Hooks execute (server-push features)
  - Broadcast event signals (client-request features)
- **Phase 2 (session_ready)**: Indexing complete, symbols/definitions available

**Patterns**:
```
// Server-push (diagnostics via hooks)
UpdateSession(uri, content, version, diagnostic_hook)
  → Hook extracts during creation → Publishes automatically

// Client-request (symbols/definitions via callbacks)
WithSession(uri, callback) → waits for session_ready → executes
```

**Benefits**: Fast diagnostics (Phase 1), guaranteed extraction (hooks), shared results (broadcast events)

## Extension Patterns

### Adding New LSP Features

1. Add method to `LanguageServiceBase` interface (domain operations only, not lifecycle events)
2. Implement in `LanguageService` (may use background dispatch)
3. Add handler to `SlangdLspServer` (coordinate with strand)
4. Register handler in base `LspServer` framework

**Note**: Only add domain operations to the base interface (e.g., GetDefinitions, GetDocumentSymbols). Document lifecycle events (OnDocumentOpened, OnDocumentSaved, etc.) are already defined and should not be extended.

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
