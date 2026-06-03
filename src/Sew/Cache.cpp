/**
 * @file Cache.cpp
 * @brief Content-addressable build cache implementation.
 */

#include <Sew/Cache.hpp>
#include <Xi/Primitives.hpp>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <ctime>

namespace Sew {

using namespace Xi;

String Cache::hashContent(const String& content) {
    // FNV-1a 64-bit hash → hex string
    u64 h = 14695981039346656037ULL;
    const u64 prime = 1099511628211ULL;
    const u8* d = content.data();
    for (usz i = 0; i < content.size(); ++i) {
        h ^= (u64)d[i];
        h *= prime;
    }

    // Convert to 16-char hex string
    String result;
    const char hex[] = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) {
        result.push(hex[(h >> i) & 0xf]);
    }
    return result;
}

String Cache::computeKey(
    const String& sourceContent,
    const String& targetName,
    const Array<String>& flags,
    const Array<String>& depHashes)
{
    // Combine all inputs into a single string and hash it
    String combined;
    combined += hashContent(sourceContent);
    combined += ":";
    combined += targetName;
    for (usz i = 0; i < flags.size(); ++i) {
        combined += ":";
        combined += flags[i];
    }
    for (usz i = 0; i < depHashes.size(); ++i) {
        combined += ":";
        combined += depHashes[i];
    }
    return hashContent(combined);
}

String Cache::cacheDir() {
    const char* home = ::getenv("HOME");
    if (!home) home = "/tmp";
    String dir(home);
    dir += "/.cache/sew/cache";
    return dir;
}

static void mkdirRecursive(const String& path) {
    String current;
    const u8* d = path.data();
    for (usz i = 0; i < path.size(); ++i) {
        current.push(d[i]);
        if (d[i] == '/') {
            ::mkdir(current.c_str(), 0755);
        }
    }
    ::mkdir(path.c_str(), 0755);
}

String Cache::get(const String& key) {
    String path = cacheDir();
    path += "/";
    path += key;

    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return "";

    // Get file size
    struct stat st;
    if (::fstat(fd, &st) != 0) { ::close(fd); return ""; }

    String content;
    content.allocate((usz)st.st_size);
    usz total = 0;
    while (total < (usz)st.st_size) {
        ssize_t n = ::read(fd, (void*)(content.data() + total),
                           (usz)st.st_size - total);
        if (n <= 0) break;
        total += (usz)n;
    }
    ::close(fd);

    // Set the actual size
    // InlineArray uses push, so we need to push byte by byte for correctness
    // Actually, we already allocated. Let's re-do this simply:
    String result;
    u8 buf[8192];
    fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return "";
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i)
            result.push(buf[i]);
    }
    ::close(fd);
    return result;
}

void Cache::set(const String& key, const String& content) {
    String dir = cacheDir();
    mkdirRecursive(dir);

    String path = dir;
    path += "/";
    path += key;

    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    const u8* d = content.data();
    usz remaining = content.size();
    usz offset = 0;
    while (remaining > 0) {
        ssize_t n = ::write(fd, d + offset, remaining);
        if (n <= 0) break;
        offset += (usz)n;
        remaining -= (usz)n;
    }
    ::close(fd);
}

bool Cache::has(const String& key) {
    String path = cacheDir();
    path += "/";
    path += key;

    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

usz Cache::cleanOld(int maxAgeDays) {
    String dir = cacheDir();
    DIR* d = ::opendir(dir.c_str());
    if (!d) return 0;

    time_t now = ::time(nullptr);
    time_t maxAge = (time_t)maxAgeDays * 86400;
    usz cleaned = 0;

    struct dirent* entry;
    while ((entry = ::readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        String path = dir;
        path += "/";
        path += entry->d_name;

        struct stat st;
        if (::stat(path.c_str(), &st) == 0) {
            if (now - st.st_mtime > maxAge) {
                ::unlink(path.c_str());
                cleaned++;
            }
        }
    }
    ::closedir(d);
    return cleaned;
}

} // namespace Sew
