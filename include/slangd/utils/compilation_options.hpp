#pragma once

#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>

namespace slangd::utils {

// Creates LSP compilation options with maximum compatibility mode.
// Used by PreambleManager, OverlaySession, and test fixtures.
//
// Configuration:
// - PreprocessorOptions: initialDefaultNetType = Unknown
// - LexerOptions: enableLegacyProtect = true
// - CompilationFlags: LanguageServerMode + all --compat all flags
// - errorLimit = 0
//
// Ensures tests match production behavior.
inline auto CreateLspCompilationOptions() -> slang::Bag {
  slang::Bag options;

  // Disable implicit net declarations for stricter diagnostics
  slang::parsing::PreprocessorOptions pp_options;
  pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;
  options.set(pp_options);

  // Enable legacy protection directives
  slang::parsing::LexerOptions lexer_options;
  lexer_options.enableLegacyProtect = true;
  options.set(lexer_options);

  // LSP mode with maximum compatibility (--compat all equivalent)
  slang::ast::CompilationOptions comp_options;
  comp_options.flags |=
      slang::ast::CompilationFlags::LanguageServerMode |
      slang::ast::CompilationFlags::AllowHierarchicalConst |
      slang::ast::CompilationFlags::RelaxEnumConversions |
      slang::ast::CompilationFlags::AllowUseBeforeDeclare |
      slang::ast::CompilationFlags::RelaxStringConversions |
      slang::ast::CompilationFlags::AllowRecursiveImplicitCall |
      slang::ast::CompilationFlags::AllowBareValParamAssignment |
      slang::ast::CompilationFlags::AllowSelfDeterminedStreamConcat |
      slang::ast::CompilationFlags::AllowMergingAnsiPorts |
      slang::ast::CompilationFlags::AllowTopLevelIfacePorts |
      slang::ast::CompilationFlags::AllowUnnamedGenerate;
  comp_options.errorLimit = 0;
  options.set(comp_options);

  return options;
}

}  // namespace slangd::utils
