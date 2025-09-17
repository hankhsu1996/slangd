# Unified Semantic Index Architecture

## Motivation

The current slangd implementation has several semantic indexing systems that evolved independently:

1. **DefinitionIndex**: Handles go-to-definition by mapping symbols to their definition locations
2. **SymbolIndex**: Provides document symbols for outline view
3. **DiagnosticIndex**: Extracts compilation errors/warnings (separate data path)

This duplication causes gaps in coverage, divergent logic, and higher maintenance cost. It also blocks delivery of LSP features like rename, hover, semantic tokens, inlay hints, and find references.

### Current Problems

#### 1. Whitelist-Based Visitor Pattern

The DefinitionIndex uses a whitelist approach, explicitly handling only **10 out of 123 symbol types** via `makeVisitor()` lambda handlers:

- **Covered**: PackageSymbol, DefinitionSymbol, UninstantiatedDefSymbol, TypeAliasType, VariableSymbol, ParameterSymbol, PortSymbol, StatementBlockSymbol, NamedValueExpression
- **Missing**: 113+ symbol types including Subroutine, ClassType, EnumType, Instance, Field, Modport, etc.
- **Result**: Severe go-to-definition coverage gaps

#### 2. Duplicate Traversal Logic

- **DefinitionIndex**: Uses `makeVisitor()` with lambda handlers, visits definitions + packages
- **SymbolIndex**: Uses scope-based traversal with 200+ lines of type-specific hierarchy logic
- **Different filtering**: SymbolIndex handles ~20 symbol types, DefinitionIndex handles ~10
- **Result**: Maintenance burden, potential inconsistencies, and duplicated effort

#### 3. Visitor Pattern Limitation

The core constraint: slang's ASTVisitor cannot have a generic "catch-all" handler that processes every symbol type. You must either:

- Explicitly handle each type (current whitelist approach)
- Use `visitDefault()` which only traverses children, doesn't process current node

## Key Observations

### 1. Symbol Base Class Properties

ALL symbols in slang have these properties:

```cpp
class Symbol {
    SymbolKind kind;        // Enum of 123 types
    std::string_view name;  // May be empty
    SourceLocation location;
};
```

### 2. Scope Inheritance Pattern

30+ symbol types inherit from `Scope`, providing automatic traversal:

- PackageSymbol, ClassType, SubroutineSymbol, etc.
- These can be traversed uniformly via `scope.members()`

### 3. Diagnostics Have Different Data Path

Diagnostics come from `compilation.getAllDiagnostics()`, not symbol traversal. They should remain separate as they:

- Include parse errors before symbol creation
- Have different lifecycle and caching needs
- Don't map 1:1 to symbols

## Proposed Solution: Unified Semantic Index

### Core Innovation: Universal Symbol Processing

**Primary Approach**: Add a minimal preVisit hook to slang's ASTVisitor that enables processing EVERY symbol without whitelisting:

```cpp
// In slang/include/slang/ast/ASTVisitor.h, lines 74-87
// EXACT LOCATION IDENTIFIED: Insert after line 79
template<typename T>
void visit(const T& t) {
    if constexpr (!VisitBad && requires { t.bad(); }) {
        if (t.bad())
            return;
    }

    // NEW: Universal pre-visit hook for symbols (2 lines)
    if constexpr (std::is_base_of_v<Symbol, T> && requires { (DERIVED).preVisit(t); })
        (DERIVED).preVisit(t);

    // Existing code continues unchanged...
    if constexpr (requires { (DERIVED).handle(t); })
        (DERIVED).handle(t);
    // ...
}
```

**Technical Validation**:
- Uses existing SFINAE pattern consistent with slang's design
- All 123 symbol types dispatch through `Symbol::visit()` method
- Zero impact on existing visitors (purely additive)

**Fallback**: If upstream integration becomes complex, implement a simple generic walker that visits all symbols and scopes uniformly.

## Architecture Design Decisions

### Memory Model and Pointer Strategy

**Use raw pointers for non-owning references** - This is the correct modern C++ approach:
- Slang owns all symbols via bump allocation in `Compilation`
- Symbol lifetime guaranteed by slang's `Compilation` object
- Raw pointers are idiomatic for non-owning references when lifetime is managed elsewhere
- Zero overhead compared to smart pointer indirection
- `std::observer_ptr` remains experimental and not in C++23 standard

**Reference slang symbols directly** - Don't duplicate symbol data:
- Leverage slang's efficient bump allocator (`Compilation : public BumpAllocator`)
- Symbols already have good cache locality
- No custom allocator maintenance needed
- Consistent with slang's existing architecture

### Two-Phase Processing Strategy

