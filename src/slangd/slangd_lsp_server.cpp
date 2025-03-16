#include "slangd/slangd_lsp_server.hpp"

#include <iostream>

namespace slangd {

SlangdLspServer::SlangdLspServer() = default;
SlangdLspServer::~SlangdLspServer() = default;

void SlangdLspServer::RegisterHandlers() {
  // Simplified registration of handlers
  std::cout << "Registering handlers for SystemVerilog LSP" << std::endl;
  // Not actually registering handlers in this simplified version
}

void SlangdLspServer::OnInitialize() {
  std::cout << "Initialize request received" << std::endl;
}

void SlangdLspServer::OnTextDocumentDidOpen() {
  std::cout << "Document opened" << std::endl;
}

void SlangdLspServer::OnTextDocumentCompletion() {
  std::cout << "Completion request received" << std::endl;
}

}  // namespace slangd
