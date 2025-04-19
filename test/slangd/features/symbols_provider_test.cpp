#include "include/slangd/features/symbols_provider.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "include/lsp/basic.hpp"
#include "include/lsp/document_features.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

// Helper to run async test functions with coroutines
template <typename F>
void RunTest(F&& test_fn) {
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  bool completed = false;
  std::exception_ptr exception;

  asio::co_spawn(
      io_context,
      [fn = std::forward<F>(test_fn), &completed, &exception,
       executor]() -> asio::awaitable<void> {
        try {
          co_await fn(executor);
          completed = true;
        } catch (...) {
          exception = std::current_exception();
          completed = true;
        }
      },
      asio::detached);

  io_context.run();

  if (exception) {
    std::rethrow_exception(exception);
  }

  REQUIRE(completed);
}

// Helper function that combines compilation and symbol extraction
auto ExtractSymbolsFromString(
    asio::any_io_executor executor, std::string source)
    -> asio::awaitable<std::vector<lsp::DocumentSymbol>> {
  const std::string workspace_root = ".";
  const std::string uri = "file://test.sv";
  auto config_manager =
      std::make_shared<slangd::ConfigManager>(executor, workspace_root);
  auto doc_manager =
      std::make_shared<slangd::DocumentManager>(executor, config_manager);
  co_await doc_manager->ParseWithCompilation(uri, source);
  auto workspace_manager = std::make_shared<slangd::WorkspaceManager>(
      executor, workspace_root, config_manager);
  auto symbols_provider =
      slangd::SymbolsProvider(doc_manager, workspace_manager);

  co_return symbols_provider.GetSymbolsForUri(uri);
}

TEST_CASE("GetDocumentSymbols extracts basic module", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
    module test_module;
    endmodule
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, module_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);
  });
}

TEST_CASE("GetDocumentSymbols extracts basic package", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // The simplest possible package
    std::string package_code = R"(
    package test_pkg;
    endpackage
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, package_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);
  });
}

TEST_CASE("GetDocumentSymbols extracts basic interface", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // The simplest possible interface
    std::string interface_code = R"(
    interface test_if;
    endinterface
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, interface_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kInterface);
  });
}

TEST_CASE(
    "GetDocumentSymbols extracts module with parameters and variables",
    "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Module with parameters and variables
    std::string module_params_code = R"(
    module mod_with_param_and_var (
      parameter int WIDTH = 8;
      logic [WIDTH-1:0] data;
    endmodule
  )";

    auto symbols =
        co_await ExtractSymbolsFromString(executor, module_params_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "mod_with_param_and_var");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);

    REQUIRE(symbols[0].children.has_value());
    REQUIRE(symbols[0].children->size() == 2);
    REQUIRE(symbols[0].children->at(0).name == "WIDTH");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kConstant);
    REQUIRE(symbols[0].children->at(1).name == "data");
    REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::kVariable);
  });
}

TEST_CASE("GetDocumentSymbols extracts module ports", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Module with ports
    std::string module_ports_code = R"(
    module mod_with_ports #(
      parameter WIDTH = 8
    )(
      input clk,
      output data
    );
    endmodule
  )";

    auto symbols =
        co_await ExtractSymbolsFromString(executor, module_ports_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "mod_with_ports");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);

    REQUIRE(symbols[0].children->size() == 3);
    REQUIRE(symbols[0].children->at(0).name == "WIDTH");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kConstant);
    REQUIRE(symbols[0].children->at(1).name == "clk");
    REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::kVariable);
    REQUIRE(symbols[0].children->at(2).name == "data");
    REQUIRE(symbols[0].children->at(2).kind == lsp::SymbolKind::kVariable);
  });
}

TEST_CASE("GetDocumentSymbols extracts enum type", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Package with enum
    // Note that in SystemVerilog, enum members are flattened into the parent
    // package scope.
    std::string enum_code = R"(
    package pkg_with_enum;
      typedef enum { RED, GREEN, BLUE } color_t;
    endpackage
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, enum_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "pkg_with_enum");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "color_t");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kEnum);

    auto enum_symbol = symbols[0].children->at(0);
    REQUIRE(enum_symbol.children->size() == 3);
    REQUIRE(enum_symbol.children->at(0).name == "RED");
    REQUIRE(enum_symbol.children->at(0).kind == lsp::SymbolKind::kEnumMember);
    REQUIRE(enum_symbol.children->at(1).name == "GREEN");
    REQUIRE(enum_symbol.children->at(1).kind == lsp::SymbolKind::kEnumMember);
    REQUIRE(enum_symbol.children->at(2).name == "BLUE");
    REQUIRE(enum_symbol.children->at(2).kind == lsp::SymbolKind::kEnumMember);
  });
}

