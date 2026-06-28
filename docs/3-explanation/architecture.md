# Architecture

Sew coordinates C++ header parsing, type generation, and object linking under a single unified pipeline.

## Compilation Pipeline

When you execute a build target, Sew processes the compilation through these stages:

1. **Import Parsing & Discovery**: Parses TypeScript `import` statements or C++ `#include` files.
2. **Topological Sort**: Computes the dependency graph. C++ headers are prioritized and compiled before JavaScript files.
3. **AST Type Extraction**: Reads headers and extracts classes, structs, namespaces, field offsets, methods, and docstring comments.
4. **Bridge Code Generation**:
   * **C++ Bridge (`sew_bridge.cpp`)**: Generates flat `extern "C"` wrappers for classes, getters/setters, constructors, and method dispatches.
   * **JS Glue (`sew_bridge.js`)**: Generates JavaScript class wrappers utilizing WebAssembly memory views.
   * **TS Typings (`sew_bridge.d.ts` / `.ts`)**: Generates typescript declaration maps.
5. **Compilation & Linking**:
   * Native target: Calls `clang++` on all sources and flat C++ bridge, linking them via `mold`.
   * Web target: Compiles C++ to WebAssembly object files, links them via `wasm-ld`, and bundles them into the final `.ts` / `.wasm` output files.

## Modularity & Separation of Concerns

Sew is structured as a library and a CLI front-end:
* **`libSewLib.a`**: Encapsulates language parsers, dependency graphs, preprocessors, code generation, and the compilation pipeline logic. It is completely platform-independent and can be integrated into other software or compilers.
* **`sew` (CLI)**: Handles system-level environment queries, command-line arguments parsing, console outputs, and temporary directory management.
