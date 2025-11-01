#include "slangd/semantic/semantic_index.hpp"

#include <set>

#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/HierarchicalReference.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/expressions/AssertionExpr.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/ClassSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/parsing/Token.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "slangd/semantic/index_visitor.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/utils/conversion.hpp"
#include "slangd/utils/path_utils.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::semantic {

auto SemanticIndex::IsInCurrentFile(
    const slang::ast::Symbol& symbol, const std::string& current_file_uri,
    const slang::SourceManager& source_manager,
    const services::PreambleManager* preamble_manager) -> bool {
  if (preamble_manager != nullptr) {
    const auto* symbol_scope = symbol.getParentScope();
    if (symbol_scope != nullptr) {
      const auto& symbol_compilation = symbol_scope->getCompilation();
      const auto& preamble_compilation = preamble_manager->GetCompilation();
      if (&symbol_compilation == &preamble_compilation) {
        return false;  // Symbol from preamble compilation, not current file
      }
    }
  }

  if (!symbol.location.valid()) {
    return false;
  }

  auto uri = std::string(ToLspLocation(symbol.location, source_manager).uri);
  return NormalizeUri(uri) == NormalizeUri(current_file_uri);
}

auto SemanticIndex::IsInCurrentFile(
    slang::SourceLocation loc, const std::string& current_file_uri,
    const slang::SourceManager& source_manager) -> bool {
  if (!loc.valid()) {
    return false;
  }

  auto uri = std::string(ToLspLocation(loc, source_manager).uri);
  return NormalizeUri(uri) == NormalizeUri(current_file_uri);
}

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager,
    const std::string& current_file_uri, slang::BufferID current_file_buffer,
    const services::PreambleManager* preamble_manager,
    std::shared_ptr<spdlog::logger> logger)
    -> std::expected<std::unique_ptr<SemanticIndex>, std::string> {
  utils::ScopedTimer timer(
      fmt::format("Semantic indexing: {}", current_file_uri), logger);

  auto index = std::unique_ptr<SemanticIndex>(
      new SemanticIndex(source_manager, current_file_uri, logger));

  // Create visitor for comprehensive symbol collection and reference tracking
  auto visitor = IndexVisitor(
      *index, current_file_uri, current_file_buffer, preamble_manager);

  // THREE-PATH TRAVERSAL APPROACH
  // Slang's API provides disjoint symbol collections:
  //
  // PATH 1: Definitions (module/interface/program declarations)
  // - getDefinitions() returns DefinitionSymbols (NOT Scopes, no body access)
  // - Visit definition header to index declaration syntax
  // - Body members are indexed by creating full instance via createDefault()
  //
  // PATH 2: Packages
  // - getPackages() returns PackageSymbols (ARE Scopes)
  // - Packages are in their own namespace, disjoint from definitions
  // - Visiting packages automatically traverses their members
  //
  // PATH 3: Compilation Unit Members (classes, enums, typedefs, global vars)
  // - CompilationUnits contain symbols not in definitions or packages
  // - Includes: classes, enums, typedefs, global variables, etc.
  // - Note: CompilationUnits can aggregate symbols from multiple files

  // PATH 1: Index definitions (headers + bodies)
  // PURE DEFINITION-BASED MODEL: Same for modules AND interfaces.
  // 1. See definition → Create instance via createDefault() → Traverse body
  // 2. During traversal, nested instances (interface ports, sub-modules) create
  //    instance symbols but DON'T traverse their bodies (already done in their
  //    definition files)
  //
  // No ordering needed - instances are independent of traversal order
  for (const auto* def : compilation.getDefinitions()) {
    if (def->kind != slang::ast::SymbolKind::Definition) {
      continue;
    }

    const auto& definition = def->as<slang::ast::DefinitionSymbol>();
    if (!IsInCurrentFile(
            definition, current_file_uri, source_manager, preamble_manager)) {
      continue;
    }

    // Visit definition header (creates self-definition for the definition name)
    definition.visit(visitor);

    // Check if all parameters have defaults (required for fromDefinition)
    bool has_all_defaults = true;
    for (const auto& param : definition.parameters) {
      if (!param.hasDefault()) {
        has_all_defaults = false;
        break;
      }
    }

    if (has_all_defaults) {
      // Create a full InstanceSymbol (not just InstanceBodySymbol) to enable
      // proper interface port connection resolution via getConnection().
      // Using createDefault() ensures parentInstance is set correctly.
      auto& instance = slang::ast::InstanceSymbol::createDefault(
          compilation, definition, nullptr, nullptr, nullptr,
          definition.location);

      // Set parent scope for the instance - required for proper symbol
      // resolution
      if (const auto* parent_scope = definition.getParentScope()) {
        instance.setParent(*parent_scope);
      }

      // Force elaboration to populate diagMap with semantic diagnostics
      // and cache symbol resolutions (visitInstances=false for file-scoped)
      {
        utils::ScopedTimer elab_timer(
            fmt::format("Elaboration for {}", definition.name), logger);
        compilation.forceElaborate(instance.body);
      }

      // Traverse the instance body to index all members
      // (uses cached symbol resolutions from forceElaborate)
      {
        utils::ScopedTimer traversal_timer(
            fmt::format("Traversal for {}", definition.name), logger);
        instance.body.visit(visitor);
      }
    }
  }

  // PATH 2: Index packages
  for (const auto* pkg : compilation.getPackages()) {
    if (IsInCurrentFile(
            *pkg, current_file_uri, source_manager, preamble_manager)) {
      pkg->visit(visitor);  // Packages are Scopes, members auto-traversed
    }
  }

  // PATH 3: Index compilation unit members (classes, enums, typedefs, etc.)
  // CompilationUnits can contain symbols from MULTIPLE files, so we must
  // filter children by file URI
  for (const auto* unit : compilation.getCompilationUnits()) {
    for (const auto& child : unit->members()) {
      if (IsInCurrentFile(
              child, current_file_uri, source_manager, preamble_manager)) {
        // Skip packages - already handled in PATH 2
        if (child.kind == slang::ast::SymbolKind::Package) {
          continue;
        }

        // Handle classes specially: visit top-level classes (parent is
        // CompilationUnit), skip package-nested classes (visited via package's
        // visitDefault)
        if (child.kind == slang::ast::SymbolKind::GenericClassDef ||
            child.kind == slang::ast::SymbolKind::ClassType) {
          const auto* parent = child.getParentScope();
          if (parent != nullptr &&
              parent->asSymbol().kind !=
                  slang::ast::SymbolKind::CompilationUnit) {
            // Skip class nested in package - already visited via parent
            continue;
          }
          // Top-level class in compilation unit - visit it
          child.visit(visitor);
          continue;
        }

        // Skip members of packages or classes - they're already visited
        // via their parent's visitDefault()
        const auto* parent = child.getParentScope();
        if (parent != nullptr) {
          const auto& parent_symbol = parent->asSymbol();
          if (parent_symbol.kind == slang::ast::SymbolKind::Package ||
              parent_symbol.kind == slang::ast::SymbolKind::ClassType ||
              parent_symbol.kind == slang::ast::SymbolKind::GenericClassDef) {
            continue;
          }
        }
        child.visit(visitor);
      }
    }
  }

  // Sort entries by source location for O(n) validation and lookup
  // optimizations O(n log n) - trivially fast even for 100k entries
  // Sort by position only - all entries are in same file (invariant)
  std::sort(
      index->semantic_entries_.begin(), index->semantic_entries_.end(),
      [](const SemanticEntry& a, const SemanticEntry& b) -> bool {
        return a.ref_range.start < b.ref_range.start;
      });

  // Check for indexing errors (e.g., BufferID mismatches)
  const auto& indexing_errors = visitor.GetIndexingErrors();
  if (!indexing_errors.empty()) {
    // Report first error + count (multiple errors usually same root cause)
    return std::unexpected(
        fmt::format(
            "Failed to index '{}': {} ({} total error{})", current_file_uri,
            indexing_errors[0], indexing_errors.size(),
            indexing_errors.size() > 1 ? "s" : ""));
  }

  // Fatal validation - invalid coordinates crash go-to-definition
  auto coord_result = index->ValidateCoordinates();
  if (!coord_result) {
    return std::unexpected(coord_result.error());
  }

  // Non-fatal validations - log and continue
  auto overlap_result = index->ValidateNoRangeOverlaps();
  if (!overlap_result) {
    logger->warn("{}", overlap_result.error());
  }

  auto coverage_result =
      index->ValidateSymbolCoverage(compilation, current_file_uri);
  if (!coverage_result) {
    logger->trace("{}", coverage_result.error());
  }

  return index;
}

