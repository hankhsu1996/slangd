#include "slangd/semantic/index_visitor.hpp"

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/AllSyntax.h>

#include "slangd/semantic/semantic_index.hpp"
#include "slangd/semantic/symbol_utils.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

using slang::ast::ArbitrarySymbolExpression;
using slang::ast::CallExpression;
using slang::ast::ClassPropertySymbol;
using slang::ast::ClassType;
using slang::ast::ConversionExpression;
using slang::ast::DataTypeExpression;
using slang::ast::DefinitionSymbol;
using slang::ast::EnumValueSymbol;
using slang::ast::ExplicitImportSymbol;
using slang::ast::FieldSymbol;
using slang::ast::FormalArgumentSymbol;
using slang::ast::GenerateBlockArraySymbol;
using slang::ast::GenerateBlockSymbol;
using slang::ast::GenericClassDefSymbol;
using slang::ast::GenvarSymbol;
using slang::ast::HierarchicalValueExpression;
using slang::ast::InstanceArraySymbol;
using slang::ast::InstanceSymbol;
using slang::ast::InterfacePortSymbol;
using slang::ast::MemberAccessExpression;
using slang::ast::MethodPrototypeSymbol;
using slang::ast::ModportPortSymbol;
using slang::ast::ModportSymbol;
using slang::ast::NamedValueExpression;
using slang::ast::NetSymbol;
using slang::ast::PackageSymbol;
using slang::ast::ParameterSymbol;
using slang::ast::StatementBlockSymbol;
using slang::ast::StructuredAssignmentPatternExpression;
using slang::ast::SubroutineSymbol;
using slang::ast::TypeAliasType;
using slang::ast::UninstantiatedDefSymbol;
using slang::ast::VariableSymbol;
using slang::ast::WildcardImportSymbol;

IndexVisitor::IndexVisitor(
    SemanticIndex& index, std::string current_file_uri,
    slang::BufferID current_file_buffer,
    const services::PreambleManager* preamble_manager)
    : index_(index),
      current_file_uri_(std::move(current_file_uri)),
      current_file_buffer_(current_file_buffer),
      preamble_manager_(preamble_manager),
      logger_(index.logger_) {
}

void IndexVisitor::AddEntry(SemanticEntry entry) {
  // INVARIANT: All entries have source locations in current_file_uri_
  // This is guaranteed by:
  // 1. IsInCurrentFile() checks at module/package level before traversal
  // 2. All Add* methods populate source_range from symbols in current file
  //
  // No additional filtering needed - preamble symbols have source in current
  // file (where the reference appears) even though definition is elsewhere

  index_.get().semantic_entries_.push_back(std::move(entry));
}

void IndexVisitor::AddDefinition(
    const slang::ast::Symbol& symbol, std::string_view name,
    lsp::Location def_loc, const slang::ast::Scope* parent_scope,
    const slang::ast::Scope* children_scope) {
  const auto& unwrapped = UnwrapSymbol(symbol);

  auto entry = SemanticEntry{
      .ref_range = def_loc.range,
      .def_loc = def_loc,
      .symbol = &unwrapped,
      .lsp_kind = ConvertToLspKind(unwrapped),
      .name = std::string(name),
      .parent = parent_scope,
      .children_scope = children_scope,
      .is_definition = true};

  AddEntry(std::move(entry));
}

void IndexVisitor::AddReference(
    const slang::ast::Symbol& symbol, std::string_view name,
    lsp::Range ref_range, lsp::Location def_loc,
    const slang::ast::Scope* parent_scope) {
  const auto& unwrapped = UnwrapSymbol(symbol);

  auto entry = SemanticEntry{
      .ref_range = ref_range,
      .def_loc = def_loc,
      .symbol = &unwrapped,
      .lsp_kind = ConvertToLspKind(unwrapped),
      .name = std::string(name),
      .parent = parent_scope,
      .children_scope = nullptr,
      .is_definition = false};

  AddEntry(std::move(entry));
}

void IndexVisitor::AddReferenceWithLspDefinition(
    const slang::ast::Symbol& symbol, std::string_view name,
    lsp::Range ref_range, lsp::Location def_loc,
    const slang::ast::Scope* parent_scope) {
  // For module/port/parameter references where PreambleManager provides
  // pre-converted LSP definition coordinates
  const auto& unwrapped = UnwrapSymbol(symbol);

  auto entry = SemanticEntry{
      .ref_range = ref_range,
      .def_loc = def_loc,
      .symbol = &unwrapped,
      .lsp_kind = ConvertToLspKind(unwrapped),
      .name = std::string(name),
      .parent = parent_scope,
      .children_scope = nullptr,
      .is_definition = false};

  AddEntry(std::move(entry));
}