**Phase 1: Index Building (preVisit)** - Cache LSP-specific conversions:
```cpp
void preVisit(const Symbol& symbol) {
    // 1. Unwrap transparent members
    const Symbol* unwrapped = unwrapSymbol(symbol);

    // 2. Filter early
    if (!shouldIndex(unwrapped)) return;

    // 3. Create SymbolInfo with cached conversions
    SymbolInfo info {
        .symbol = unwrapped,
        .lspKind = convertToLspKind(unwrapped->kind),
        .range = computeLspRange(unwrapped->location),
        .parent = unwrapped->getParentScope(),
        .isDocumentSymbol = isRelevantForDocumentSymbols(unwrapped)
    };

    // 4. Store in flat index
    symbols[unwrapped->location] = std::move(info);
}
```

**Phase 2: LSP Request Handling (on-demand)** - Build structures from flat data:
```cpp
auto getDocumentSymbols(const std::string& uri) -> std::vector<DocumentSymbol> {
    return buildHierarchyFromFlat(uri);
}
```

## Refined Architecture Design

### Core Data Structures

```cpp
class UnifiedSemanticIndex {
public:
    // LSP-optimized symbol information with cached conversions
    struct SymbolInfo {
        // Core reference to slang's symbol
        const slang::ast::Symbol* symbol;  // The unwrapped, actual symbol

        // Cached LSP conversions (computed during indexing)
        lsp::SymbolKind lspKind;          // Pre-converted symbol kind
        lsp::Range range;                  // Pre-computed range
        lsp::Range selectionRange;         // Pre-computed selection range

        // Hierarchy data (using raw pointers - lifetime managed by slang)
        const Symbol* parent;              // For building tree structure
        std::vector<const Symbol*> children; // Direct children

        // Filtering flags
        bool isDocumentSymbol;             // Should appear in outline?
        bool isDefinition;                 // Is this a definition location?

        // Future extensions (added on-demand)
        std::optional<TypeInfo> typeInfo;  // For hover
        std::optional<BitWidth> bitWidth;  // SystemVerilog features
    };

    // Core indexes - flat storage for efficient queries
    std::unordered_map<SourceLocation, SymbolInfo> symbols;                    // All indexed symbols
    std::unordered_map<SourceLocation, const Symbol*> locationToSymbol;        // Click location -> Symbol
    std::unordered_map<SourceLocation, SourceLocation> references;             // Usage -> Definition
    std::unordered_map<SourceLocation, std::vector<SourceLocation>> reverseRefs; // Definition -> Usages

    // LSP API - build structures on-demand from flat data
    auto getDefinition(SourceLocation loc) -> std::optional<SourceLocation>;
    auto getDocumentSymbols(const std::string& uri) -> std::vector<DocumentSymbol>;
    auto getReferences(SourceLocation loc) -> std::vector<SourceLocation>;     // Future
    auto getHover(SourceLocation loc) -> std::optional<HoverInfo>;             // Future

private:
    class IndexVisitor : public ASTVisitor<IndexVisitor, true, true> {
        // Universal symbol processing - called for EVERY symbol type!
        void preVisit(const Symbol& symbol) {
            // Unwrap transparent members
            const Symbol* unwrapped = unwrapSymbol(symbol);

            // Early filtering
            if (!shouldIndex(*unwrapped)) return;

            // Cache LSP conversions during indexing
            SymbolInfo info {
                .symbol = unwrapped,
                .lspKind = convertToLspKind(unwrapped->kind),
                .range = computeLspRange(unwrapped->location),
                .selectionRange = computeSelectionRange(*unwrapped),
                .parent = unwrapped->getParentScope(),
                .isDocumentSymbol = isRelevantForDocumentSymbols(*unwrapped),
                .isDefinition = true
            };

            // Store in flat index
            symbols[unwrapped->location] = std::move(info);
            locationToSymbol[unwrapped->location] = unwrapped;
        }

        // Reference tracking for go-to-definition and find-references
        void handle(const NamedValueExpression& expr) {
            if (expr.symbol.location) {
                references[expr.sourceRange.start()] = expr.symbol.location;
                reverseRefs[expr.symbol.location].push_back(expr.sourceRange.start());
            }
            visitDefault(expr);
        }

        // Add handlers for other reference types discovered in DefinitionIndex analysis
        void handle(const NamedTypeSyntax& syntax) {
            // Type references in variable/parameter declarations
            visitDefault(syntax);
        }

        // Add more reference handlers as needed
        void handle(const IdentifierNameSyntax& syntax) {
            // Handle other reference types
            visitDefault(syntax);
        }
    };

    // Hierarchy building - on-demand from flat data
    // REPLACES: 200+ lines of complex type-specific logic in current SymbolIndex
    auto buildHierarchyFromFlat(const std::string& uri) -> std::vector<DocumentSymbol> {
        // Step 1: Collect symbols from this file (pre-filtered during indexing)
        std::vector<const SymbolInfo*> fileSymbols;
        for (const auto& [loc, info] : symbols) {
            if (info.isDocumentSymbol && isInFile(loc, uri)) {
                fileSymbols.push_back(&info);
            }
        }

        // Step 2: Build parent-child relationships using cached parent pointers
        // SIMPLER THAN: Current BuildSymbolChildren() type-specific traversal
        std::unordered_map<const Symbol*, std::vector<const SymbolInfo*>> childrenMap;
        std::vector<const SymbolInfo*> roots;

        for (auto* info : fileSymbols) {
            if (info->parent && isParentInFile(info->parent, uri)) {
                childrenMap[info->parent].push_back(info);
            } else {
                roots.push_back(info);
            }
        }

        // Step 3: Recursively build DocumentSymbol tree
        std::vector<DocumentSymbol> result;
        for (auto* root : roots) {
            result.push_back(buildDocumentSymbol(root, childrenMap));
        }
        return result;
    }

    // Symbol unwrapping (from current SymbolIndex analysis)
    const Symbol* unwrapSymbol(const Symbol& symbol) {
        if (symbol.kind != SymbolKind::TransparentMember) {
            return &symbol;
        }
        return unwrapSymbol(symbol.as<TransparentMemberSymbol>().wrapped);
    }
};
```

