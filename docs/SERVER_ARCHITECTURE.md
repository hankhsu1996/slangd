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
- Session cannot be removed while hook runs (guaranteed execution)
- Single-publish: full diagnostics (parse + semantic) to avoid visual flicker
- `forceElaborate()` populates `compilation.diagMap` during indexing (file-scoped)

## Async & Threading Model

**High-level overview:**

- **Main event loop** (io_context): All LSP protocol handling, file tracking, session lifecycle
- **Strand coordination**: Serializes access to shared state (document tracking, session storage)
- **Background thread pool**: CPU-intensive compilation and semantic indexing
- **Coroutines**: `co_await` enables non-blocking operations while maintaining responsiveness

**Request flow:**

```
LSP Request → Check session storage → Background compilation (if needed) → LSP Response
```

For detailed async patterns, executor model, synchronization mechanisms, and when/why/how to use them, see `ASYNC_ARCHITECTURE.md`.

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
- **SessionManager**: Session lifecycle (create/store/invalidate), provides access via WithSession callbacks
- **OverlaySession**: Data class (Compilation + SemanticIndex + SourceManager)

**Architecture**: SessionManager centralizes all session lifecycle (create/store/invalidate). LanguageService features are read-only consumers using callback pattern (no shared_ptr escape).

### Compilation Architecture

**Two-compilation design:**

- **PreambleManager**: Compiles ALL project files once → extracts packages, interfaces, module metadata

  - Built from ProjectLayoutService (config file + file discovery)
  - Shared across ALL OverlaySessions
  - Immutable snapshot with version tracking

- **OverlaySession**: Compiles current file + uses PreambleManager data
  - Current file buffer (in-memory, authoritative)
  - Packages via cross-compilation binding (PackageSymbol\* pointers from preamble)
  - Interfaces from PreambleManager (read from disk via `SyntaxTree::fromFile`)
  - Per-file, cached by SessionManager (1:1 mapping with debounced removal)

**Critical insight:** Packages are NOT loaded as syntax trees - PreambleAwareCompilation injects preamble PackageSymbol\* pointers directly into packageMap for cross-compilation binding. Interfaces are still read from disk for port resolution. See `PREAMBLE.md` for details.

**Memory considerations:** Preamble sharing and session caching have specific memory characteristics. See `MEMORY_ARCHITECTURE.md` for details on memory profile, fragmentation behavior, and expected RSS.

**Preconditions (workspace initialization):**

- `GetDocumentSymbols()` waits for `config_ready` - needs defines for correct `#ifdef` handling (fast: config file)
- `OnDocumentOpened()` waits for `workspace_ready` - needs preamble for session creation (slow: parse all files)
- `OverlaySession::Create()` requires non-null `preamble_manager` (will crash in SemanticIndex otherwise)

**Project Layout Management:**

ProjectLayoutService maintains the list of source files for preamble compilation. File discovery can take 10s+ on NFS with 10k+ files, making full rebuilds expensive.

**Incremental updates** - Layout tracks files using AddFile/RemoveFile on create/delete events:

- Filters applied at add time (PathMatch/PathExclude regex), layout stores post-filter files only
- Design rationale: GetSourceFiles() called frequently (every preamble rebuild), AddFile() called rarely (user creates file)
- Gated by AutoDiscover mode - no incremental updates when explicit file lists used

**Config changes** - Only case requiring full layout rebuild:

- Config changes may affect search paths (which directories to scan)
- HandleConfigChange() reloads config + rebuilds layout + rebuilds preamble

**Invalidation rules:**

| Event            | ProjectLayout | PreambleManager | OverlaySession            | Reason                                                     |
| ---------------- | ------------- | --------------- | ------------------------- | ---------------------------------------------------------- |
| Config change    | Full rebuild  | Rebuild         | Invalidate + rebuild open | Config affects compilation settings (macros, search paths) |
| SV file created  | AddFile()     | Rebuild         | Invalidate all            | New file may contain package/interface/module              |
| SV file deleted  | RemoveFile()  | Rebuild         | Invalidate all            | Removed file may have contained package/interface/module   |
| SV file modified | No change     | Rebuild         | Invalidate all            | File list unchanged, content changed (includes open files) |

**Two-phase feedback on file save:**

