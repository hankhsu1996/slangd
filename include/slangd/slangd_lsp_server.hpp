#pragma once

#include "lsp/server.hpp"

namespace slangd {

/**
 * @brief SystemVerilog Language Server implementation
 */
class SlangdLspServer : public lsp::Server {
 public:
  SlangdLspServer();
  ~SlangdLspServer() override;

 protected:
  /**
   * @brief Register SystemVerilog-specific LSP method handlers
   */
  void RegisterHandlers() override;

 private:
  // Simplified handler methods without complex return types
  void OnInitialize();
  void OnTextDocumentDidOpen();
  void OnTextDocumentCompletion();
};

}  // namespace slangd