### Why Reference Maps Are Still Needed

Even though slang symbols have internal pointers, we need location-based lookups for LSP operations:

```cpp
// User clicks at position 10:42 in file.sv
// LSP gives us: SourceLocation(10:42)
// We need: Location -> Symbol -> Definition -> Location

// 1. Click location to symbol (replaces current LookupSymbolAt() linear search)
std::unordered_map<SourceLocation, const Symbol*> locationToSymbol;

// 2. Usage location to definition location (extends current AddReference() pattern)
std::unordered_map<SourceLocation, SourceLocation> references;

// 3. Definition to all usage locations (new capability for find-references)
std::unordered_map<SourceLocation, std::vector<SourceLocation>> reverseRefs;
```

**Current DefinitionIndex patterns to replicate:**
- `AddDefinition(key, range)` - stores symbol definition locations
- `AddReference(range, key)` - stores symbol usage locations
- `LookupSymbolAt(loc)` - linear search through reference map (inefficient)

Our approach provides O(1) lookups vs current O(n) linear search.

### Architecture Benefits

- **Complete Coverage**: ALL 123 symbol types processed automatically via preVisit
- **Single Traversal**: One pass builds all semantic data (symbols + references)
- **Efficient Caching**: LSP conversions computed once during indexing
- **Memory Efficient**: Reference slang symbols, don't duplicate data
- **Flexible Queries**: Flat storage enables different access patterns
- **Hierarchy On-Demand**: Build document symbol trees when needed
- **Future Ready**: Foundation for semantic tokens, hover, find references
- **Modern C++**: Raw pointers for non-owning refs, idiomatic design

## Advanced Features (Future Phases)

**Critical Analysis**: The external feedback suggests several advanced features. We should mention these but NOT implement in MVP since we haven't shipped yet.

### Phase 2: Enhanced Data (Post-MVP)

```cpp
struct SymbolInfo {
    // Basic info (Phase 1)
    SymbolKind kind;
    std::string_view name;
    SourceLocation location;

    // Enhanced data (Phase 2 - compute on demand)
    std::optional<TypeInfo> typeInfo;        // For hover
    std::optional<std::string> documentation; // Doc comments
    std::optional<BitWidth> bitWidth;        // SystemVerilog specific
};

// Add specific handlers for types we care about
void handle(const VariableSymbol& var) {
    auto& info = symbols[var.location];
    info.bitWidth = extractBitWidth(var.getType()); // SystemVerilog feature
    visitDefault(var);
}
```

### Phase 3: Performance Optimizations (Future)

**External feedback suggests**: "per-file slabs", "two-phase fill", "incremental updates"

**Critical assessment**: These are premature optimizations for MVP. Consider only after we have:

1. Working implementation with basic features
2. Performance problems in practice
3. User feedback on what's actually slow

**If needed later**:

- Organize data per-file for faster incremental updates
- Two-phase approach: structure first, expensive details on-demand
- Cache frequently accessed data

### Phase 4: Advanced LSP Features (Future)