When saving an open file, two LSP events occur:
- textDocument/didSave → overlay rebuild with old preamble (fast feedback)
- workspace/didChangeWatchedFiles → preamble rebuild (slow, debounced)
- After preamble done → overlay rebuild with new preamble (accurate feedback)

Separation of concerns: textDocument events handle overlay, workspace events handle preamble.

Why not rebuild overlays before preamble? Overlays use content from DocumentStateManager (editor buffer). For open files, didSave already provides fast feedback. For closed files modified externally (AI writes, git pull), LSP server doesn't have content until VSCode reloads the file and sends didChange event.

**Debouncing:** Preamble rebuilds use 500ms debounce to batch rapid changes (e.g., git operations). Pending flag prevents rebuild storms by limiting to max 2 rebuilds during rapid changes.

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
  ├─ File Watcher Events (all file system changes)
  │   └─ OnDidChangeWatchedFiles(changes[])
  │       ├─ Config changes → HandleConfigChange()
  │       │   └─ Reload config + rebuild layout + rebuild preamble
  │       └─ SV file changes → HandleSourceFileChange(uri, type)
  │           ├─ Created → AddFile() + debounced preamble rebuild (500ms)
  │           ├─ Deleted → RemoveFile() + debounced preamble rebuild (500ms)
  │           └─ Modified → debounced preamble rebuild (500ms, no layout change)
  │               Note: Includes open files - triggers preamble rebuild even for files
  │               being edited, ensuring packages/interfaces/modules stay current
  │
  └─ LSP Feature Handlers (requests from client - respond with data)
      ├─ OnDocumentSymbols(uri) → LanguageService.GetDocumentSymbols(uri)
      └─ OnDefinition(uri, pos) → LanguageService.GetDefinitionsForPosition(uri, pos)

LanguageService (domain layer - state management + feature implementations)
  ├─ OpenDocumentTracker open_tracker_  ← Tracks which documents are open (shared)
  ├─ DocumentStateManager doc_state_  ← Document state (content + version)
  ├─ SessionManager session_manager_  ← Session storage + lifecycle
  ├─ DiagnosticPublisher diagnostic_publisher_  ← Callback from LSP server
  │
  ├─ Document Lifecycle
  │   ├─ OnDocumentOpened → doc_state_.Update() + SessionManager.UpdateSession(diagnostic_hook)
  │   ├─ OnDocumentChanged → doc_state_.Update() (no session rebuild - typing is fast!)
  │   ├─ OnDocumentSaved → doc_state_.Get() + SessionManager.UpdateSession(diagnostic_hook)
  │   │   Separation of concerns: textDocument events handle overlay only
  │   │   File watcher (workspace event) handles preamble rebuild separately
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

SessionManager (session storage)
  ├─ open_tracker_: shared reference  ← Queries for open state
  ├─ session_strand_: asio::strand  ← Thread-safe access to maps
  ├─ sessions_: map<uri, SessionEntry{session, version}>  ← Version-aware storage
  ├─ pending_: map<uri, PendingCreation{hooks, events}>  ← Being created
  ├─ cleanup_timers_: map<uri, timer>  ← Cleanup delay (5s)
  ├─ Storage key: URI + LSP document version (not content hash)
  └─ UpdateSession accepts optional hooks for server-push features

For detailed session management design including debounced removal, memory efficiency, and
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

**Session storage strategy**:

- **Version comparison**: Reuses session if LSP document version unchanged (0ms storage hit)
- **Debounced cleanup**: Closed files stay in storage for 5 seconds (supports prefetch pattern)
- **Prefetch optimization**: VSCode hover opens/closes files within 50-100ms - reuse avoids rebuild
- **No size limits**: Preamble architecture enables ~10MB per session (100 files = 1GB acceptable)
- **No content hashing**: LSP provides version tracking - no need to hash content

**Why this design**:

1. **Protocol layer is stateless**: Pure LSP translation (params → domain calls), zero state management
2. **Single source of truth**: Document state lives only in domain layer (`DocumentStateManager`)
3. **Consistent handlers**: All protocol handlers are 1-line thin delegates (no state access)
4. **No duplication**: Eliminated redundant storage between protocol and domain layers
5. **Typing performance**: `OnDocumentChanged` updates state only (no session rebuild), save triggers rebuild
6. **Close/reopen efficiency**: Reopening file reuses stored session if version matches (no ~500ms rebuild)
7. **Memory efficiency**: Preamble architecture enables 1:1 mapping (~10MB per session)
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
