# Caching System

Sew features a content-addressable build cache to ensure fast incremental compilation.

## Cache Key Computation

The cache key for a compilation unit is computed using a Blake2b hash of the following parameters:
1. **Source Content**: Hash of the source file content after preprocessing.
2. **Target Name**: The output path of the compiled object file.
3. **Compiler Flags**: All active compiler flags and options.
4. **Environment Parameters**: Environment variables that affect compilation (e.g. `SEW_EXTRA_FLAGS`, `SEW_XIC_INCLUDE`).
5. **Clang Version**: Clang compiler version string.
6. **Sew Metadata**: Modification time and size of the `sew` binary itself.
7. **Dependency Hashes**: Content hashes of all header files imported by the compilation unit.

## Storage Layout

All cache data is stored inside the user's home directory under `~/.cache/sew/cache/`:

* **`~/.cache/sew/cache/<key>`**: A text file containing the path to the cached object file (e.g. `~/.cache/sew/cache/<key>.o`).
* **`~/.cache/sew/cache/<key>.o`**: The actual compiled binary object file.

When a compilation is requested:
1. Computes the `cacheKey`.
2. Checks if `~/.cache/sew/cache/<cacheKey>` exists, and reads the object path from it.
3. Checks if the object path (`~/.cache/sew/cache/<cacheKey>.o`) actually exists on disk.
4. If it exists, the cache is hit, and Sew directly links the object file from the cache, skipping compilation entirely.
5. If the object file is missing, the cache is missed, the file is recompiled, and the new object file is saved to the cache.