```cpp
// Only after MVP is solid
auto getHover(SourceLocation loc) -> std::optional<HoverInfo>;
auto getReferences(SourceLocation loc) -> std::vector<SourceLocation>;
auto getSemanticTokens(SourceRange range) -> std::vector<SemanticToken>;
auto getInlayHints(SourceRange range) -> std::vector<InlayHint>; // Bit widths, etc.
```

### Phase 5: Rename Safety (Advanced)

**External feedback suggests**: "prepare step that rejects anonymous or synthesized names"

**Assessment**: This is very advanced and requires deep SystemVerilog semantics. Defer until we have basic rename working.

## Diagnostics: Keep Separate (Confirmed)

Analysis confirms diagnostics should remain in DiagnosticIndex:

1. **Different Source**: Come from `compilation.getAllDiagnostics()`, not symbol traversal
2. **Include Parse Errors**: Exist before symbols are created
3. **Different Lifecycle**: Updated on each keystroke vs symbol structure
4. **MVP Priority**: Error reporting is working and stable

## Implementation Plan (MVP Focus)

### Week 1-2: Core Foundation

1. Add preVisit hook to slang's ASTVisitor (2 lines)
2. Create basic UnifiedSemanticIndex structure
3. Implement preVisit for universal symbol collection
4. Build test suite comparing with current DefinitionIndex

### Week 3-4: Parallel Operation

1. Keep existing indexes operational
2. Add feature flag to enable new index
3. Validate go-to-definition works for ALL symbol types
4. Compare performance (single vs multiple traversals)

### Week 5-6: Migration

1. Switch go-to-definition to unified index
2. Switch document symbols to unified index
3. Remove old DefinitionIndex and SymbolIndex
4. Ship MVP with complete symbol coverage

### Post-MVP: Future Phases

- Phase 2: Add hover with type info
- Phase 3: Add find references
- Phase 4: Performance optimizations if needed
- Phase 5: Advanced features like inlay hints

## Critical Assessment of External Feedback

### Good Ideas to Adopt

- **Single traversal emphasis** - aligns with our approach
- **Keep diagnostics separate** - confirms our analysis
- **Migration with parallel operation** - reduces risk
- **Two-phase concept** - but simplified (basic info first, enhancements later)

### Over-Engineering for MVP

- **"Stable identity with canonical semantic paths"** - complex, unclear benefits
- **"Per-file slabs"** - premature optimization, adds complexity
- **Concurrency and parallelization** - not needed until we have perf problems
- **Complex incremental update strategies** - solve after basic functionality works

### SystemVerilog-Specific Features (Future)

- **Bit width inlay hints** - interesting for SystemVerilog, but advanced
- **Packed structure analysis** - domain-specific, post-MVP
- **Rename safety for nets/interfaces** - requires deep language knowledge

## Success Metrics (MVP)

1. Go-to-definition works for ALL 123 symbol types (not just current 10)
2. Single traversal replaces multiple separate walks
3. Document outline shows complete symbol hierarchy
4. Reduced code complexity and maintenance burden
5. Foundation ready for hover/rename features

## Risks and Mitigations

### Technical Risks

- **Slang hook complexity**: Fallback to generic walker
- **Performance regression**: Profile and optimize incrementally
- **Coverage gaps**: Start with current DefinitionIndex coverage, expand systematically

### Project Risks

- **Scope creep**: Focus on MVP - resist adding advanced features until shipped
- **Over-engineering**: Keep simple until proven we need complexity
- **Delayed shipping**: Parallel operation allows safe migration

## Conclusion

The Unified Semantic Index solves our core problem (incomplete symbol coverage) through a well-architected solution that balances efficiency, maintainability, and extensibility.

### Key Design Wins

- **Modern C++ Practices**: Raw pointers for non-owning references, leveraging slang's bump allocation
- **Efficient Processing**: Two-phase approach with cached LSP conversions during indexing
- **Complete Coverage**: Universal preVisit processing handles ALL 123 symbol types automatically
- **Flexible Architecture**: Flat storage with parent pointers enables both hierarchical and flat queries
- **Future Ready**: Foundation supports semantic tokens, hover, find references, and rename

### Implementation Strategy

The detailed implementation plan is documented in `UNIFIED_SEMANTIC_INDEX_TODO.local.md` with clear phases:

1. **Phase 0-1**: Research and slang ASTVisitor enhancement
2. **Phase 2-3**: Core implementation and LSP features
3. **Phase 4-5**: Testing, validation, and migration
4. **Future Phases**: Advanced LSP features (hover, semantic tokens, rename)

### Risk Management

- **Parallel operation** during migration reduces deployment risk
- **Fallback strategies** for slang integration complexity
- **Incremental approach** allows validation at each step
- **Performance monitoring** ensures no regressions

This architecture positions slangd for comprehensive LSP feature support while maintaining the performance and reliability users expect.
