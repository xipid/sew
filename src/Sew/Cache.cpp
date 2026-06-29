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
#include <cstdio>
#include <mutex>

extern "C" {
    void crypto_blake2b(unsigned char *hash, size_t hash_size, const unsigned char *msg, size_t msg_size);
}

namespace Sew {

using namespace Xi;

static String getClangVersion() {
    static String s_version;
    static bool s_loaded = false;
    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_loaded) {
        FILE* f = popen("clang++ --version 2>/dev/null", "r");
        if (f) {
            char buf[256];
            if (fgets(buf, sizeof(buf), f)) {
                s_version = buf;
            }
            pclose(f);
        }
        s_loaded = true;
    }
    return s_version;
}

static String getSewSelfMetadata() {
    static String s_meta;
    static bool s_loaded = false;
    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_loaded) {
        char path[1024];
        ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len != -1) {
            path[len] = '\0';
            struct stat st;
            if (::stat(path, &st) == 0) {
                s_meta = String((long long)st.st_mtime) + ":" + String((long long)st.st_size);
            }
        }
        s_loaded = true;
    }
    return s_meta;
}

String Cache::hashContent(const String& content) {
    unsigned char hash[32];
    crypto_blake2b(hash, 32, (const unsigned char*)content.data(), content.size());
    
    String result;
    const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        result.push(hex[(hash[i] >> 4) & 0xf]);
        result.push(hex[hash[i] & 0xf]);
    }
    return result;
}

String Cache::computeKey(
    const String& sourceContent,
    const String& targetName,
    const Array<String>& flags,
    const Array<String>& depHashes)
{
    String combined;
    combined += hashContent(sourceContent);
    combined += ":";
    combined += targetName;
    combined += ":";
    
    // Hash environment variables
    const char* env1 = ::getenv("SEW_EXTRA_FLAGS");
    if (env1) combined += env1;
    combined += ":";
    const char* env2 = ::getenv("SEW_XIC_INCLUDE");
    if (env2) combined += env2;
    combined += ":";
    const char* env3 = ::getenv("SEW_EXTRA_INCLUDE");
    if (env3) combined += env3;
    combined += ":";
    
    // Hash compiler version
    combined += getClangVersion();
    combined += ":";
    
    // Hash sew self metadata
    combined += getSewSelfMetadata();
    
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

String Cache::computeKeyFromHash(
    const String& contentHash,
    const String& targetName,
    const Array<String>& flags,
    const Array<String>& depHashes)
{
    String combined;
    combined += contentHash;
    combined += ":";
    combined += targetName;
    combined += ":";
    
    // Hash environment variables
    const char* env1 = ::getenv("SEW_EXTRA_FLAGS");
    if (env1) combined += env1;
    combined += ":";
    const char* env2 = ::getenv("SEW_XIC_INCLUDE");
    if (env2) combined += env2;
    combined += ":";
    const char* env3 = ::getenv("SEW_EXTRA_INCLUDE");
    if (env3) combined += env3;
    combined += ":";
    
    // Hash compiler version
    combined += getClangVersion();
    combined += ":";
    
    // Hash sew self metadata
    combined += getSewSelfMetadata();
    
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
    return content;
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
