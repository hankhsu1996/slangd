#include "slangd/semantic/document_symbol_builder.hpp"

#include <unordered_map>

#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/AllSyntax.h>
#include <spdlog/spdlog.h>

#include "slangd/semantic/semantic_index.hpp"
#include "slangd/semantic/symbol_utils.hpp"

namespace slangd::semantic {

auto DocumentSymbolBuilder::BuildDocumentSymbolTree(
    const std::string& uri, const SemanticIndex& semantic_index)
    -> std::vector<lsp::DocumentSymbol> {
  const auto& semantic_entries = semantic_index.GetSemanticEntries();
  const auto& source_manager = semantic_index.GetSourceManager();

  // Build parent-to-children map from flat symbols, filtering by URI
  std::unordered_map<
      const slang::ast::Scope*, std::vector<const SemanticEntry*>>
      children_map;
  std::vector<const SemanticEntry*> roots;

  // Group symbols by their parent, filtering by URI and processing only
  // definitions
  for (const auto& entry : semantic_entries) {
    if (!entry.is_definition) {
      continue;  // Skip cross-references, only process definitions
    }

    // Skip EnumValue symbols at root level - they'll be added as children
    if (entry.symbol->kind == slang::ast::SymbolKind::EnumValue) {
      continue;
    }

    // Skip genvar symbols - they're indexed for go-to-definition but not shown
    // in document symbols (like for-loop variables in software languages)
    // Note: Implicit genvar localparams are filtered in semantic_index.cpp
    if (entry.symbol->kind == slang::ast::SymbolKind::Genvar) {
      continue;
    }

    // FILTER: Only include symbols from the requested document using LSP
    // coordinates
    if (entry.def_loc.uri != uri) {
      continue;
    }

    // Treat top-level symbols as roots, others as children
    // Package, Module, Interface are typically top-level even with non-null
    // parent. Classes are roots only when parent is null or when parent is
    // CompilationUnit (top-level classes). Classes inside packages should be
    // children.
    bool is_root = entry.parent == nullptr ||
                   entry.symbol->kind == slang::ast::SymbolKind::Package ||
                   entry.symbol->kind == slang::ast::SymbolKind::Definition ||
                   entry.symbol->kind == slang::ast::SymbolKind::InstanceBody;

    // Special case: Classes are roots only if they're truly top-level (parent
    // is CompilationUnit or null)
    if (!is_root &&
        (entry.symbol->kind == slang::ast::SymbolKind::GenericClassDef ||
         entry.symbol->kind == slang::ast::SymbolKind::ClassType)) {
      if (entry.parent->asSymbol().kind ==
          slang::ast::SymbolKind::CompilationUnit) {
        is_root = true;
      }
    }

    if (is_root) {
      roots.push_back(&entry);
    } else {
      // Skip symbols that are children of functions/tasks for document symbols
      // but keep them in semantic index for go-to-definition
      if (entry.parent->asSymbol().kind != slang::ast::SymbolKind::Subroutine) {
        children_map[entry.parent].push_back(&entry);
      }
    }
  }

  // Recursively build DocumentSymbol tree from roots
  std::vector<lsp::DocumentSymbol> result;
  for (const auto* root_entry : roots) {
    auto doc_symbol_opt =
        DocumentSymbolBuilder::CreateDocumentSymbol(*root_entry);
    if (!doc_symbol_opt.has_value()) {
      continue;  // Skip symbols with empty names
    }

    auto doc_symbol = std::move(*doc_symbol_opt);

    // Determine where to find children:
    // 1. If children_scope is set (for GenericClassDef), use that
    // 2. Otherwise, use the symbol itself if it's a Scope
    const slang::ast::Scope* symbol_as_scope = root_entry->children_scope;
    if (symbol_as_scope == nullptr && root_entry->symbol->isScope()) {
      symbol_as_scope = &root_entry->symbol->as<slang::ast::Scope>();
    }

    // Special handling for Definition symbols (modules/interfaces)
    // The interface/package fix changed modules to be found as Definition
    // symbols but children are stored with Definition as parent, not as Scope
    if (root_entry->symbol->kind == slang::ast::SymbolKind::Definition) {
      // Find children by iterating through children_map and checking parent
      // symbols
      for (const auto& [parent_scope, children_list] : children_map) {
        if (parent_scope != nullptr) {
          // Compare by name since the parent might be an InstanceBody but we
          // want Definition
          if (parent_scope->asSymbol().name == root_entry->symbol->name &&
              !parent_scope->asSymbol().name.empty()) {
            // Found children for this Definition symbol
            for (const auto* child_entry : children_list) {
              auto child_doc_symbol_opt = CreateDocumentSymbol(*child_entry);
              if (!child_doc_symbol_opt.has_value()) {
                continue;  // Skip symbols with empty names
              }
              // Apply TypeAlias handling here too!
              auto child_doc_symbol = std::move(*child_doc_symbol_opt);
              if (child_entry->symbol->kind ==
                  slang::ast::SymbolKind::TypeAlias) {
                HandleEnumTypeAlias(
                    child_doc_symbol, child_entry->symbol, source_manager);
                HandleStructTypeAlias(
                    child_doc_symbol, child_entry->symbol, source_manager);
              }

              // CRITICAL FIX: Recursively attach children to this child symbol
              // too! For example, if this child is a GenerateBlock, it needs
              // its own children. Use children_scope if set (for
              // GenericClassDef), otherwise use symbol if it's a Scope
              const slang::ast::Scope* child_as_scope =
                  child_entry->children_scope;
              if (child_as_scope == nullptr && child_entry->symbol->isScope()) {
                child_as_scope = &child_entry->symbol->as<slang::ast::Scope>();
              }
              AttachChildrenToSymbol(
                  child_doc_symbol, child_as_scope, children_map,
                  source_manager);

              doc_symbol.children->push_back(std::move(child_doc_symbol));
            }
            break;
          }
        }
      }
    } else {
      // Normal scope-based lookup
      DocumentSymbolBuilder::AttachChildrenToSymbol(
          doc_symbol, symbol_as_scope, children_map, source_manager);
    }

    // Special handling for enum and struct type aliases
    if (root_entry->symbol->kind == slang::ast::SymbolKind::TypeAlias) {
      DocumentSymbolBuilder::HandleEnumTypeAlias(
          doc_symbol, root_entry->symbol, source_manager);
      DocumentSymbolBuilder::HandleStructTypeAlias(
          doc_symbol, root_entry->symbol, source_manager);
    }

    result.push_back(std::move(doc_symbol));
  }

  // Filter out empty generate blocks to reduce clutter
  DocumentSymbolBuilder::FilterEmptyGenerateBlocks(result, semantic_index);

  return result;
}

// Implementation of private static member functions

auto DocumentSymbolBuilder::CreateDocumentSymbol(const SemanticEntry& entry)
    -> std::optional<lsp::DocumentSymbol> {
  // VSCode requires DocumentSymbol names to be non-empty
  // Filter out symbols with empty names
  if (entry.name.empty()) {
    return std::nullopt;
  }

  lsp::DocumentSymbol doc_symbol;
  doc_symbol.name = entry.name;
  doc_symbol.kind = entry.lsp_kind;
  // Use stored LSP coordinates directly (no SourceManager conversion needed)
  doc_symbol.range = entry.def_loc.range;
  doc_symbol.selectionRange = doc_symbol.range;  // Use same range for now
  doc_symbol.children =
      std::vector<lsp::DocumentSymbol>();  // Always initialize empty vector
  return doc_symbol;
}

auto DocumentSymbolBuilder::AttachChildrenToSymbol(
    lsp::DocumentSymbol& parent, const slang::ast::Scope* parent_scope,
    const std::unordered_map<
        const slang::ast::Scope*, std::vector<const SemanticEntry*>>&
        children_map,
    const slang::SourceManager& source_manager) -> void {
  if (parent_scope == nullptr) {
    return;  // No scope to check for children
  }

  // Special handling for GenerateBlockArraySymbol: collect unique symbols
  // from template, not from all iterations
  if (parent_scope->asSymbol().kind ==
      slang::ast::SymbolKind::GenerateBlockArray) {
    const auto& gen_array =
        parent_scope->asSymbol().as<slang::ast::GenerateBlockArraySymbol>();

    // For LSP purposes, we only need to show the template symbols once,
    // not all iterations. Use the first entry as the representative.
    if (!gen_array.entries.empty() && (gen_array.entries[0] != nullptr) &&
        gen_array.entries[0]->isScope()) {
      const auto& block_scope = gen_array.entries[0]->as<slang::ast::Scope>();

      // Add all symbols from the template (first entry only)
      for (const auto& member : block_scope.members()) {
        // Skip genvar symbols (they're indexed for go-to-definition but not
        // shown in document symbols)
        // Note: Implicit genvar localparams are filtered in semantic_index.cpp
        if (member.kind == slang::ast::SymbolKind::Genvar) {
          continue;
        }

        if (ShouldIndexForDocumentSymbols(member)) {
          // Create DocumentSymbol directly for the member
          if (!member.name.empty()) {
            lsp::DocumentSymbol member_doc_symbol;
            member_doc_symbol.name = std::string(member.name);
            member_doc_symbol.kind = ConvertToLspKindForDocuments(member);
            member_doc_symbol.range = ComputeLspRange(member, source_manager);
            member_doc_symbol.selectionRange = member_doc_symbol.range;
            member_doc_symbol.children = std::vector<lsp::DocumentSymbol>();
            parent.children->push_back(std::move(member_doc_symbol));
          }
        }
      }
    }
    return;  // Done with special handling
  }

  auto children_it = children_map.find(parent_scope);
  if (children_it == children_map.end()) {
    return;  // No children
  }

  for (const auto* child_entry : children_it->second) {
    auto child_doc_symbol_opt = CreateDocumentSymbol(*child_entry);
    if (!child_doc_symbol_opt.has_value()) {
      continue;  // Skip symbols with empty names
    }

    auto child_doc_symbol = std::move(*child_doc_symbol_opt);

    // For child symbols, check if they are scopes themselves
    // Use children_scope if set (for GenericClassDef), otherwise use symbol if
    // it's a Scope
    const slang::ast::Scope* child_as_scope = child_entry->children_scope;
    if (child_as_scope == nullptr && child_entry->symbol->isScope()) {
      child_as_scope = &child_entry->symbol->as<slang::ast::Scope>();
    }
    AttachChildrenToSymbol(
        child_doc_symbol, child_as_scope, children_map, source_manager);

    // Special handling for enum and struct type aliases in children too
    if (child_entry->symbol->kind == slang::ast::SymbolKind::TypeAlias) {
      HandleEnumTypeAlias(
          child_doc_symbol, child_entry->symbol, source_manager);
      HandleStructTypeAlias(
          child_doc_symbol, child_entry->symbol, source_manager);
    }

    parent.children->push_back(std::move(child_doc_symbol));
  }
}

auto DocumentSymbolBuilder::HandleEnumTypeAlias(
    lsp::DocumentSymbol& enum_doc_symbol,
    const slang::ast::Symbol* type_alias_symbol,
    const slang::SourceManager& source_manager) -> void {
  using SK = slang::ast::SymbolKind;

  // Check if this is a TypeAlias of an enum
  if (type_alias_symbol->kind != SK::TypeAlias) {
    return;
  }

  const auto& type_alias = type_alias_symbol->as<slang::ast::TypeAliasType>();
  const auto& canonical_type = type_alias.getCanonicalType();

  // Check if it's an enum type
  if (canonical_type.kind != SK::EnumType) {
    return;  // Not an enum
  }

  // Get the enum type to access its values directly
  const auto& enum_type = canonical_type.as<slang::ast::EnumType>();

  // Use the enum type's values() method to get all enum values
  // This is more reliable than trying to match by scope
  for (const auto& enum_value : enum_type.values()) {
    // Skip enum values with empty names
    std::string enum_name(enum_value.name);
    if (enum_name.empty()) {
      continue;
    }

    // Create a SymbolInfo-like structure for the enum value
    lsp::DocumentSymbol enum_value_symbol;
    enum_value_symbol.name = enum_name;
    enum_value_symbol.kind = lsp::SymbolKind::kEnumMember;

    // Set range from the enum value location
    if (enum_value.location) {
      enum_value_symbol.range = ComputeLspRange(enum_value, source_manager);
      enum_value_symbol.selectionRange = enum_value_symbol.range;
    } else {
      // Default range if no location
      enum_value_symbol.range = {
          .start = {.line = 0, .character = 0},
          .end = {.line = 0, .character = 0}};
      enum_value_symbol.selectionRange = {
          .start = {.line = 0, .character = 0},
          .end = {.line = 0, .character = 0}};
    }

    enum_value_symbol.children = std::vector<lsp::DocumentSymbol>();
    enum_doc_symbol.children->push_back(std::move(enum_value_symbol));
  }
}

auto DocumentSymbolBuilder::HandleStructTypeAlias(
    lsp::DocumentSymbol& struct_doc_symbol,
    const slang::ast::Symbol* type_alias_symbol,
    const slang::SourceManager& source_manager) -> void {
  using SK = slang::ast::SymbolKind;

  // Check if this is a TypeAlias of a struct
  if (type_alias_symbol->kind != SK::TypeAlias) {
    return;
  }

  const auto& type_alias = type_alias_symbol->as<slang::ast::TypeAliasType>();
  const auto& canonical_type = type_alias.getCanonicalType();

  // Check if it's a struct type (packed or unpacked)
  if (canonical_type.kind != SK::PackedStructType &&
      canonical_type.kind != SK::UnpackedStructType) {
    return;  // Not a struct
  }

  // Both PackedStructType and UnpackedStructType inherit from Scope
  const auto& struct_scope = canonical_type.as<slang::ast::Scope>();

  // Iterate through the struct members to find field symbols
  for (const auto& member : struct_scope.members()) {
    if (member.kind == SK::Field) {
      const auto& field_symbol = member.as<slang::ast::FieldSymbol>();

      // Skip fields with empty names
      std::string field_name(field_symbol.name);
      if (field_name.empty()) {
        continue;
      }

      // Create a DocumentSymbol for the field
      lsp::DocumentSymbol field_doc_symbol;
      field_doc_symbol.name = field_name;
      field_doc_symbol.kind = lsp::SymbolKind::kField;

      // Set range from the field location
      if (field_symbol.location.valid()) {
        field_doc_symbol.range = ComputeLspRange(field_symbol, source_manager);
        field_doc_symbol.selectionRange = field_doc_symbol.range;
      } else {
        // Default range if no location
        field_doc_symbol.range = {
            .start = {.line = 0, .character = 0},
            .end = {.line = 0, .character = 0}};
        field_doc_symbol.selectionRange = {
            .start = {.line = 0, .character = 0},
            .end = {.line = 0, .character = 0}};
      }

      field_doc_symbol.children = std::vector<lsp::DocumentSymbol>();
      struct_doc_symbol.children->push_back(std::move(field_doc_symbol));
    }
  }
}

auto DocumentSymbolBuilder::FilterEmptyGenerateBlocks(
    std::vector<lsp::DocumentSymbol>& symbols,
    const SemanticIndex& semantic_index) -> void {
  // First, recursively filter children
  for (auto& symbol : symbols) {
    if (symbol.children.has_value()) {
      FilterEmptyGenerateBlocks(*symbol.children, semantic_index);
    }
  }

  // Remove generate blocks that are truly empty (no symbols or statements)
  // Check the semantic index to see if the generate block contains any indexed
  // items
  std::erase_if(symbols, [](const lsp::DocumentSymbol& symbol) {
    if (symbol.kind != lsp::SymbolKind::kNamespace) {
      return false;  // Only filter namespace symbols (generate blocks)
    }

    if (!symbol.children.has_value() || !symbol.children->empty()) {
      return false;  // Don't filter if already has document symbol children
    }

    // For now, filter out empty generate blocks (no document symbol children)
    // NOTE: Could enhance this to check semantic index for statement-based
    // symbols
    return true;  // Filter out generate blocks with no document symbol children
  });
}

}  // namespace slangd::semantic
