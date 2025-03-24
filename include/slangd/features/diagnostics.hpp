#pragma once

#include <memory>
#include <string>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include "lsp/diagnostic.hpp"

namespace slangd {

/**
 * Extracts and converts diagnostics from a syntax tree
 *
 * This function extracts syntax errors, preprocessor errors,
 * and other parse-time diagnostics from a syntax tree.
 *
 * @param syntax_tree The syntax tree to extract diagnostics from
 * @param source_manager The source manager for location information
 * @param diag_engine The diagnostic engine to format messages and get
 * severities
 * @param uri The URI of the document being processed
 * @return Vector of LSP diagnostics
 */
std::vector<lsp::Diagnostic> ExtractSyntaxDiagnostics(
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri);

/**
 * Extracts and converts diagnostics from a compilation
 *
 * This function extracts semantic errors, type checking errors,
 * and other compilation-time diagnostics from a compilation.
 *
 * @param compilation The compilation to extract diagnostics from
 * @param source_manager The source manager for location information
 * @param diag_engine The diagnostic engine to format messages and get
 * severities
 * @param uri The URI of the document being processed
 * @return Vector of LSP diagnostics
 */
std::vector<lsp::Diagnostic> ExtractSemanticDiagnostics(
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri);

/**
 * Extracts and converts diagnostics from both syntax tree and compilation
 *
 * This is the main entry point for extracting all diagnostics related
 * to a document.
 *
 * @param syntax_tree The syntax tree to extract diagnostics from
 * @param compilation The compilation to extract diagnostics from
 * @param source_manager The source manager for location information
 * @param diag_engine The diagnostic engine to format messages and get
 * severities
 * @param uri The URI of the document being processed
 * @return Vector of LSP diagnostics
 */
std::vector<lsp::Diagnostic> GetDocumentDiagnostics(
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri);

/**
 * Helper function to convert Slang diagnostics to LSP diagnostics
 *
 * @param slang_diagnostics The Slang diagnostics to convert
 * @param source_manager The source manager for location information
 * @param diag_engine The diagnostic engine to format messages and get
 * severities
 * @param uri The URI of the document being processed
 * @return Vector of LSP diagnostics for the specified URI only
 */
std::vector<lsp::Diagnostic> ConvertDiagnostics(
    const slang::Diagnostics& slang_diagnostics,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri);

}  // namespace slangd
