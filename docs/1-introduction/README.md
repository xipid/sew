# Introduction

Sew is an agentic, zero-configuration compiler wrapper and build orchestrator that unifies native C++ compilation and WebAssembly/JavaScript target building under a single tool. It replaces CMake, Ninja, CCache, and manual binding generators (like Embind or Napi) with a fully integrated static analysis and compilation pipeline.

## Core Philosophy

Traditional C++ build systems separate the compilation of source files (e.g. via Make/Ninja) from the description of the build (e.g. via CMakeLists) and the caching layer (e.g. via CCache). This separation results in thousands of lines of boilerplate configuration, slow cold builds, and fragile dependency tracking, particularly when targeting web environments.

Sew unifies these steps:
1. **Static Analysis**: Parses C++ source files directly to resolve preprocessor directives and build a precise dependency graph.
2. **Global Hashing**: Computes content hashes of files, compiler configurations, and system-level parameters to guarantee cache correctness globally.
3. **Direct Code Generation**: Translates C++ class reflection and function signatures into clean, type-safe TypeScript interfaces and WebAssembly/Native bindings.

## Key Capabilities

* **Boilerplate-Free Building**: No build configuration files are needed. Sew infers compilation parameters and header paths from source imports.
* **Global Content Caching**: Hashed objects are cached globally under the user's home directory (`~/.cache/sew/cache/`), enabling instantaneous cache reuse across multiple projects.
* **Dual-Target Native and Web Pipeline**: Compiles C++ to native AMD64 architectures or bundles it transparently into WebAssembly with auto-generated TypeScript typed interfaces.
* **Runtime Reflection Registry**: Enables runtime hot-reloading and memory swizzling of live objects when new library layouts are loaded dynamically.
