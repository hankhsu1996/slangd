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

### Synchronous (Fast Operations)
```cpp
auto OnGotoDefinition() -> asio::awaitable<DefinitionResult> {
  return language_service_->GetDefinitionsForPosition(...);  // 0-5ms
}
```
- Go-to-definition, document symbols
- **Risk**: Blocks if overlay creation needed

### Asynchronous (Expensive Operations)  
```cpp
auto OnDidOpenTextDocument() -> asio::awaitable<void> {
  asio::co_spawn(strand_, [this, uri]() -> asio::awaitable<void> {
    auto diagnostics = co_await language_service_->ComputeDiagnostics(...);  // 700ms+
    co_await PublishDiagnostics(diagnostics);
  }, asio::detached);
}
```
- Diagnostics, file watching
- **Pattern**: `co_spawn(strand_, work, asio::detached)`

## Overlay Session Management

**Caching Strategy**: LRU cache of compiled SystemVerilog sessions
- **Cache hit**: 0ms (instant response)  
- **Cache miss**: 700ms+ (full compilation)

**Performance Issue**: Cross-file navigation triggers overlay builds on single thread, causing 10s delays.

## Key Files

- `src/main.cpp`: Single io_context setup
- `src/slangd/core/slangd_lsp_server.cpp`: LSP handlers, async patterns
- `src/slangd/services/language_service.cpp`: Overlay management, blocking calls
- `src/slangd/services/overlay_session.cpp`: Expensive compilation logic