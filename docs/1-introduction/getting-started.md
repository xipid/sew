# Getting Started

This guide walks through building Sew from source and executing your first compilation targets.

## Building Sew from Source

To build the Sew library and compiler binary, configure the project using CMake:

```bash

./build.sh

# Or

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```

On successful compilation, the output artifacts are generated:
* `build/sew`: The CLI executable wrapper.
* `build/libSewLib.a`: The static Sew library.

## Commands and Targets

### Native AMD64 Compilation

Compile a C++ source file into a native executable:

```bash
./sew main.cpp -t amd -o build/my_executable
```

To compile into a dynamically linkable shared library for hot-reloading:

```bash
./sew module.cpp -t amd -o build/my_library.so
```

### TypeScript/WebAssembly Bundle Generation

To compile a JavaScript or TypeScript file that references C++ headers directly into a WebAssembly library, compile directly to a `.ts` file:

```bash
./sew index.js -t js -o build/module.ts
```

This command runs the parser and linker, generating two output files:
1. `build/module.ts`: The unified TypeScript glue file containing high-performance memory views, classes, method definitions, and documentation comments.
2. `build/module.wasm`: The compiled WebAssembly binary.