void IndexVisitor::TraverseType(const slang::ast::Type& type) {
  // Skip if already traversed - multiple symbols can share the same type syntax
  if (const auto* type_syntax = type.getSyntax()) {
    if (!visited_type_syntaxes_.insert(type_syntax).second) {
      return;
    }
  }

  switch (type.kind) {
    case slang::ast::SymbolKind::PackedArrayType: {
      const auto& packed_array = type.as<slang::ast::PackedArrayType>();
      packed_array.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(packed_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::FixedSizeUnpackedArrayType: {
      const auto& unpacked_array =
          type.as<slang::ast::FixedSizeUnpackedArrayType>();
      unpacked_array.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(unpacked_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::DynamicArrayType: {
      const auto& dynamic_array = type.as<slang::ast::DynamicArrayType>();
      TraverseType(dynamic_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::QueueType: {
      const auto& queue_type = type.as<slang::ast::QueueType>();
      queue_type.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(queue_type.elementType);
      break;
    }
    case slang::ast::SymbolKind::AssociativeArrayType: {
      const auto& assoc_array = type.as<slang::ast::AssociativeArrayType>();
      TraverseType(assoc_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::TypeAlias: {
      const auto& type_alias = type.as<slang::ast::TypeAliasType>();
      TraverseType(type_alias.targetType.getType());
      break;
    }
    case slang::ast::SymbolKind::TypeReference: {
      const auto& type_ref = type.as<slang::ast::TypeReferenceSymbol>();
      const auto& resolved_type = type_ref.getResolvedType();

      if (const auto* typedef_target =
              resolved_type.as_if<slang::ast::TypeAliasType>()) {
        auto definition_loc = CreateSymbolLocation(*typedef_target, logger_);
        if (definition_loc) {
          // Index package name if this is a scoped type reference
          if (type_ref.getSyntax() != nullptr) {
            IndexPackageInScopedName(
                type_ref.getSyntax(), type_ref, *typedef_target);
          }
          // For scoped names, extract just the typedef name part
          auto usage_range = type_ref.getUsageLocation();
          if (type_ref.getSyntax() != nullptr &&
              type_ref.getSyntax()->kind ==
                  slang::syntax::SyntaxKind::ScopedName) {
            const auto& scoped =
                type_ref.getSyntax()->as<slang::syntax::ScopedNameSyntax>();
            usage_range = scoped.right->sourceRange();
          }

          auto ref_loc = CreateLspLocation(type_ref, usage_range, logger_);
          if (ref_loc) {
            AddReference(
                *typedef_target, typedef_target->name, ref_loc->range,
                *definition_loc, typedef_target->getParentScope());
          }
        }
      } else if (
          const auto* class_target =
              resolved_type.as_if<slang::ast::ClassType>()) {
        // For specialized classes (e.g., MyClass#(int)), use the generic class
        // definition. Specialized classes are created during overlay
        // compilation but their source location belongs to preamble buffers.
        const slang::ast::Symbol* def_symbol = class_target;
        if (class_target->genericClass != nullptr) {
          def_symbol = class_target->genericClass;
        }

        auto definition_loc = CreateSymbolLocation(*def_symbol, logger_);
        if (definition_loc) {
          // Index package name if this is a scoped type reference
          if (type_ref.getSyntax() != nullptr) {
            IndexPackageInScopedName(
                type_ref.getSyntax(), type_ref, *class_target);
          }
          // Extract the class name identifier range (not the entire
          // specialization)
          auto usage_range = type_ref.getUsageLocation();
          if (type_ref.getSyntax() != nullptr) {
            // For ClassName (e.g., Cache#(...)), extract just the identifier
            if (type_ref.getSyntax()->kind ==
                slang::syntax::SyntaxKind::ClassName) {
              const auto& class_name =
                  type_ref.getSyntax()->as<slang::syntax::ClassNameSyntax>();
              usage_range = class_name.identifier.range();

              // Index parameter values (e.g., CACHE_LINE_SIZE in
              // .WIDTH(CACHE_LINE_SIZE))
              if (class_name.parameters != nullptr) {
                IndexClassParameters(
                    *class_target, *class_name.parameters, type_ref);
              }
            }
            // For scoped names (e.g., pkg::Cache), extract just the right part
            else if (
                type_ref.getSyntax()->kind ==
                slang::syntax::SyntaxKind::ScopedName) {
              const auto& scoped =
                  type_ref.getSyntax()->as<slang::syntax::ScopedNameSyntax>();
              usage_range = scoped.right->sourceRange();
            }
          }

          auto ref_loc = CreateLspLocation(type_ref, usage_range, logger_);
          if (ref_loc) {
            AddReference(
                *class_target, class_target->name, ref_loc->range,
                *definition_loc, class_target->getParentScope());
          }
        }
      }
      break;
    }
    case slang::ast::SymbolKind::EnumType: {
      const auto& enum_type = type.as<slang::ast::EnumType>();
      // Traverse base type to index type references (e.g., typedef enum
      // base_type_t)
      TraverseType(enum_type.baseType);
      for (const auto& enum_value : enum_type.values()) {
        this->visit(enum_value);
      }
      break;
    }
    case slang::ast::SymbolKind::PackedStructType: {
      const auto& struct_type = type.as<slang::ast::PackedStructType>();
      for (const auto& field :
           struct_type.membersOfType<slang::ast::FieldSymbol>()) {
        this->visit(field);
      }
      break;
    }
    case slang::ast::SymbolKind::UnpackedStructType: {
      const auto& struct_type = type.as<slang::ast::UnpackedStructType>();
      for (const auto& field : struct_type.fields) {
        this->visit(*field);
      }
      break;
    }
    case slang::ast::SymbolKind::PackedUnionType: {
      const auto& union_type = type.as<slang::ast::PackedUnionType>();
      for (const auto& field :
           union_type.membersOfType<slang::ast::FieldSymbol>()) {
        this->visit(field);
      }
      break;
    }
    case slang::ast::SymbolKind::UnpackedUnionType: {
      const auto& union_type = type.as<slang::ast::UnpackedUnionType>();
      for (const auto& field : union_type.fields) {
        this->visit(*field);
      }
      break;
    }
    case slang::ast::SymbolKind::ClassType: {
      // ClassType references are handled via TypeReference wrapping
      // For now, we just skip traversal to avoid duplicate indexing
      break;
    }
    default:
      break;
  }
}

void IndexVisitor::IndexClassSpecialization(
    const slang::ast::ClassType& class_type,
    const slang::syntax::SyntaxNode* call_syntax,
    const slang::ast::Expression& overlay_context) {
  if (class_type.genericClass == nullptr ||
      !class_type.genericClass->location.valid()) {
    return;
  }

  // Extract definition range from class declaration
  const auto* class_def_syntax = class_type.genericClass->getSyntax();
  if (class_def_syntax == nullptr ||
      class_def_syntax->kind != slang::syntax::SyntaxKind::ClassDeclaration) {
    return;
  }

  auto definition_range =
      class_def_syntax->as<slang::syntax::ClassDeclarationSyntax>()
          .name.range();

  // Find ClassNameSyntax in the call syntax tree
  if (call_syntax != nullptr &&
      call_syntax->kind == slang::syntax::SyntaxKind::InvocationExpression) {
    const auto& invocation =
        call_syntax->as<slang::syntax::InvocationExpressionSyntax>();
    TraverseClassNames(
        invocation.left, class_type, definition_range, overlay_context);
  }
}

void IndexVisitor::TraverseClassNames(
    const slang::syntax::SyntaxNode* node,
    const slang::ast::ClassType& class_type,
    slang::SourceRange definition_range,
    const slang::ast::Expression& overlay_context) {
  if (node == nullptr) {
    return;
  }

  if (node->kind == slang::syntax::SyntaxKind::ClassName) {
    const auto& class_name = node->as<slang::syntax::ClassNameSyntax>();

    // Use genericClass to convert preamble syntax ranges safely
    // CRITICAL: definition_range is from preamble genericClass syntax,
    // so we must use genericClass compilation to decode it correctly
    auto def_loc =
        CreateLspLocation(*class_type.genericClass, definition_range, logger_);
    auto ref_loc = CreateLspLocation(
        overlay_context, class_name.identifier.range(), logger_);

    if (def_loc && ref_loc) {
      AddReference(
          *class_type.genericClass, class_type.genericClass->name,
          ref_loc->range, *def_loc, class_type.genericClass->getParentScope());
    }

    // Index parameter names in specialization
    if (class_name.parameters != nullptr) {
      IndexClassParameters(class_type, *class_name.parameters, overlay_context);
    }
  } else if (node->kind == slang::syntax::SyntaxKind::ScopedName) {
    const auto& scoped = node->as<slang::syntax::ScopedNameSyntax>();
    TraverseClassNames(
        scoped.left, class_type, definition_range, overlay_context);
    TraverseClassNames(
        scoped.right, class_type, definition_range, overlay_context);
  }
}

void IndexVisitor::IndexClassParameters(
    const slang::ast::ClassType& class_type,
    const slang::syntax::ParameterValueAssignmentSyntax& params,
    const slang::ast::Expression& overlay_context) {
  // Visit parameter value expressions to index symbol references
  for (const auto* expr : class_type.parameterAssignmentExpressions) {
    if (expr != nullptr) {
      expr->visit(*this);
    }
  }

  // Index parameter names (e.g., .WIDTH in Cache#(.WIDTH(...)))
  for (const auto* param_base : params.parameters) {
    if (param_base->kind != slang::syntax::SyntaxKind::NamedParamAssignment) {
      continue;
    }

    const auto& named_param =
        param_base->as<slang::syntax::NamedParamAssignmentSyntax>();
    std::string_view param_name = named_param.name.valueText();

    if (class_type.genericClass == nullptr) {
      continue;
    }

    const auto* preamble_scope = class_type.genericClass->getParentScope();
    if (preamble_scope == nullptr) {
      continue;
    }
    const auto& preamble_compilation = preamble_scope->getCompilation();
    const auto* preamble_sm = preamble_compilation.getSourceManager();
    if (preamble_sm == nullptr) {
      continue;
    }

    for (const auto* generic_param : class_type.genericParameters) {
      if (generic_param->name != param_name ||
          generic_param->kind != slang::ast::SymbolKind::Parameter) {
        continue;
      }

      const auto& param_symbol =
          generic_param->as<slang::ast::ParameterSymbol>();
      if (!param_symbol.location.valid()) {
        continue;
      }

      auto param_def_loc =
          CreateSymbolLocationWithSM(param_symbol, *preamble_sm);
      auto ref_loc =
          CreateLspLocation(overlay_context, named_param.name.range(), logger_);

      if (param_def_loc && ref_loc) {
        AddReference(
            param_symbol, param_symbol.name, ref_loc->range, *param_def_loc,
            param_symbol.getParentScope());
      }
      break;
    }
  }
}

void IndexVisitor::IndexClassParameters(
    const slang::ast::ClassType& class_type,
    const slang::syntax::ParameterValueAssignmentSyntax& params,
    const slang::ast::Symbol& overlay_context) {
  // Visit parameter value expressions to index symbol references
  for (const auto* expr : class_type.parameterAssignmentExpressions) {
    if (expr != nullptr) {
      expr->visit(*this);
    }
  }

  // Index parameter names (e.g., .WIDTH in typedef Cache#(.WIDTH(...)))
  for (const auto* param_base : params.parameters) {
    if (param_base->kind != slang::syntax::SyntaxKind::NamedParamAssignment) {
      continue;
    }

    const auto& named_param =
        param_base->as<slang::syntax::NamedParamAssignmentSyntax>();
    std::string_view param_name = named_param.name.valueText();

    if (class_type.genericClass == nullptr) {
      continue;
    }

    const auto* preamble_scope = class_type.genericClass->getParentScope();
    if (preamble_scope == nullptr) {
      continue;
    }
    const auto& preamble_compilation = preamble_scope->getCompilation();
    const auto* preamble_sm = preamble_compilation.getSourceManager();
    if (preamble_sm == nullptr) {
      continue;
    }

    for (const auto* generic_param : class_type.genericParameters) {
      if (generic_param->name != param_name ||
          generic_param->kind != slang::ast::SymbolKind::Parameter) {
        continue;
      }

      const auto& param_symbol =
          generic_param->as<slang::ast::ParameterSymbol>();
      if (!param_symbol.location.valid()) {
        continue;
      }

      auto param_def_loc =
          CreateSymbolLocationWithSM(param_symbol, *preamble_sm);
      auto ref_loc =
          CreateLspLocation(overlay_context, named_param.name.range(), logger_);

      if (param_def_loc && ref_loc) {
        AddReference(
            param_symbol, param_symbol.name, ref_loc->range, *param_def_loc,
            param_symbol.getParentScope());
      }
      break;
    }
  }
}

void IndexVisitor::IndexInstanceParameters(
    const slang::ast::InstanceSymbol& instance,
    const slang::syntax::ParameterValueAssignmentSyntax& params,
    const slang::ast::Symbol& syntax_owner) {
  // Parameter value assignments can be ordered or named
  // For named assignments: .FLAG(50) - we index the parameter name
  // For ordered assignments: #(50, 100) - no names to index, only values

  // NOTE: We intentionally do NOT visit parameter initializers from
  // instance.body.members() here, as those are the DEFAULT values from the
  // definition (which may be in a different compilation/preamble). We only
  // index the parameter NAMES referenced in the instantiation syntax.

  for (const auto* param_base : params.parameters) {
    // Only process named parameter assignments
    if (param_base->kind != slang::syntax::SyntaxKind::NamedParamAssignment) {
      continue;
    }

    const auto& named_param =
        param_base->as<slang::syntax::NamedParamAssignmentSyntax>();
    std::string_view param_name = named_param.name.valueText();

    // Find corresponding parameter symbol in instance body
    for (const auto& member : instance.body.members()) {
      if (member.kind != slang::ast::SymbolKind::Parameter) {
        continue;
      }

      const auto& param_symbol = member.as<slang::ast::ParameterSymbol>();
      if (param_symbol.name == param_name && param_symbol.location.valid()) {
        const auto* param_syntax = param_symbol.getSyntax();
        if (param_syntax == nullptr) {
          continue;
        }

        // Create LSP location for parameter
        // param_symbol.getCompilation() now returns the correct compilation
        // (definition's compilation) thanks to Slang fix
        auto param_def_loc = CreateSymbolLocation(param_symbol, logger_);
        // Use syntax_owner for correct cross-compilation context
        auto ref_loc =
            CreateLspLocation(syntax_owner, named_param.name.range(), logger_);

        if (param_def_loc && ref_loc) {
          AddReference(
              param_symbol, param_symbol.name, ref_loc->range, *param_def_loc,
              param_symbol.getParentScope());
        }
        break;
      }
    }
  }
}

void IndexVisitor::IndexInstancePorts(
    const slang::ast::InstanceSymbol& instance,
    const slang::syntax::HierarchicalInstanceSyntax& hierarchical_inst_syntax,
    const slang::ast::Symbol& syntax_owner) {
  // Index port connection names (e.g., .a_port, .sum_port)
  // Port connections can be named (.a_port(x)) or ordered (just (x))
  // We only index named connections where the port name is explicit

  // Iterate through port connection syntax
  for (const auto* port_conn_base : hierarchical_inst_syntax.connections) {
    // Only process named port connections
    if (port_conn_base == nullptr ||
        port_conn_base->kind !=
            slang::syntax::SyntaxKind::NamedPortConnection) {
      continue;
    }

    const auto& named_port =
        port_conn_base->as<slang::syntax::NamedPortConnectionSyntax>();
    std::string_view port_name = named_port.name.valueText();

    // Find corresponding port symbol in port connections
    auto port_connections = instance.getPortConnections();
    for (const auto* port_conn : port_connections) {
      if (port_conn == nullptr) {
        continue;
      }

      const auto& port_symbol = port_conn->port;
      if (port_symbol.name == port_name && port_symbol.location.valid()) {
        // Create LSP location for port
        auto port_def_loc = CreateSymbolLocation(port_symbol, logger_);
        // Use syntax_owner for correct cross-compilation context
        auto ref_loc =
            CreateLspLocation(syntax_owner, named_port.name.range(), logger_);

        if (port_def_loc && ref_loc) {
          AddReference(
              port_symbol, port_symbol.name, ref_loc->range, *port_def_loc,
              port_symbol.getParentScope());
        }
        break;
      }
    }
  }
}

void IndexVisitor::IndexPackageInScopedName(
    const slang::syntax::SyntaxNode* syntax,
    const slang::ast::Symbol& syntax_owner,
    const slang::ast::Symbol& target_symbol) {
  // Check if this is a scoped name (pkg::item)
  if (syntax == nullptr ||
      syntax->kind != slang::syntax::SyntaxKind::ScopedName) {
    return;
  }

  const auto& scoped = syntax->as<slang::syntax::ScopedNameSyntax>();
  // Only handle :: separator (package scope), not . (hierarchical)
  if (scoped.separator.kind != slang::parsing::TokenKind::DoubleColon) {
    return;
  }

  // Check if left part is a simple identifier
  if (scoped.left->kind != slang::syntax::SyntaxKind::IdentifierName) {
    return;
  }

  const auto& ident = scoped.left->as<slang::syntax::IdentifierNameSyntax>();

  // Walk up the scope chain to find the package
  const auto* scope = target_symbol.getParentScope();
  while (scope != nullptr) {
    const auto& scope_symbol = scope->asSymbol();
    if (scope_symbol.kind == slang::ast::SymbolKind::Package) {
      const auto& pkg = scope_symbol.as<slang::ast::PackageSymbol>();
      auto pkg_def_loc = CreateSymbolLocation(pkg, logger_);

      // Derive SM from syntax_owner's compilation
      auto ref_loc =
          CreateLspLocation(syntax_owner, ident.identifier.range(), logger_);

      if (pkg_def_loc && ref_loc) {
        AddReference(
            pkg, pkg.name, ref_loc->range, *pkg_def_loc, pkg.getParentScope());
      }
      break;  // Found package, stop searching
    }
    scope = scope->asSymbol().getParentScope();
  }
}

void IndexVisitor::IndexPackageInScopedName(
    const slang::syntax::SyntaxNode* syntax,
    const slang::ast::Expression& expr_context,
    const slang::ast::Symbol& target_symbol) {
  // Check if this is a scoped name (pkg::item)
  if (syntax == nullptr ||
      syntax->kind != slang::syntax::SyntaxKind::ScopedName) {
    return;
  }

  const auto& scoped = syntax->as<slang::syntax::ScopedNameSyntax>();
  // Only handle :: separator (package scope), not . (hierarchical)
  if (scoped.separator.kind != slang::parsing::TokenKind::DoubleColon) {
    return;
  }

  // Check if left part is a simple identifier
  if (scoped.left->kind != slang::syntax::SyntaxKind::IdentifierName) {
    return;
  }

  const auto& ident = scoped.left->as<slang::syntax::IdentifierNameSyntax>();

  // Walk up the scope chain to find the package
  const auto* scope = target_symbol.getParentScope();
  while (scope != nullptr) {
    const auto& scope_symbol = scope->asSymbol();
    if (scope_symbol.kind == slang::ast::SymbolKind::Package) {
      const auto& pkg = scope_symbol.as<slang::ast::PackageSymbol>();
      auto pkg_def_loc = CreateSymbolLocation(pkg, logger_);

      // Derive SM from expression's compilation
      auto ref_loc =
          CreateLspLocation(expr_context, ident.identifier.range(), logger_);

      if (pkg_def_loc && ref_loc) {
        AddReference(
            pkg, pkg.name, ref_loc->range, *pkg_def_loc, pkg.getParentScope());
      }
      break;  // Found package, stop searching
    }
    scope = scope->asSymbol().getParentScope();
  }
}

auto IndexVisitor::ResolveTargetSymbol(const NamedValueExpression& expr)
    -> const slang::ast::Symbol* {
  const slang::ast::Symbol* target = &expr.symbol;

  // Unwrap explicit imports
  if (expr.symbol.kind == slang::ast::SymbolKind::ExplicitImport) {
    const auto& import = expr.symbol.as<slang::ast::ExplicitImportSymbol>();
    if (const auto* imported = import.importedSymbol()) {
      target = imported;
    }
  }

  // Redirect compiler-generated variables
  if (expr.symbol.kind == slang::ast::SymbolKind::Variable) {
    const auto& var = expr.symbol.as<slang::ast::VariableSymbol>();
    if (var.flags.has(slang::ast::VariableFlags::CompilerGenerated)) {
      if (const auto* declared = var.getDeclaredSymbol()) {
        target = declared;
      } else if (const auto* parent_scope = var.getParentScope()) {
        const auto& parent = parent_scope->asSymbol();
        if (parent.kind == slang::ast::SymbolKind::Subroutine) {
          target = &parent;
        }
      }
    }
  }

  return target;
}

auto IndexVisitor::ExtractDefinitionRange(const slang::ast::Symbol& symbol)
    -> std::optional<slang::SourceRange> {
  if (!symbol.location.valid()) {
    return std::nullopt;
  }

  const auto* syntax = symbol.getSyntax();
  if (syntax == nullptr) {
    return std::nullopt;
  }

  using SK = slang::ast::SymbolKind;
  using SyntaxKind = slang::syntax::SyntaxKind;

  // Try precise extraction by kind
  switch (symbol.kind) {
    case SK::Parameter:
    case SK::EnumValue:
      if (syntax->kind == SyntaxKind::Declarator) {
        return syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
      }
      break;

    case SK::Subroutine:
      if (syntax->kind == SyntaxKind::TaskDeclaration ||
          syntax->kind == SyntaxKind::FunctionDeclaration) {
        const auto& func =
            syntax->as<slang::syntax::FunctionDeclarationSyntax>();
        if (func.prototype != nullptr && func.prototype->name != nullptr) {
          return func.prototype->name->sourceRange();
        }
      }
      break;

    case SK::StatementBlock:
      if (syntax->kind == SyntaxKind::SequentialBlockStatement ||
          syntax->kind == SyntaxKind::ParallelBlockStatement) {
        const auto& block = syntax->as<slang::syntax::BlockStatementSyntax>();
        if (block.blockName != nullptr) {
          return block.blockName->name.range();
        }
      }
      break;

    default:
      break;
  }

  // Fallback: symbol location + name length
  return slang::SourceRange(
      symbol.location, symbol.location + symbol.name.length());
}

auto IndexVisitor::ComputeReferenceRange(
    const NamedValueExpression& expr, const slang::ast::Symbol& symbol)
    -> std::optional<slang::SourceRange> {
  // For scoped names (pkg::item), use rightmost part
  slang::SourceLocation start = expr.sourceRange.start();
  if (expr.syntax != nullptr &&
      expr.syntax->kind == slang::syntax::SyntaxKind::ScopedName) {
    const auto& scoped = expr.syntax->as<slang::syntax::ScopedNameSyntax>();
    start = scoped.right->sourceRange().start();
  }

  return slang::SourceRange(
      start, start + static_cast<uint32_t>(symbol.name.length()));
}

void IndexVisitor::handle(const NamedValueExpression& expr) {
  // Step 1: Resolve target symbol (unwrap imports, compiler-generated)
  const auto* target_symbol = ResolveTargetSymbol(expr);

  // Step 2: Index package name in scoped references (e.g., pkg::PARAM)
  if (expr.syntax != nullptr) {
    IndexPackageInScopedName(expr.syntax, expr, *target_symbol);
  }

  // Step 3: Extract definition range
  auto def_range = ExtractDefinitionRange(*target_symbol);
  if (!def_range) {
    this->visitDefault(expr);
    return;
  }

  // Step 4: Compute reference range
  auto ref_range = ComputeReferenceRange(expr, *target_symbol);
  if (!ref_range) {
    this->visitDefault(expr);
    return;
  }

  // Step 5: Convert ranges and add reference
  auto ref_loc = CreateLspLocation(expr, *ref_range, logger_);
  auto def_loc = CreateLspLocation(*target_symbol, *def_range, logger_);

  if (ref_loc && def_loc) {
    AddReference(
        *target_symbol, target_symbol->name, ref_loc->range, *def_loc,
        target_symbol->getParentScope());
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const ArbitrarySymbolExpression& expr) {
  // ArbitrarySymbolExpression wraps interface instance references
  // For interface port connections, hierRef.path[0] contains the port symbol
  const slang::ast::Symbol* target_symbol = expr.symbol;
  if (expr.hierRef.isViaIfacePort() && !expr.hierRef.path.empty()) {
    target_symbol = expr.hierRef.path[0].symbol;
  }

  if (target_symbol->location.valid()) {
    if (auto def_loc = CreateSymbolLocation(*target_symbol, logger_)) {
      if (auto ref_loc = CreateLspLocation(expr, expr.sourceRange, logger_)) {
        AddReference(
            *target_symbol, target_symbol->name, ref_loc->range, *def_loc,
            target_symbol->getParentScope());
      }
    }
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const CallExpression& expr) {
  // Only handle user-defined subroutine calls, not system calls
  if (expr.isSystemCall()) {
    this->visitDefault(expr);
    return;
  }

  // Skip expressions not in current file (e.g. default arguments in preamble)
  if (expr.sourceRange.start().buffer() != current_file_buffer_) {
    this->visitDefault(expr);
    return;
  }

  const auto* subroutine_symbol = std::get_if<0>(&expr.subroutine);
  if (subroutine_symbol == nullptr || !(*subroutine_symbol)->location.valid()) {
    this->visitDefault(expr);
    return;
  }

  // Check if calling a class static method with specialization
  if (const auto* parent_scope = (*subroutine_symbol)->getParentScope()) {
    if (parent_scope->asSymbol().kind == slang::ast::SymbolKind::ClassType) {
      const auto& class_type =
          parent_scope->asSymbol().as<slang::ast::ClassType>();

      // Only index specialized classes (has genericClass pointer)
      if (class_type.genericClass != nullptr) {
        IndexClassSpecialization(class_type, expr.syntax, expr);
      }
    }
  }

  auto extract_call_range = [&]() -> std::optional<slang::SourceRange> {
    if (expr.syntax == nullptr) {
      return std::nullopt;
    }

    const slang::syntax::SyntaxNode* call_syntax = expr.syntax;

    // Unwrap ParenthesizedExpression to get the inner InvocationExpression
    // This handles size casts with function calls: (func_name(args))'(value)
    if (call_syntax->kind ==
        slang::syntax::SyntaxKind::ParenthesizedExpression) {
      const auto& paren =
          call_syntax->as<slang::syntax::ParenthesizedExpressionSyntax>();
      call_syntax = paren.expression;
    }

    if (call_syntax->kind == slang::syntax::SyntaxKind::InvocationExpression) {
      const auto& invocation =
          call_syntax->as<slang::syntax::InvocationExpressionSyntax>();

      // For ScopedName (e.g., pkg::Class#(...)::func), extract the rightmost
      // name to get precise function name range, not the entire scope chain
      if (invocation.left->kind == slang::syntax::SyntaxKind::ScopedName) {
        const auto& scoped =
            invocation.left->as<slang::syntax::ScopedNameSyntax>();
        return scoped.right->sourceRange();
      }

      return invocation.left->sourceRange();
    }

    if (call_syntax->kind ==
        slang::syntax::SyntaxKind::ArrayOrRandomizeMethodExpression) {
      const auto& method =
          call_syntax
              ->as<slang::syntax::ArrayOrRandomizeMethodExpressionSyntax>();
      if (method.method != nullptr) {
        return method.method->sourceRange();
      }
    }

    return std::nullopt;
  };

  auto call_range = extract_call_range();

  if (!call_range) {
    this->visitDefault(expr);
    return;
  }

  // Determine if this is a class method or package-scoped function
  const auto* parent_scope = (*subroutine_symbol)->getParentScope();
  const bool is_class_method =
      parent_scope != nullptr &&
      parent_scope->asSymbol().kind == slang::ast::SymbolKind::ClassType;

  // Index package names in scoped references (e.g., pkg::func())
  // But NOT for class methods (e.g., ClassName::method()) - those are handled
  // below
  if (expr.syntax != nullptr &&
      expr.syntax->kind == slang::syntax::SyntaxKind::InvocationExpression &&
      !is_class_method) {
    const auto& invocation =
        expr.syntax->as<slang::syntax::InvocationExpressionSyntax>();
    IndexPackageInScopedName(invocation.left, expr, **subroutine_symbol);
  }

  // Index class names in scoped static method calls (e.g., ClassName::method())
  if (expr.syntax != nullptr &&
      expr.syntax->kind == slang::syntax::SyntaxKind::InvocationExpression &&
      is_class_method) {
    const auto& invocation =
        expr.syntax->as<slang::syntax::InvocationExpressionSyntax>();

    // Index class name in scoped static method calls (e.g.,
    // ClassName::method())
    if (invocation.left != nullptr &&
        invocation.left->kind == slang::syntax::SyntaxKind::ScopedName) {
      const auto& scoped =
          invocation.left->as<slang::syntax::ScopedNameSyntax>();
      if (scoped.separator.kind == slang::parsing::TokenKind::DoubleColon &&
          scoped.left->kind == slang::syntax::SyntaxKind::IdentifierName) {
        // This is ClassName::method() - index the class name
        const auto& class_ident =
            scoped.left->as<slang::syntax::IdentifierNameSyntax>();

        const auto* parent_scope = (*subroutine_symbol)->getParentScope();
        if (parent_scope != nullptr && parent_scope->asSymbol().kind ==
                                           slang::ast::SymbolKind::ClassType) {
          const auto& parent_class =
              parent_scope->asSymbol().as<slang::ast::ClassType>();

          // Verify the identifier matches the class name (or its typedef)
          // Note: parent_class.name might be the specialized name like "Cache"
          // but we also need to handle typedef names like "L1Cache"
          // For now, create a reference to the class using the identifier text
          auto ref_loc =
              CreateLspLocation(expr, class_ident.identifier.range(), logger_);

          // Determine the definition location
          // For specialized classes, use the generic class definition
          const slang::ast::Symbol* def_symbol = &parent_class;
          if (parent_class.genericClass != nullptr) {
            def_symbol = parent_class.genericClass;
          }

          auto def_loc = CreateSymbolLocation(*def_symbol, logger_);

          if (ref_loc && def_loc) {
            AddReference(
                *def_symbol, class_ident.identifier.valueText(), ref_loc->range,
                *def_loc, def_symbol->getParentScope());
          }
        }
      }
    }
  }

  // Convert definition location
  // For specialized class methods, use preamble SM since definition is in
  // preamble
  std::optional<lsp::Location> def_loc;

  if (is_class_method) {
    const auto& parent_class =
        parent_scope->asSymbol().as<slang::ast::ClassType>();

    if (parent_class.genericClass != nullptr) {
      const auto* preamble_scope = parent_class.genericClass->getParentScope();
      if (preamble_scope != nullptr) {
        const auto& preamble_comp = preamble_scope->getCompilation();
        const auto* preamble_sm = preamble_comp.getSourceManager();
        if (preamble_sm != nullptr) {
          def_loc =
              CreateSymbolLocationWithSM(**subroutine_symbol, *preamble_sm);
        }
      }
    }
  }

  if (!def_loc) {
    def_loc = CreateSymbolLocation(**subroutine_symbol, logger_);
  }

  // Convert reference location
  // Since we filtered out cross-file expressions above, this is always in
  // current file
  auto ref_loc = CreateLspLocation(expr, *call_range, logger_);

  if (def_loc && ref_loc) {
    AddReference(
        **subroutine_symbol, (*subroutine_symbol)->name, ref_loc->range,
        *def_loc, (*subroutine_symbol)->getParentScope());
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const ConversionExpression& expr) {
  // Only process explicit user-written casts (e.g., type_name'(value) or
  // NUM'(value)) Skip implicit compiler-generated conversions to avoid
  // duplicates
  if (!expr.isImplicit()) {
    // Handle type casts (e.g., typedef_t'(value))
    TraverseType(*expr.type);

    // Handle size casts (e.g., NUM_ENTRIES'(value))
    // castWidthExpr stores the width expression (NUM_ENTRIES) for LSP
    // navigation
    if (const auto* width_expr = expr.getCastWidthExpr()) {
      width_expr->visit(*this);
    }
  }
  this->visitDefault(expr);
}

void IndexVisitor::handle(const DataTypeExpression& expr) {
  TraverseType(*expr.type);
  this->visitDefault(expr);
}

void IndexVisitor::handle(const MemberAccessExpression& expr) {
  auto definition_loc = CreateSymbolLocation(expr.member, logger_);
  auto ref_loc = CreateLspLocation(expr, expr.memberNameRange(), logger_);

  if (definition_loc && ref_loc) {
    AddReference(
        expr.member, expr.member.name, ref_loc->range, *definition_loc,
        expr.member.getParentScope());
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const HierarchicalValueExpression& expr) {
  // Hierarchical references like `bus.addr` or `mem_inst.array_field[idx]`
  // Each path element stores its source range (captured during hierarchical
  // lookup)
  //
  // For `bus.addr`:
  // - Path[0]: `bus` (InterfacePort) with sourceRange for "bus"
  // - Path[1]: `addr` (Variable) with sourceRange for "addr"

  // Create references for each path element using stored source ranges
  for (const auto& elem : expr.ref.path) {
    const slang::ast::Symbol* symbol = elem.symbol;

    // Handle ModportPortSymbol by redirecting to internal symbol
    if (symbol->kind == slang::ast::SymbolKind::ModportPort) {
      const auto& modport_port = symbol->as<slang::ast::ModportPortSymbol>();
      if (modport_port.internalSymbol != nullptr) {
        symbol = modport_port.internalSymbol;
      }
    }

    // Skip array elements (empty name, no source range) and invalid ranges
    const bool is_array_element =
        symbol->kind == slang::ast::SymbolKind::Instance &&
        symbol->name.empty();
    if (!is_array_element && elem.sourceRange.start().valid()) {
      auto definition_loc = CreateSymbolLocation(*symbol, logger_);
      auto ref_loc = CreateLspLocation(expr, elem.sourceRange, logger_);
      if (definition_loc && ref_loc) {
        AddReference(
            *symbol, symbol->name, ref_loc->range, *definition_loc,
            symbol->getParentScope());
      }
    }
  }

  // Visit selector expressions (e.g., ARRAY_IDX in if_array[ARRAY_IDX].signal,
  // or LOWER and UPPER in if_array[LOWER:UPPER].signal)
  // These expressions were bound during hierarchical lookup and stored in the
  // path elements as a variant (monostate for name, Expression* for index,
  // pair for range)
  for (const auto& elem : expr.ref.path) {
    std::visit(
        [this](auto&& selector) {
          using T = std::decay_t<decltype(selector)>;
          if constexpr (std::is_same_v<T, const slang::ast::Expression*>) {
            // Single-index selector
            if (selector != nullptr) {
              selector->visit(*this);
            }
          } else if constexpr (std::is_same_v<
                                   T, std::pair<
                                          const slang::ast::Expression*,
                                          const slang::ast::Expression*>>) {
            // Range selector
            if (selector.first != nullptr) {
              selector.first->visit(*this);
            }
            if (selector.second != nullptr) {
              selector.second->visit(*this);
            }
          }
          // std::monostate case - name selector, no expression to visit
        },
        elem.selectorExprs);
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const StructuredAssignmentPatternExpression& expr) {
  // Handle type reference in typed assignment patterns (e.g., type_t'{...})
  TraverseType(*expr.type);

  // Handle field references in assignment patterns like '{field1: value1,
  // field2: value2}'
  for (const auto& setter : expr.memberSetters) {
    const slang::ast::Symbol& member_symbol = *setter.member;

    // Create reference from field name in pattern to field definition
    auto definition_loc = CreateSymbolLocation(member_symbol, logger_);
    auto ref_loc = CreateLspLocation(expr, setter.keyRange, logger_);
    if (definition_loc && ref_loc) {
      AddReference(
          member_symbol, member_symbol.name, ref_loc->range, *definition_loc,
          member_symbol.getParentScope());
    }
  }

  this->visitDefault(expr);
}

void IndexVisitor::handle(const FormalArgumentSymbol& formal_arg) {
  // Formal arguments need their own handler because they're dispatched
  // separately from VariableSymbol in the visitor.

  auto def_loc = CreateSymbolLocation(formal_arg, logger_);
  if (def_loc) {
    AddDefinition(
        formal_arg, formal_arg.name, *def_loc, formal_arg.getParentScope());
  }

  // Traverse the type to index type references in argument declarations
  TraverseType(formal_arg.getType());
  this->visitDefault(formal_arg);
}

void IndexVisitor::handle(const VariableSymbol& symbol) {
  if (!symbol.location.valid()) {
    TraverseType(symbol.getType());
    this->visitDefault(symbol);
    return;
  }

  // Skip compiler-generated variables (e.g., implicit function return
  // variables)
  if (symbol.flags.has(slang::ast::VariableFlags::CompilerGenerated)) {
    TraverseType(symbol.getType());
    this->visitDefault(symbol);
    return;
  }

  auto def_loc = CreateSymbolLocation(symbol, logger_);
  if (def_loc) {
    AddDefinition(symbol, symbol.name, *def_loc, symbol.getParentScope());
  }

  TraverseType(symbol.getType());
  this->visitDefault(symbol);
}

void IndexVisitor::handle(const WildcardImportSymbol& import_symbol) {
  const auto* package = import_symbol.getPackage();
  if (package == nullptr || !package->location.valid()) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto* import_syntax = import_symbol.getSyntax();
  if (import_syntax == nullptr ||
      import_syntax->kind != slang::syntax::SyntaxKind::PackageImportItem) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto& import_item =
      import_syntax->as<slang::syntax::PackageImportItemSyntax>();
  const auto* pkg_syntax = package->getSyntax();
  if (pkg_syntax == nullptr) {
    this->visitDefault(import_symbol);
    return;
  }

  auto definition_loc = CreateSymbolLocation(*package, logger_);
  auto ref_loc =
      CreateLspLocation(import_symbol, import_item.package.range(), logger_);
  if (definition_loc && ref_loc) {
    AddReference(
        *package, package->name, ref_loc->range, *definition_loc,
        package->getParentScope());
  }
  this->visitDefault(import_symbol);
}

void IndexVisitor::handle(const ExplicitImportSymbol& import_symbol) {
  const auto* package = import_symbol.package();
  if (package == nullptr) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto* import_syntax = import_symbol.getSyntax();
  if (import_syntax == nullptr ||
      import_syntax->kind != slang::syntax::SyntaxKind::PackageImportItem) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto& import_item =
      import_syntax->as<slang::syntax::PackageImportItemSyntax>();

  auto definition_loc = CreateSymbolLocation(*package, logger_);
  auto ref_loc =
      CreateLspLocation(import_symbol, import_item.package.range(), logger_);
  if (definition_loc && ref_loc) {
    AddReference(
        *package, package->name, ref_loc->range, *definition_loc,
        package->getParentScope());
  }

  // Create entry for the imported symbol name
  const auto* imported_symbol = import_symbol.importedSymbol();
  if (imported_symbol != nullptr) {
    auto imported_definition_loc =
        CreateSymbolLocation(*imported_symbol, logger_);
    auto ref_loc =
        CreateLspLocation(import_symbol, import_item.item.range(), logger_);
    if (imported_definition_loc && ref_loc) {
      AddReference(
          *imported_symbol, imported_symbol->name, ref_loc->range,
          *imported_definition_loc, imported_symbol->getParentScope());
    }
  }

  this->visitDefault(import_symbol);
}

void IndexVisitor::handle(const ParameterSymbol& param) {
  // Skip implicit genvar localparams (they're automatically created by Slang
  // for each generate block iteration). The GenvarSymbol is already indexed.
  if (!param.isFromGenvar()) {
    auto def_loc = CreateSymbolLocation(param, logger_);
    if (def_loc) {
      AddDefinition(param, param.name, *def_loc, param.getParentScope());
    }
  }

  TraverseType(param.getType());
  this->visitDefault(param);
}

void IndexVisitor::handle(const SubroutineSymbol& subroutine) {
  auto def_loc = CreateSymbolLocation(subroutine, logger_);
  if (def_loc) {
    AddDefinition(
        subroutine, subroutine.name, *def_loc, subroutine.getParentScope());

    // Add reference for end label (e.g., "endfunction : my_func")
    if (const auto* syntax = subroutine.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::TaskDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::FunctionDeclaration) {
        const auto& func_syntax =
            syntax->as<slang::syntax::FunctionDeclarationSyntax>();
        if (func_syntax.endBlockName != nullptr) {
          auto ref_loc = CreateLspLocation(
              subroutine, func_syntax.endBlockName->name.range(), logger_);
          if (ref_loc) {
            AddReference(
                subroutine, subroutine.name, ref_loc->range, *def_loc,
                subroutine.getParentScope());
          }
        }
      }
    }
  }
  this->visitDefault(subroutine);
}

void IndexVisitor::handle(const MethodPrototypeSymbol& method_prototype) {
  auto def_loc = CreateSymbolLocation(method_prototype, logger_);
  if (def_loc) {
    AddDefinition(
        method_prototype, method_prototype.name, *def_loc,
        method_prototype.getParentScope());
  }

  // Traverse return type and arguments for type references
  TraverseType(method_prototype.getReturnType());
  for (const auto* arg : method_prototype.getArguments()) {
    TraverseType(arg->getType());
  }

  this->visitDefault(method_prototype);
}

void IndexVisitor::handle(const DefinitionSymbol& definition) {
  if (definition.location.valid()) {
    if (const auto* syntax = definition.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ModuleDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::InterfaceDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::ProgramDeclaration) {
        const auto& decl_syntax =
            syntax->as<slang::syntax::ModuleDeclarationSyntax>();

        if (auto def_loc = CreateLspLocation(
                definition, decl_syntax.header->name.range(), logger_)) {
          AddDefinition(
              definition, definition.name, *def_loc,
              definition.getParentScope());

          // Add reference for end label (e.g., "endmodule : Test")
          if (decl_syntax.blockName != nullptr) {
            auto ref_loc = CreateLspLocation(
                definition, decl_syntax.blockName->name.range(), logger_);
            if (ref_loc) {
              AddReference(
                  definition, definition.name, ref_loc->range, *def_loc,
                  definition.getParentScope());
            }
          }
        }
      }
    }
  }

  // Interfaces are handled differently - FromCompilation creates instances
  // for them instead of visiting the DefinitionSymbol directly
  // (see target_interface_instances in FromCompilation)
  if (definition.definitionKind != slang::ast::DefinitionKind::Interface) {
    this->visitDefault(definition);
  }
}

void IndexVisitor::handle(const TypeAliasType& type_alias) {
  auto def_loc = CreateSymbolLocation(type_alias, logger_);
  if (def_loc) {
    AddDefinition(
        type_alias, type_alias.name, *def_loc, type_alias.getParentScope());
  }

  // Need to traverse the target type for cases like: typedef data_from_t
  // data_to_t; This ensures we create references for data_from_t
  TraverseType(type_alias.targetType.getType());
  this->visitDefault(type_alias);
}

void IndexVisitor::handle(const EnumValueSymbol& enum_value) {
  auto def_loc = CreateSymbolLocation(enum_value, logger_);
  if (def_loc) {
    AddDefinition(
        enum_value, enum_value.name, *def_loc, enum_value.getParentScope());
  }
  this->visitDefault(enum_value);
}

void IndexVisitor::handle(const FieldSymbol& field) {
  auto def_loc = CreateSymbolLocation(field, logger_);
  if (def_loc) {
    AddDefinition(field, field.name, *def_loc, field.getParentScope());
  }

  TraverseType(field.getType());
  this->visitDefault(field);
}

void IndexVisitor::handle(const NetSymbol& net) {
  auto def_loc = CreateSymbolLocation(net, logger_);
  if (def_loc) {
    AddDefinition(net, net.name, *def_loc, net.getParentScope());
  }

  TraverseType(net.getType());
  this->visitDefault(net);
}

void IndexVisitor::handle(const ClassPropertySymbol& class_property) {
  auto def_loc = CreateSymbolLocation(class_property, logger_);
  if (def_loc) {
    AddDefinition(
        class_property, class_property.name, *def_loc,
        class_property.getParentScope());
  }

  TraverseType(class_property.getType());
  this->visitDefault(class_property);
}

void IndexVisitor::handle(const GenericClassDefSymbol& class_def) {
  // Parameterized classes: class C #(parameter P);
  // Slang creates GenericClassDefSymbol as the definition symbol
  if (class_def.location.valid()) {
    // CRITICAL: GenericClassDefSymbol does NOT expose class body as children
    // (similar to ModuleSymbol vs InstanceSymbol pattern).
    // We must get a ClassType specialization to access parameters and members.
    // Use getDefaultSpecialization() to create a temporary instance with
    // default parameter values.
    const slang::ast::Scope* class_type_scope = nullptr;
    if (const auto* parent_scope = class_def.getParentScope()) {
      if (const auto* default_type =
              class_def.getDefaultSpecialization(*parent_scope)) {
        if (default_type->isClass()) {
          const auto& class_type =
              default_type->getCanonicalType().as<slang::ast::ClassType>();
          class_type_scope = &class_type.as<slang::ast::Scope>();
        }
      }
    }

    // Add GenericClassDef definition with ClassType scope as children_scope
    auto def_loc = CreateSymbolLocation(class_def, logger_);
    if (def_loc) {
      AddDefinition(
          class_def, class_def.name, *def_loc, class_def.getParentScope(),
          class_type_scope);

      // Add reference for end label (e.g., "endclass : MyClass")
      if (const auto* syntax = class_def.getSyntax()) {
        if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
          const auto& class_syntax =
              syntax->as<slang::syntax::ClassDeclarationSyntax>();
          if (class_syntax.endBlockName != nullptr) {
            auto ref_loc = CreateLspLocation(
                class_def, class_syntax.endBlockName->name.range(), logger_);
            if (ref_loc) {
              AddReference(
                  class_def, class_def.name, ref_loc->range, *def_loc,
                  class_def.getParentScope());
            }
          }
        }
      }
    }

    // NOTE: No URI filtering needed here - FromCompilation() already filters
    // symbols by file, so this handler only runs for classes in current file.
    if (const auto* parent_scope = class_def.getParentScope()) {
      if (const auto* default_type =
              class_def.getDefaultSpecialization(*parent_scope)) {
        // Index base class reference using stored range from Slang
        if (default_type->isClass()) {
          const auto& class_type =
              default_type->getCanonicalType().as<slang::ast::ClassType>();
          if (const auto* base = class_type.getBaseClass()) {
            auto base_ref_range = class_type.getBaseClassRefRange();
            if (base->isClass() && base_ref_range.start().valid()) {
              const auto& base_class =
                  base->getCanonicalType().as<slang::ast::ClassType>();
              // For parameterized classes, use genericClass as the definition
              // symbol
              const auto* base_symbol =
                  (base_class.genericClass != nullptr)
                      ? static_cast<const slang::ast::Symbol*>(
                            base_class.genericClass)
                      : static_cast<const slang::ast::Symbol*>(&base_class);

              if (base_symbol->location.valid()) {
                if (const auto* base_syntax = base_symbol->getSyntax()) {
                  if (base_syntax->kind ==
                      slang::syntax::SyntaxKind::ClassDeclaration) {
                    auto base_def_range =
                        base_syntax->as<slang::syntax::ClassDeclarationSyntax>()
                            .name.range();
                    auto base_def_loc = CreateLspLocation(
                        *base_symbol, base_def_range, logger_);
                    auto ref_loc =
                        CreateLspLocation(class_type, base_ref_range, logger_);
                    if (base_def_loc && ref_loc) {
                      AddReference(
                          *base_symbol, base_symbol->name, ref_loc->range,
                          *base_def_loc, base_symbol->getParentScope());
                    }
                  }
                }
              }
            }
          }
        }

        // Visit parameter assignment expressions to index symbol references
        if (default_type->isClass()) {
          const auto& class_type =
              default_type->getCanonicalType().as<slang::ast::ClassType>();
          for (const auto* expr : class_type.parameterAssignmentExpressions) {
            if (expr != nullptr) {
              expr->visit(*this);
            }
          }
        }

        // Visit the default specialization to index class body
        default_type->visit(*this);
      }
    }

    // Note: We don't call visitDefault(class_def) because it won't traverse
    // into the class body (the body is only accessible via ClassType)
  }
}

void IndexVisitor::handle(const ClassType& class_type) {
  // ClassType serves dual roles in Slang's architecture:
  // 1. Standalone definition for non-parameterized classes
  // 2. Specialization container for parameterized classes (genericClass !=
  // nullptr)
  //
  // We only create definition for role #1 to avoid duplicates with
  // GenericClassDefSymbol This pattern respects Slang's compilation-optimized
  // design while maintaining LSP correctness
  if (class_type.genericClass == nullptr) {
    auto def_loc = CreateSymbolLocation(class_type, logger_);
    if (def_loc) {
      AddDefinition(
          class_type, class_type.name, *def_loc, class_type.getParentScope());

      // Add reference for end label (e.g., "endclass : MyClass")
      if (const auto* syntax = class_type.getSyntax()) {
        if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
          const auto& class_syntax =
              syntax->as<slang::syntax::ClassDeclarationSyntax>();
          if (class_syntax.endBlockName != nullptr) {
            auto ref_loc = CreateLspLocation(
                class_type, class_syntax.endBlockName->name.range(), logger_);
            if (ref_loc) {
              AddReference(
                  class_type, class_type.name, ref_loc->range, *def_loc,
                  class_type.getParentScope());
            }
          }
        }
      }
    }

    if (const auto* syntax = class_type.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
        // Index base class reference using stored range from Slang
        if (const auto* base = class_type.getBaseClass()) {
          auto base_ref_range = class_type.getBaseClassRefRange();
          if (base->isClass() && base_ref_range.start().valid()) {
            const auto& base_class =
                base->getCanonicalType().as<slang::ast::ClassType>();
            // For parameterized classes, use genericClass as the definition
            // symbol
            const auto* base_symbol =
                (base_class.genericClass != nullptr)
                    ? static_cast<const slang::ast::Symbol*>(
                          base_class.genericClass)
                    : static_cast<const slang::ast::Symbol*>(&base_class);

            auto base_definition_loc =
                CreateSymbolLocation(*base_symbol, logger_);
            auto ref_loc =
                CreateLspLocation(class_type, base_ref_range, logger_);
            if (base_definition_loc && ref_loc) {
              AddReference(
                  *base_symbol, base_symbol->name, ref_loc->range,
                  *base_definition_loc, base_symbol->getParentScope());
            }
          }
        }
      }
    }
  }

  // DESIGN PRINCIPLE: ClassType body traversal should ONLY happen via explicit
  // visit from GenericClassDefSymbol.getDefaultSpecialization().
  // Type references to ClassType (variables, parameters) should NOT traverse
  // the body - they're handled by TypeReferenceSymbol wrapping.
  //
  // This eliminates the need for:
  // - Syntax-based deduplication (no duplicate traversal)
  // - URI filtering (only explicit visits from current file's
  // GenericClassDefSymbol)
  // - visited_type_syntaxes_ tracking
  //
  // The body traversal happens in GenericClassDefSymbol handler via explicit
  // default_type->visit(*this), which calls visitDefault() to index members.
  this->visitDefault(class_type);
}

void IndexVisitor::handle(const InterfacePortSymbol& interface_port) {
  auto def_loc = CreateSymbolLocation(interface_port, logger_);
  if (def_loc) {
    AddDefinition(
        interface_port, interface_port.name, *def_loc,
        interface_port.getParentScope());

    // Index references in dimension expressions (e.g., inputs[NUM_INPUTS])
    auto dimensions = interface_port.getDimensions();
    if (dimensions && !dimensions->empty()) {
      for (const auto& dim : *dimensions) {
        dim.visitExpressions(
            [this](const slang::ast::Expression& expr) -> void {
              expr.visit(*this);
            });
      }
    }

    // Create cross-reference from interface name to interface definition
    if (interface_port.interfaceDef != nullptr &&
        interface_port.interfaceDef->location.valid()) {
      auto interface_name_range = interface_port.interfaceNameRange();
      if (interface_name_range.start().valid()) {
        auto interface_definition_loc =
            CreateSymbolLocation(*interface_port.interfaceDef, logger_);
        auto ref_loc =
            CreateLspLocation(interface_port, interface_name_range, logger_);
        if (interface_definition_loc && ref_loc) {
          AddReference(
              *interface_port.interfaceDef, interface_port.interfaceDef->name,
              ref_loc->range, *interface_definition_loc,
              interface_port.interfaceDef->getParentScope());
        }
      }
    }

    // Create cross-reference from modport name to modport definition
    // modportSymbol was cached by getModport() during instance elaboration
    if (interface_port.modportSymbol != nullptr &&
        interface_port.interfaceDef != nullptr) {
      auto modport_name_range = interface_port.modportNameRange();
      if (modport_name_range.start().valid()) {
        // CROSS-COMPILATION: modportSymbol is looked up from instance.body,
        // which is created from interfaceDef (may be in preamble).
        // The modportSymbol pointer is cached in overlay's InterfacePortSymbol,
        // but the actual ModportSymbol object lives in the preamble
        // compilation. Therefore, modportSymbol->location has a preamble
        // BufferID.
        //
        // Use interfaceDef (preamble) to derive the SourceManager for decoding
        // modport_location_range, following the principle:
        // "derive SourceManager from the AST node that owns the range"
        slang::SourceRange modport_location_range(
            interface_port.modportSymbol->location,
            interface_port.modportSymbol->location +
                interface_port.modportSymbol->name.length());
        auto modport_definition_loc = CreateLspLocation(
            *interface_port.interfaceDef, modport_location_range, logger_);
        auto ref_loc =
            CreateLspLocation(interface_port, modport_name_range, logger_);

        if (modport_definition_loc && ref_loc) {
          AddReference(
              *interface_port.modportSymbol, interface_port.modportSymbol->name,
              ref_loc->range, *modport_definition_loc,
              interface_port.modportSymbol->getParentScope());
        }
      }
    }
  }
  // Skip visitDefault to avoid traversing interface port's nested scope
}

void IndexVisitor::handle(const ModportSymbol& modport) {
  if (modport.location.valid()) {
    auto def_loc = CreateSymbolLocation(modport, logger_);
    if (def_loc) {
      AddDefinition(modport, modport.name, *def_loc, modport.getParentScope());
    }
  }
  this->visitDefault(modport);
}

void IndexVisitor::handle(const ModportPortSymbol& modport_port) {
  if (modport_port.location.valid()) {
    if (const auto* syntax = modport_port.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ModportNamedPort) {
        auto source_range =
            syntax->as<slang::syntax::ModportNamedPortSyntax>().name.range();

        // ModportPortSymbol references the underlying interface member
        // Create reference from modport port name to the actual signal
        if (modport_port.internalSymbol != nullptr &&
            modport_port.internalSymbol->location.valid()) {
          auto target_loc =
              CreateSymbolLocation(*modport_port.internalSymbol, logger_);
          auto ref_loc = CreateLspLocation(modport_port, source_range, logger_);
          if (target_loc && ref_loc) {
            AddReference(
                *modport_port.internalSymbol, modport_port.name, ref_loc->range,
                *target_loc, modport_port.getParentScope());
          }
        }
      }
    }
  }
  // Skip visitDefault - ModportPortSymbol is just a reference wrapper around
  // internalSymbol, no meaningful children to traverse
}

void IndexVisitor::handle(const InstanceArraySymbol& instance_array) {
  // Handle arrays of interface instances (e.g., array_if if_array[4] ();)
  // InstanceArraySymbol contains multiple InstanceSymbol children
  const auto* syntax = instance_array.getSyntax();

  // Visit dimension expressions to index parameter references (e.g.,
  // if_array[ARRAY_SIZE])
  if (instance_array.dimension.has_value()) {
    instance_array.dimension->visitExpressions(
        [this](const slang::ast::Expression& expr) -> void {
          expr.visit(*this);
        });
  }

  if (syntax != nullptr &&
      syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    // Check if this is an interface array by looking at first element
    bool is_interface_array = false;
    if (!instance_array.elements.empty()) {
      const auto* first_elem = instance_array.elements[0];
      if (first_elem != nullptr &&
          first_elem->kind == slang::ast::SymbolKind::Instance) {
        const auto& first_instance =
            first_elem->as<slang::ast::InstanceSymbol>();
        is_interface_array = first_instance.isInterface();
      }
    }

    // Handle both interface and module arrays
    // 1. Create self-definition for array name
    auto def_loc = CreateSymbolLocation(instance_array, logger_);
    if (def_loc) {
      AddDefinition(
          instance_array, instance_array.name, *def_loc,
          instance_array.getParentScope());
    }

    // 2. Create reference from type name to definition (module or interface)
    const auto* parent_syntax = syntax->parent;
    if (parent_syntax != nullptr &&
        parent_syntax->kind ==
            slang::syntax::SyntaxKind::HierarchyInstantiation) {
      const auto& inst_syntax =
          parent_syntax->as<slang::syntax::HierarchyInstantiationSyntax>();

      // Get definition from first array element
      if (!instance_array.elements.empty()) {
        const auto* first_elem = instance_array.elements[0];
        if (first_elem != nullptr &&
            first_elem->kind == slang::ast::SymbolKind::Instance) {
          const auto& first_instance =
              first_elem->as<slang::ast::InstanceSymbol>();
          const auto& definition = first_instance.getDefinition();
          auto definition_loc = CreateSymbolLocation(definition, logger_);
          auto ref_loc = CreateLspLocation(
              first_instance, inst_syntax.type.range(), logger_);

          if (definition_loc && ref_loc) {
            AddReference(
                definition, definition.name, ref_loc->range, *definition_loc,
                definition.getParentScope());
          }

          // 3. Index parameter overrides (e.g., #(.FLAG(1)))
          if (inst_syntax.parameters != nullptr) {
            IndexInstanceParameters(
                first_instance, *inst_syntax.parameters, instance_array);
          }
        }
      }
    }

    // For interface arrays, skip visitDefault to avoid duplicate references
    // For module arrays, also skip (we only need the type reference, not body)
    if (is_interface_array) {
      return;  // Skip body elaboration for interfaces
    }
  }

  // Module arrays fall through - body already skipped by SkipBody flag
}

void IndexVisitor::handle(const InstanceSymbol& instance) {
  const auto* syntax = instance.getSyntax();

  // Skip array elements (they have empty names and are handled by
  // InstanceArraySymbol)
  if (instance.name.empty()) {
    // Array element - skip to avoid duplicate references
    // Body traversal is controlled by InstanceArraySymbol handler
    return;
  }

  // Create references for module and interface instances in module bodies
  if (syntax != nullptr &&
      syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    // 1. Create self-definition for instance name
    auto def_loc = CreateSymbolLocation(instance, logger_);
    if (def_loc) {
      AddDefinition(
          instance, instance.name, *def_loc, instance.getParentScope());
    }

    // 2. Create reference from type name to module/interface definition
    const auto* parent_syntax = syntax->parent;
    if (parent_syntax != nullptr &&
        parent_syntax->kind ==
            slang::syntax::SyntaxKind::HierarchyInstantiation) {
      const auto& inst_syntax =
          parent_syntax->as<slang::syntax::HierarchyInstantiationSyntax>();

      // Get definition from instance (module or interface)
      const auto& definition = instance.getDefinition();
      auto def_loc = CreateSymbolLocation(definition, logger_);
      auto ref_loc =
          CreateLspLocation(instance, inst_syntax.type.range(), logger_);

      if (def_loc && ref_loc) {
        AddReference(
            definition, definition.name, ref_loc->range, *def_loc,
            definition.getParentScope());
      }

      // 3. Index parameter overrides (e.g., #(.FLAG(1)))
      if (inst_syntax.parameters != nullptr) {
        IndexInstanceParameters(instance, *inst_syntax.parameters, instance);
      }

      // 3b. Index port connection names (e.g., .a_port in .a_port(x))
      // Get HierarchicalInstanceSyntax to access port connections
      const auto& hierarchical_inst_syntax =
          syntax->as<slang::syntax::HierarchicalInstanceSyntax>();
      IndexInstancePorts(instance, hierarchical_inst_syntax, instance);
    }

    // 4. Visit parameter value expressions (e.g., .WIDTH(BUS_WIDTH))
    // The parameter symbols in instance.body have the override values bound
    for (const auto* param : instance.body.getParameters()) {
      if (param->symbol.kind == slang::ast::SymbolKind::Parameter) {
        const auto& p = param->symbol.as<slang::ast::ParameterSymbol>();
        if (const auto* expr = p.getInitializer()) {
          expr->visit(*this);
        }
      }
      // Type parameters don't have initializer expressions to visit
    }
  }

  // 5. Visit port connection expressions (e.g., .clk_port(sys_clk))
  // This indexes all variable references in port connections for all port types
  // (PortSymbol, MultiPortSymbol, InterfacePortSymbol)
  auto port_connections = instance.getPortConnections();
  for (const auto* port_conn : port_connections) {
    if (port_conn == nullptr) {
      continue;
    }

    const auto* expr = port_conn->getExpression();
    if (expr != nullptr) {
      expr->visit(*this);
    }
  }

  // Control body traversal - only traverse standalone instances, not nested
  // ones SINGLE-FILE MODE: We only index the current module's body (via
  // createDefault in PATH 1). Nested instances (submodules, interface
  // instances) should NOT have their bodies traversed - those are indexed when
  // their definition file is opened.
  const auto* parent = instance.getParentScope();
  if (parent != nullptr &&
      parent->asSymbol().kind == slang::ast::SymbolKind::CompilationUnit) {
    // Standalone instance (only relevant for interfaces) - traverse body
    this->visitDefault(instance);
  }

  // Nested instance (inside a module/interface) - skip body traversal
  // This prevents us from indexing submodule/interface bodies in single-file
  // mode
}

void IndexVisitor::handle(const GenerateBlockArraySymbol& generate_array) {
  // Visit inline genvar declarations (for (genvar j = 0; ...))
  for (const auto& member : generate_array.members()) {
    if (member.kind == slang::ast::SymbolKind::Genvar) {
      member.visit(*this);
    }
  }

  // For external genvars (genvar idx; for (idx = 0; ...)), create reference
  // for LHS identifier
  if (generate_array.externalGenvarRefRange.has_value() &&
      generate_array.genvar != nullptr) {
    auto definition_loc = CreateSymbolLocation(*generate_array.genvar, logger_);
    auto ref_range = *generate_array.externalGenvarRefRange;
    auto ref_loc = CreateLspLocation(generate_array, ref_range, logger_);
    if (definition_loc && ref_loc) {
      AddReference(
          *generate_array.genvar, generate_array.genvar->name, ref_loc->range,
          *definition_loc, generate_array.genvar->getParentScope());
    }
  }

  // Visit loop control expressions
  if (generate_array.initialExpression != nullptr) {
    generate_array.initialExpression->visit(*this);
  }
  if (generate_array.stopExpression != nullptr) {
    generate_array.stopExpression->visit(*this);
  }
  if (generate_array.iterExpression != nullptr) {
    generate_array.iterExpression->visit(*this);
  }

  // Index only first entry - all entries are identical
  if (!generate_array.entries.empty()) {
    const auto* first_entry = generate_array.entries[0];
    if (first_entry != nullptr) {
      first_entry->visit(*this);
    }
  }
  // NOTE: No visitDefault() - we manually control traversal
}

void IndexVisitor::handle(const GenerateBlockSymbol& generate_block) {
  // Create reference for generate block definition (only if explicitly named)
  if (generate_block.location.valid()) {
    if (const auto* syntax = generate_block.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::GenerateBlock) {
        const auto& gen_block =
            syntax->as<slang::syntax::GenerateBlockSyntax>();

        // Only create reference if there's an explicit name in the source code
        if (gen_block.beginName != nullptr) {
          // Extract name from syntax, not symbol (symbol.name may be empty)
          std::string_view block_name = gen_block.beginName->name.valueText();

          if (auto def_loc = CreateLspLocation(
                  generate_block, gen_block.beginName->name.range(), logger_)) {
            // Skip GenerateBlockArray parent since it's not indexed in document
            // symbols
            const slang::ast::Scope* parent_scope =
                generate_block.getParentScope();
            if (parent_scope != nullptr &&
                parent_scope->asSymbol().kind ==
                    slang::ast::SymbolKind::GenerateBlockArray) {
              parent_scope = parent_scope->asSymbol().getParentScope();
            }

            // Use name from syntax, not symbol (symbol.name may be empty for
            // generate blocks)
            AddDefinition(generate_block, block_name, *def_loc, parent_scope);

            // Add reference for end label (e.g., "end : gen_loop")
            if (gen_block.endName != nullptr) {
              auto ref_loc = CreateLspLocation(
                  generate_block, gen_block.endName->name.range(), logger_);
              if (ref_loc) {
                AddReference(
                    generate_block, block_name, ref_loc->range, *def_loc,
                    parent_scope);
              }
            }
          }
        }
        // For unnamed blocks (auto-generated names like "genblk1"), don't
        // create reference since users can't click on text that doesn't exist
        // in source
      }
    }
  }

  // Visit condition expression for if/case generate blocks
  // For example: if (ENABLE) has a reference to ENABLE parameter
  // Multiple sibling blocks (if/else branches, case branches) share the same
  // condition pointer, so we deduplicate to avoid visiting it multiple times
  if (generate_block.conditionExpression != nullptr) {
    auto [_, inserted] =
        visited_generate_conditions_.insert(generate_block.conditionExpression);
    if (inserted) {
      generate_block.conditionExpression->visit(*this);
    }
  }

  // Visit case item expressions for case generate blocks
  // For example: case (MODE) MODE_A: has a reference to MODE_A parameter
  for (const auto* item_expr : generate_block.caseItemExpressions) {
    if (item_expr != nullptr) {
      item_expr->visit(*this);
    }
  }

  this->visitDefault(generate_block);
}

void IndexVisitor::handle(const GenvarSymbol& genvar) {
  auto def_loc = CreateSymbolLocation(genvar, logger_);
  if (def_loc) {
    AddDefinition(genvar, genvar.name, *def_loc, genvar.getParentScope());
  }
}

void IndexVisitor::handle(const PackageSymbol& package) {
  auto def_loc = CreateSymbolLocation(package, logger_);
  if (def_loc) {
    AddDefinition(package, package.name, *def_loc, package.getParentScope());

    // Add reference for end label (e.g., "endpackage : TestPkg")
    if (const auto* syntax = package.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::PackageDeclaration) {
        const auto& decl_syntax =
            syntax->as<slang::syntax::ModuleDeclarationSyntax>();
        if (decl_syntax.blockName != nullptr) {
          auto ref_loc = CreateLspLocation(
              package, decl_syntax.blockName->name.range(), logger_);
          if (ref_loc) {
            AddReference(
                package, package.name, ref_loc->range, *def_loc,
                package.getParentScope());
          }
        }
      }
    }
  }
  this->visitDefault(package);
}

void IndexVisitor::handle(const StatementBlockSymbol& statement_block) {
  // StatementBlockSymbol represents named statement blocks (e.g., assertion
  // labels) Only index if it has a valid name (not empty or auto-generated)
  if (!statement_block.name.empty()) {
    auto def_loc = CreateSymbolLocation(statement_block, logger_);
    if (def_loc) {
      AddDefinition(
          statement_block, statement_block.name, *def_loc,
          statement_block.getParentScope());
    }
  }
  this->visitDefault(statement_block);
}

void IndexVisitor::handle(const UninstantiatedDefSymbol& symbol) {
  const auto* syntax = symbol.getSyntax();
  if (syntax == nullptr) {
    return;  // Nothing to index without syntax
  }

  // Always create self-definition for instance name (same-file and
  // cross-file)
  if (syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    auto def_loc = CreateSymbolLocation(symbol, logger_);
    if (def_loc) {
      AddDefinition(symbol, symbol.name, *def_loc, symbol.getParentScope());
    }
  }

  // Visit parameter expressions (for same-file cases)
  // UninstantiatedDefSymbol now stores properly typed ParameterSymbols
  for (const auto* param : symbol.getParameters()) {
    if (param->symbol.kind == slang::ast::SymbolKind::Parameter) {
      const auto& p = param->symbol.as<slang::ast::ParameterSymbol>();
      if (const auto* expr = p.getInitializer()) {
        expr->visit(*this);
      }
    }
    // Type parameters don't have initializer expressions to visit
  }

  // Index interface port connections by extracting symbols from bound
  // expressions Slang already resolved port connections during binding -
  // extract the symbols
  auto port_conns = symbol.getPortConnections();
  for (const auto* assertion_expr : port_conns) {
    if (assertion_expr != nullptr) {
      // Always visit the expression tree to index nested references
      assertion_expr->visit(*this);

      // Additionally, check if this is an interface instance reference
      // (SimpleAssertionExpr with ArbitrarySymbolExpression)
      if (assertion_expr->kind == slang::ast::AssertionExprKind::Simple) {
        const auto& simple =
            assertion_expr->as<slang::ast::SimpleAssertionExpr>();
        const auto& expr = simple.expr;

        // Check if expression is ArbitrarySymbolExpression (interface
        // instance ref)
        if (expr.kind == slang::ast::ExpressionKind::ArbitrarySymbol) {
          const auto& arb = expr.as<slang::ast::ArbitrarySymbolExpression>();
          const auto& ref_symbol = *arb.symbol;  // Dereference not_null

          // Handle both single interface instances and interface arrays
          if (ref_symbol.kind == slang::ast::SymbolKind::Instance) {
            const auto& instance_symbol =
                ref_symbol.as<slang::ast::InstanceSymbol>();
            if (instance_symbol.isInterface()) {
              auto definition_loc =
                  CreateSymbolLocation(instance_symbol, logger_);
              auto ref_loc =
                  CreateLspLocation(symbol, expr.sourceRange, logger_);
              if (definition_loc && ref_loc) {
                // Create reference using the expression's source range
                AddReference(
                    instance_symbol, instance_symbol.name, ref_loc->range,
                    *definition_loc, instance_symbol.getParentScope());
              }
            }
          } else if (ref_symbol.kind == slang::ast::SymbolKind::InstanceArray) {
            const auto& instance_array =
                ref_symbol.as<slang::ast::InstanceArraySymbol>();
            // Check if this is an interface array by examining first element
            if (!instance_array.elements.empty() &&
                instance_array.elements[0] != nullptr &&
                instance_array.elements[0]->kind ==
                    slang::ast::SymbolKind::Instance) {
              const auto& first_instance =
                  instance_array.elements[0]->as<slang::ast::InstanceSymbol>();
              if (first_instance.isInterface()) {
                auto definition_loc =
                    CreateSymbolLocation(instance_array, logger_);
                auto ref_loc =
                    CreateLspLocation(symbol, expr.sourceRange, logger_);
                if (definition_loc && ref_loc) {
                  // Create reference using the expression's source range
                  AddReference(
                      instance_array, instance_array.name, ref_loc->range,
                      *definition_loc, instance_array.getParentScope());
                }
              }
            }
          } else if (ref_symbol.kind == slang::ast::SymbolKind::InterfacePort) {
            const auto& iface_port =
                ref_symbol.as<slang::ast::InterfacePortSymbol>();
            auto definition_loc = CreateSymbolLocation(iface_port, logger_);
            auto ref_loc = CreateLspLocation(symbol, expr.sourceRange, logger_);
            if (definition_loc && ref_loc) {
              // Create reference using the expression's source range
              AddReference(
                  iface_port, iface_port.name, ref_loc->range, *definition_loc,
                  iface_port.getParentScope());
            }
          }
        }
      }
    }
  }

  // Index the module/interface definition reference
  // With preamble injection, UninstantiatedDefSymbol now has getDefinition()
  // method
  const auto* definition = symbol.getDefinition();
  if (definition != nullptr &&
      syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    const auto* parent_syntax = syntax->parent;
    if (parent_syntax != nullptr &&
        parent_syntax->kind ==
            slang::syntax::SyntaxKind::HierarchyInstantiation) {
      const auto& inst_syntax =
          parent_syntax->as<slang::syntax::HierarchyInstantiationSyntax>();
      auto type_range = inst_syntax.type.range();

      // Create reference from module/interface type name to definition
      auto def_loc = CreateSymbolLocation(*definition, logger_);
      auto ref_loc = CreateLspLocation(symbol, type_range, logger_);
      if (def_loc && ref_loc) {
        AddReference(
            *definition, symbol.definitionName, ref_loc->range, *def_loc,
            symbol.getParentScope());
      }

      // Index parameter assignments (e.g., #(.WIDTH(32), .DEPTH(64)))
      // For UninstantiatedDefSymbol, we can access parameter metadata from
      // DefinitionSymbol.parameters (ParameterDecl structs with name and
      // location)
      if (inst_syntax.parameters != nullptr) {
        for (const auto* param_base : inst_syntax.parameters->parameters) {
          // Only process named parameter assignments
          if (param_base->kind !=
              slang::syntax::SyntaxKind::NamedParamAssignment) {
            continue;
          }

          const auto& named_param =
              param_base->as<slang::syntax::NamedParamAssignmentSyntax>();
          std::string_view param_name = named_param.name.valueText();

          // Find corresponding parameter in definition's parameter list
          const auto& def_sym = definition->as<slang::ast::DefinitionSymbol>();
          for (const auto& param_decl : def_sym.parameters) {
            if (param_decl.name == param_name && param_decl.location.valid()) {
              // Create SourceRange for parameter name (location + name
              // length)
              auto end_offset =
                  param_decl.location.offset() + param_decl.name.length();
              auto end_loc = slang::SourceLocation(
                  param_decl.location.buffer(), end_offset);
              slang::SourceRange param_range(param_decl.location, end_loc);

              // Use CreateLspLocation to safely convert SourceRange to LSP
              // location This handles cross-compilation correctly
              auto param_def_loc =
                  CreateLspLocation(*definition, param_range, logger_);
              auto ref_loc =
                  CreateLspLocation(symbol, named_param.name.range(), logger_);

              if (param_def_loc && ref_loc) {
                const auto* parent_scope = definition->getParentScope();
                AddReference(
                    *definition, param_decl.name, ref_loc->range,
                    *param_def_loc, parent_scope);
              }
              break;
            }
          }
        }
      }

      // Index port connections (e.g., .a_port(x), .sum_port(result))
      // Parse PortListSyntax from definition to extract port names and
      // locations
      const auto& hierarchical_inst_syntax =
          syntax->as<slang::syntax::HierarchicalInstanceSyntax>();
      const auto& def_sym = definition->as<slang::ast::DefinitionSymbol>();
      if (def_sym.portList != nullptr &&
          def_sym.portList->kind == slang::syntax::SyntaxKind::AnsiPortList) {
        const auto& ansi_port_list =
            def_sym.portList->as<slang::syntax::AnsiPortListSyntax>();

        // Iterate through port connections in instantiation
        for (const auto* port_conn_base :
             hierarchical_inst_syntax.connections) {
          // Only process named port connections
          if (port_conn_base == nullptr ||
              port_conn_base->kind !=
                  slang::syntax::SyntaxKind::NamedPortConnection) {
            continue;
          }

          const auto& named_port =
              port_conn_base->as<slang::syntax::NamedPortConnectionSyntax>();
          std::string_view port_name = named_port.name.valueText();

          // Find matching port in definition's ANSI port list
          for (const auto* port_syntax : ansi_port_list.ports) {
            if (port_syntax != nullptr &&
                port_syntax->kind ==
                    slang::syntax::SyntaxKind::ImplicitAnsiPort) {
              const auto& implicit_port =
                  port_syntax->as<slang::syntax::ImplicitAnsiPortSyntax>();
              if (implicit_port.declarator != nullptr &&
                  implicit_port.declarator->name.valueText() == port_name) {
                // Found matching port - create reference using safe helper
                slang::SourceRange port_range =
                    implicit_port.declarator->name.range();

                auto port_def_loc =
                    CreateLspLocation(*definition, port_range, logger_);
                auto ref_loc =
                    CreateLspLocation(symbol, named_port.name.range(), logger_);

                if (port_def_loc && ref_loc) {
                  const auto* parent_scope = definition->getParentScope();
                  AddReference(
                      *definition, port_name, ref_loc->range, *port_def_loc,
                      parent_scope);
                }
                break;
              }
            }
          }
        }
      }
    }
  }
}

}  // namespace slangd::semantic
