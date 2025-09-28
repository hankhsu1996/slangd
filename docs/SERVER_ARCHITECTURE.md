# Server Architecture

## ASIO Concurrency Model

**Single-threaded event loop** with strand-serialized state access.

```
io_context (main thread)
├── LspServer (JSON-RPC handling)
├── SlangdLspServer (LSP protocol) 
└── LanguageService (SystemVerilog logic)
```

## Request Handling Patterns

### Asynchronous (All LSP Operations)
```cpp
auto OnGotoDefinition() -> asio::awaitable<DefinitionResult> {
  co_return co_await language_service_->GetDefinitionsForPosition(...);  // 0-700ms+
}

auto OnDocumentSymbols() -> asio::awaitable<DocumentSymbolResult> {
  co_return co_await language_service_->GetDocumentSymbols(...);  // 0-700ms+
}

auto OnDidOpenTextDocument() -> asio::awaitable<void> {
  asio::co_spawn(strand_, [this, uri]() -> asio::awaitable<void> {
    auto diagnostics = co_await language_service_->ComputeDiagnostics(...);  // 700ms+
    co_await PublishDiagnostics(diagnostics);
  }, asio::detached);
}
```
- **All operations** now use async pattern for consistency
- **Cache hits**: 0ms (instant response)
- **Cache misses**: 700ms+ (overlay compilation on main thread)
- **Next step**: Background thread pool for true concurrency

## Overlay Session Management

**Caching Strategy**: LRU cache of compiled SystemVerilog sessions
- **Cache hit**: 0ms (instant response)  
- **Cache miss**: 700ms+ (full compilation)

**Performance Issue**: Overlay compilation still blocks main thread for 700ms+ even with async interfaces. Next step: background thread pool for true concurrency.

## Key Files

- `src/main.cpp`: Single io_context setup
- `src/slangd/core/slangd_lsp_server.cpp`: LSP handlers, async patterns
- `src/slangd/services/language_service.cpp`: Overlay management, blocking calls
- `src/slangd/services/overlay_session.cpp`: Expensive compilation logic