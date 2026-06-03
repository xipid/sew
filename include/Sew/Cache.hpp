/**
 * @file Cache.hpp
 * @brief Content-addressable build cache for Sew.
 *
 * Cache directory: ~/.cache/sew/cache/
 * Each entry is a file named by its key, containing the compiled output.
 * Entries older than 30 days are cleaned on exit.
 */

#pragma once

#include <Collection/String.hpp>
#include <Collection/Array.hpp>

namespace Sew {

using namespace Collection;

class Cache {
public:
    /// Compute a cache key from content + target + flags + dependency hashes.
    static String computeKey(
        const String& sourceContent,
        const String& targetName,
        const Array<String>& flags,
        const Array<String>& depHashes);

    /// FNV-1a hash of content (fast, deterministic, 64-bit hex).
    static String hashContent(const String& content);

    /// Get the cache directory path (~/.cache/sew/cache/).
    static String cacheDir();

    /// Ensure directory exists recursively.
    static void mkdirRecursive(const String& path);

    /// Read a cached entry. Returns empty string on miss.
    static String get(const String& key);

    /// Write a cached entry.
    static void set(const String& key, const String& content);

    /// Check if a cache entry exists.
    static bool has(const String& key);

    /// Clean entries older than maxAgeDays.
    static usz cleanOld(int maxAgeDays = 30);
};

} // namespace Sew
