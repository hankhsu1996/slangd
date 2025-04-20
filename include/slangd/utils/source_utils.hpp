#pragma once

#include <memory>
#include <string>

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd {

/**
 * Determine if a source location belongs to a specific document URI
 *
 * @param location The Slang source location to check
 * @param source_manager The source manager that contains the location
 * @param uri The URI of the document to check against
 * @return True if the location is in the document with the given URI
 */
auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool;

/**
 * Extract a filename from a path or URI
 *
 * @param path A file path or URI that may contain directory components
 * @return The filename part without directory components
 */
auto ExtractFilename(const std::string& path) -> std::string;

/**
 * Check if a file is a SystemVerilog file based on its extension
 *
 * @param path The file path to check
 * @return True if the file has a SystemVerilog extension (.sv, .svh, .v, .vh)
 */
auto IsSystemVerilogFile(const std::string& path) -> bool;

/**
 * Check if a file is a config file based on its extension
 *
 * @param path The file path to check
 * @return True if the file has a config extension (.slangd)
 */
auto IsConfigFile(const std::string& path) -> bool;

/**
 * Normalize a file path by resolving symbolic links and returning the canonical
 * path
 *
 * @param path The file path to normalize
 * @return The canonical path with symbolic links resolved, or the original path
 * if canonical cannot be determined
 */
auto NormalizePath(const std::string& path) -> std::string;

}  // namespace slangd