// Go-to-definition using LSP coordinates
auto SemanticIndex::LookupDefinitionAt(
    const std::string& uri, lsp::Position position) const
    -> std::optional<lsp::Location> {
  // Validate URI first - all entries must be from current_file_uri_
  if (uri != current_file_uri_) {
    return std::nullopt;  // Wrong file!
  }

  // Binary search in sorted entries by position
  // Entries are sorted by source_range.start within the single file
  const auto projection = [](const SemanticEntry& e) {
    return e.ref_range.start;
  };

  auto it = std::ranges::upper_bound(
      semantic_entries_, position, std::ranges::less{}, projection);

  // Move back one entry - this is the candidate that might contain our position
  // (since its start is <= target, but the next entry's start is > target)
  if (it != semantic_entries_.begin()) {
    --it;

    // Verify the entry contains the target position
    if (it->ref_range.Contains(position)) {
      // Return the definition location using standard LSP type
      return lsp::Location{.uri = it->def_loc.uri, .range = it->def_loc.range};
    }
  }

  return std::nullopt;
}

auto SemanticIndex::ValidateNoRangeOverlaps(bool strict) const
    -> std::expected<void, std::string> {
  utils::ScopedTimer timer("ValidateNoRangeOverlaps", logger_);

  if (semantic_entries_.empty()) {
    return {};
  }

  std::vector<std::string> overlaps;
  auto filename =
      current_file_uri_.substr(current_file_uri_.find_last_of('/') + 1);

  for (size_t i = 1; i < semantic_entries_.size(); ++i) {
    const auto& prev = semantic_entries_[i - 1];
    const auto& curr = semantic_entries_[i];

    bool overlap =
        (prev.ref_range.start < curr.ref_range.end &&
         curr.ref_range.start < prev.ref_range.end);

    if (overlap) {
      auto msg = fmt::format(
          "Range overlap for symbol '{}' at line {} (char {}-{}) in '{}'",
          curr.name, curr.ref_range.start.line + 1,
          curr.ref_range.start.character, curr.ref_range.end.character,
          filename);

      if (strict) {
        return std::unexpected(msg);
      }
      overlaps.push_back(msg);
    }
  }

  if (!overlaps.empty()) {
    std::string aggregated =
        fmt::format("Found {} range overlaps:", overlaps.size());
    for (const auto& overlap : overlaps) {
      aggregated += "\n  " + overlap;
    }
    return std::unexpected(aggregated);
  }

  return {};
}

