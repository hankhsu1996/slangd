#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <lsp/document_features.hpp>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Expression.h>
#include <slang/ast/Scope.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <spdlog/spdlog.h>

namespace slangd::services {
class PreambleManager;
}

namespace slangd::syntax {
class SyntaxNode;
}

namespace slangd::semantic {

class SemanticIndex;
struct SemanticEntry;

// Visitor for collecting symbol definitions and references
// Traverses AST to populate SemanticIndex with unified semantic entries
class IndexVisitor
    : public slang::ast::ASTVisitor<IndexVisitor, true, true, true> {
 public:
  explicit IndexVisitor(
      SemanticIndex& index, std::string current_file_uri,
      slang::BufferID current_file_buffer,
      const services::PreambleManager* preamble_manager);

  // Expression handlers
  void handle(const slang::ast::NamedValueExpression& expr);
  void handle(const slang::ast::ArbitrarySymbolExpression& expr);
  void handle(const slang::ast::CallExpression& expr);
  void handle(const slang::ast::ConversionExpression& expr);
  void handle(const slang::ast::DataTypeExpression& expr);
  void handle(const slang::ast::MemberAccessExpression& expr);
  void handle(const slang::ast::HierarchicalValueExpression& expr);
  void handle(const slang::ast::StructuredAssignmentPatternExpression& expr);

  // Symbol handlers
  void handle(const slang::ast::FormalArgumentSymbol& formal_arg);
  void handle(const slang::ast::VariableSymbol& symbol);
  void handle(const slang::ast::WildcardImportSymbol& import_symbol);
  void handle(const slang::ast::ExplicitImportSymbol& import_symbol);
  void handle(const slang::ast::ParameterSymbol& param);
  void handle(const slang::ast::SubroutineSymbol& subroutine);
  void handle(const slang::ast::MethodPrototypeSymbol& method_prototype);
  void handle(const slang::ast::DefinitionSymbol& definition);
  void handle(const slang::ast::TypeAliasType& type_alias);
  void handle(const slang::ast::EnumValueSymbol& enum_value);
  void handle(const slang::ast::FieldSymbol& field);
  void handle(const slang::ast::NetSymbol& net);
  void handle(const slang::ast::ClassPropertySymbol& class_property);
  void handle(const slang::ast::GenericClassDefSymbol& class_def);
  void handle(const slang::ast::ClassType& class_type);
  void handle(const slang::ast::InterfacePortSymbol& interface_port);
  void handle(const slang::ast::ModportSymbol& modport);
  void handle(const slang::ast::ModportPortSymbol& modport_port);
  void handle(const slang::ast::InstanceArraySymbol& instance_array);
  void handle(const slang::ast::InstanceSymbol& instance);
  void handle(const slang::ast::GenerateBlockArraySymbol& generate_array);
  void handle(const slang::ast::GenerateBlockSymbol& generate_block);
  void handle(const slang::ast::GenvarSymbol& genvar);
  void handle(const slang::ast::PackageSymbol& package);
  void handle(const slang::ast::StatementBlockSymbol& statement_block);
  void handle(const slang::ast::UninstantiatedDefSymbol& symbol);

  template <typename T>
  void handle(const T& node) {
    this->visitDefault(node);
  }

  [[nodiscard]] auto GetIndexingErrors() const
      -> const std::vector<std::string>& {
    return indexing_errors_;
  }

 private:
  std::reference_wrapper<SemanticIndex> index_;
  std::string current_file_uri_;
  slang::BufferID current_file_buffer_;
  const services::PreambleManager* preamble_manager_;
  std::shared_ptr<spdlog::logger> logger_;

  std::unordered_set<const slang::syntax::SyntaxNode*> visited_type_syntaxes_;
  std::unordered_set<const slang::ast::Expression*>
      visited_generate_conditions_;
  std::vector<std::string> indexing_errors_;

  // Helper methods
  void AddEntry(SemanticEntry entry);
  void AddDefinition(
      const slang::ast::Symbol& symbol, std::string_view name,
      lsp::Location def_loc, const slang::ast::Scope* parent_scope,
      const slang::ast::Scope* children_scope = nullptr);
  void AddReference(
      const slang::ast::Symbol& symbol, std::string_view name,
      lsp::Range ref_range, lsp::Location def_loc,
      const slang::ast::Scope* parent_scope);
  void AddReferenceWithLspDefinition(
      const slang::ast::Symbol& symbol, std::string_view name,
      lsp::Range ref_range, lsp::Location def_loc,
      const slang::ast::Scope* parent_scope);
  void TraverseType(const slang::ast::Type& type);
  void IndexClassSpecialization(
      const slang::ast::ClassType& class_type,
      const slang::syntax::SyntaxNode* call_syntax,
      const slang::ast::Expression& overlay_context);
  void TraverseClassNames(
      const slang::syntax::SyntaxNode* node,
      const slang::ast::ClassType& class_type,
      slang::SourceRange definition_range,
      const slang::ast::Expression& overlay_context);
  void IndexClassParameters(
      const slang::ast::ClassType& class_type,
      const slang::syntax::ParameterValueAssignmentSyntax& params,
      const slang::ast::Expression& overlay_context);
  void IndexClassParameters(
      const slang::ast::ClassType& class_type,
      const slang::syntax::ParameterValueAssignmentSyntax& params,
      const slang::ast::Symbol& overlay_context);
  void IndexInstanceParameters(
      const slang::ast::InstanceSymbol& instance,
      const slang::syntax::ParameterValueAssignmentSyntax& params,
      const slang::ast::Symbol& syntax_owner);
  void IndexInstancePorts(
      const slang::ast::InstanceSymbol& instance,
      const slang::syntax::HierarchicalInstanceSyntax& hierarchical_inst_syntax,
      const slang::ast::Symbol& syntax_owner);
  void IndexPackageInScopedName(
      const slang::syntax::SyntaxNode* syntax,
      const slang::ast::Symbol& syntax_owner,
      const slang::ast::Symbol& target_symbol);
  void IndexPackageInScopedName(
      const slang::syntax::SyntaxNode* syntax,
      const slang::ast::Expression& expr_context,
      const slang::ast::Symbol& target_symbol);

  static auto ResolveTargetSymbol(const slang::ast::NamedValueExpression& expr)
      -> const slang::ast::Symbol*;
  static auto ExtractDefinitionRange(const slang::ast::Symbol& symbol)
      -> std::optional<slang::SourceRange>;
  static auto ComputeReferenceRange(
      const slang::ast::NamedValueExpression& expr,
      const slang::ast::Symbol& symbol) -> std::optional<slang::SourceRange>;
};

}  // namespace slangd::semantic
