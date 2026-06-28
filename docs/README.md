# Introduction

Sew is a high-performance build tool, dependency resolver, preprocessor, and compiler wrapper designed to replace CMake, Ninja, CCache, and WebAssembly bindings generation for C++ and JavaScript.

## Core Design

Instead of relying on boilerplate configuration files, Sew dynamically discovers dependency graphs by parsing C++ `#include` directives and TypeScript `import` statements. 

* **Direct Compilation**: Interfaces directly with Clang and linkers.
* **Content-Addressable Caching**: Built-in caching (~/.cache/sew/cache/) tracks content hashes and compilation flags globally.
* **Universal Targets**: Compiles natively to AMD64 shared/executable binaries, or targets WebAssembly with automated TypeScript bindings generation.
* **Runtime Reflection**: Automatically generates metadata descriptors and registration hooks for types and functions.