TEST_CASE("GetDocumentSymbols extracts struct type", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Package with struct
    std::string struct_code = R"(
    package pkg_with_struct;
      typedef struct {
        logic [7:0] a;
        logic [7:0] b;
      } my_struct_t;
    endpackage
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, struct_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "pkg_with_struct");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "my_struct_t");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kStruct);

    REQUIRE(symbols[0].children->at(0).children->size() == 2);
    REQUIRE(symbols[0].children->at(0).children->at(0).name == "a");
    REQUIRE(
        symbols[0].children->at(0).children->at(0).kind ==
        lsp::SymbolKind::kField);
    REQUIRE(symbols[0].children->at(0).children->at(1).name == "b");
    REQUIRE(
        symbols[0].children->at(0).children->at(1).kind ==
        lsp::SymbolKind::kField);
  });
}

TEST_CASE("GetDocumentSymbols extracts functions", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Package with function
    std::string function_code = R"(
    package pkg_with_function;
      function int add(int a, int b);
        return a + b;
      endfunction
    endpackage
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, function_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "pkg_with_function");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "add");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kFunction);
  });
}

TEST_CASE(
    "GetDocumentSymbols extracts multiple top-level symbols",
    "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Multiple top-level entities
    std::string multi_code = R"(
    module module1; endmodule
    module module2; endmodule
    package package1; endpackage
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, multi_code);

    REQUIRE(symbols.size() == 3);
    REQUIRE(symbols[0].name == "module1");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);
    REQUIRE(symbols[1].name == "module2");
    REQUIRE(symbols[1].kind == lsp::SymbolKind::kClass);
    REQUIRE(symbols[2].name == "package1");
    REQUIRE(symbols[2].kind == lsp::SymbolKind::kPackage);
  });
}

TEST_CASE("GetDocumentSymbols extracts nested struct", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Package with nested struct
    std::string nested_struct_code = R"(
    package pkg_with_nested_struct;
      typedef struct {
        struct {
          logic [7:0] a;
        } inner;
      } my_struct_t;
    endpackage
  )";

    auto symbols =
        co_await ExtractSymbolsFromString(executor, nested_struct_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "pkg_with_nested_struct");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "my_struct_t");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kStruct);

    REQUIRE(symbols[0].children->at(0).children->size() == 1);
    REQUIRE(symbols[0].children->at(0).children->at(0).name == "inner");
    REQUIRE(
        symbols[0].children->at(0).children->at(0).kind ==
        lsp::SymbolKind::kField);

    REQUIRE(symbols[0].children->at(0).children->at(0).children->size() == 1);
    REQUIRE(
        symbols[0].children->at(0).children->at(0).children->at(0).name == "a");
    REQUIRE(
        symbols[0].children->at(0).children->at(0).children->at(0).kind ==
        lsp::SymbolKind::kField);
  });
}

TEST_CASE("GetDocumentSymbols extracts type parameters", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string type_param_code = R"(
    module mod_with_type_param #(
      parameter type T = logic [7:0]
    )(
      input T data,
      output T data_out
    );
    endmodule
  )";

    auto symbols = co_await ExtractSymbolsFromString(executor, type_param_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "mod_with_type_param");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);

    REQUIRE(symbols[0].children->size() == 3);
    REQUIRE(symbols[0].children->at(0).name == "T");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kTypeParameter);
    REQUIRE(symbols[0].children->at(1).name == "data");
    REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::kVariable);
    REQUIRE(symbols[0].children->at(2).name == "data_out");
    REQUIRE(symbols[0].children->at(2).kind == lsp::SymbolKind::kVariable);
  });
}

TEST_CASE(
    "GetDocumentSymbols extracts module instantiation", "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_inst_code = R"(
    module submodule;
      logic [7:0] a;
    endmodule

    module mod_with_inst;
      submodule submod();
    endmodule
  )";

    auto symbols =
        co_await ExtractSymbolsFromString(executor, module_inst_code);

    REQUIRE(symbols.size() == 2);
    REQUIRE(symbols[0].name == "mod_with_inst");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);
    REQUIRE(symbols[1].name == "submodule");
    REQUIRE(symbols[1].kind == lsp::SymbolKind::kClass);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "submod");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kVariable);

    REQUIRE(symbols[1].children->size() == 1);
    REQUIRE(symbols[1].children->at(0).name == "a");
    REQUIRE(symbols[1].children->at(0).kind == lsp::SymbolKind::kVariable);
  });
}

TEST_CASE(
    "GetDocumentSymbols extracts unknown module instantiation",
    "[symbol_utils]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_inst_code = R"(
    module mod_with_inst;
      submodule submod();
    endmodule
  )";

    auto symbols =
        co_await ExtractSymbolsFromString(executor, module_inst_code);

    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].name == "mod_with_inst");
    REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);

    REQUIRE(symbols[0].children->size() == 1);
    REQUIRE(symbols[0].children->at(0).name == "submod");
    REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::kVariable);
  });
}