auto SemanticIndex::ValidateCoordinates() const
    -> std::expected<void, std::string> {
  utils::ScopedTimer timer("ValidateCoordinates", logger_);

  // Always fatal - invalid coordinates (line == -1) crash go-to-definition
  size_t invalid_count = 0;
  std::string first_invalid_symbol;
  for (const auto& entry : semantic_entries_) {
    if (entry.ref_range.start.line == -1 || entry.ref_range.end.line == -1 ||
        entry.def_loc.range.start.line == -1 ||
        entry.def_loc.range.end.line == -1) {
      if (invalid_count == 0) {
        first_invalid_symbol = std::string(entry.name);
      }
      invalid_count++;
    }
  }

  if (invalid_count > 0) {
    return std::unexpected(
        fmt::format(
            "Found {} entries with invalid coordinates in '{}'. "
            "First invalid symbol: '{}'. "
            "This will cause crashes. Please report this bug.",
            invalid_count, current_file_uri_, first_invalid_symbol));
  }

  return {};
}

auto SemanticIndex::ValidateSymbolCoverage(
    slang::ast::Compilation& compilation, const std::string& current_file_uri,
    bool strict) const -> std::expected<void, std::string> {
  utils::ScopedTimer timer("ValidateSymbolCoverage", logger_);

  struct IdentifierCollector {
    std::vector<slang::parsing::Token> identifiers;

    void visit(const slang::syntax::SyntaxNode& node) {
      // Skip hierarchical paths - not currently supported
      if (node.kind == slang::syntax::SyntaxKind::ScopedName) {
        return;
      }

      for (uint32_t i = 0; i < node.getChildCount(); i++) {
        const auto* child = node.childNode(i);
        if (child != nullptr) {
          visit(*child);
        } else {
          auto token = node.childToken(i);
          if (token.kind == slang::parsing::TokenKind::Identifier) {
            identifiers.push_back(token);
          }
        }
      }
    }
  };

  IdentifierCollector collector;
  for (const auto& tree : compilation.getSyntaxTrees()) {
    auto tree_location = tree->root().sourceRange().start();
    if (!IsInCurrentFile(
            tree_location, current_file_uri, source_manager_.get())) {
      continue;
    }

    collector.visit(tree->root());
    break;
  }

  // Build position set from semantic_entries_ for O(log n) lookup
  std::set<std::pair<int, int>> indexed_positions;
  for (const auto& entry : semantic_entries_) {
    indexed_positions.emplace(
        entry.ref_range.start.line, entry.ref_range.start.character);
  }

  std::vector<slang::parsing::Token> missing;
  for (const auto& token : collector.identifiers) {
    // Skip built-in enum methods
    std::string_view token_text = token.valueText();
    if (token_text == "name" || token_text == "num" || token_text == "next" ||
        token_text == "prev" || token_text == "first" || token_text == "last") {
      continue;
    }

    auto lsp_pos = ToLspPosition(token.location(), source_manager_.get());
    if (!indexed_positions.contains({lsp_pos.line, lsp_pos.character})) {
      missing.push_back(token);
    }
  }

  if (!missing.empty()) {
    auto file_name = source_manager_.get().getFileName(
        source_manager_.get().getFullyExpandedLoc(missing[0].location()));

    std::map<uint32_t, std::vector<std::string_view>> missing_by_line;
    for (const auto& token : missing) {
      auto location =
          source_manager_.get().getFullyExpandedLoc(token.location());
      auto line_number = source_manager_.get().getLineNumber(location);
      missing_by_line[line_number].push_back(token.valueText());
    }

    std::string message = fmt::format(
        "File {} has {} identifiers without definitions on {} lines", file_name,
        missing.size(), missing_by_line.size());

    if (strict) {
      for (const auto& [line, symbols] : missing_by_line) {
        std::string symbols_str;
        for (size_t i = 0; i < symbols.size(); ++i) {
          if (i > 0) {
            symbols_str += ", ";
          }
          symbols_str += symbols[i];
        }
        message += fmt::format("\n  Line {}: {}", line, symbols_str);
      }
    }
    return std::unexpected(message);
  }

  return {};
}

}  // namespace slangd::semantic
