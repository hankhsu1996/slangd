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
bool IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Extract a filename from a path or URI
 *
 * @param path A file path or URI that may contain directory components
 * @return The filename part without directory components
 */
std::string ExtractFilename(const std::string& path);

}  // namespace slangd
