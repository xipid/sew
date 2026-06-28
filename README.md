# Sew

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![DOI](https://zenodo.org/badge/1258403692.svg)](https://doi.org/10.5281/zenodo.20977334)
[![Docs](https://img.shields.io/badge/docs-gitbook-blue?style=flat-square)](https://sew.gitbook.io/sew/)
[![Discord](https://img.shields.io/badge/discord-join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/s7Rg4DHuej)


**Sew** is a content-addressable build wrapper, dependency resolver, preprocessor, and reflection registry for C++ and WebAssembly. It replaces complex multi-tool pipelines (like CMake, Ninja, and CCache) with a single, intelligent compilation engine.

---

## Key Features

* **Direct Compiler Wrapping**: Calls Clang directly without generating intermediate makefiles or build descriptions.
* **Content-Addressable Hashing**: Cached objects are stored globally in `~/.cache/sew/cache/` using content hashes of both input files and precise compiler arguments.
* **Dual Target Architecture**: 
  * **AMD64**: Compiles directly to native executables and shared libraries.
  * **WebAssembly**: Compiles to WASM binaries and generates type-safe JS/TS wrapper bindings.
* **Dynamic Hot-Reloading**: Features a runtime reflection registry allowing C++ shared libraries to be hot-reloaded dynamically, automatically migrating active instances to new memory layouts.

---

## Installation & Setup

Build the project from source:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja


# Or directly:
./build.sh
```

The compiled `sew` binary will be available in the `build` directory.

---

## Quickstart

### Native AMD64 Build

Compile C++ files directly to a native binary:

```bash
sew main.cpp -t amd -o build/my_executable
```

### WebAssembly & TypeScript Generation

Compile C++ code to a WebAssembly module with generated TypeScript declarations and JavaScript wrappers:

```bash
sew index.js -t wasm -o build/sew.wasm
```

---

## Reflection API

```cpp
#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>

using namespace Sew::Reflect;

Player player;
Reflection rf = reflect(player);

// Introspect fields
int* health = (int*)rf.ptr("health");

// Modify fields via introspection
int newVal = 50;
rf.set("health", &newVal, sizeof(int));
```

---

## Citation

If you use Sew in your research, please cite the project:

```bibtex
@software{sew2026,
  author = {Abderrahmene Merzoug},
  title = {Sew: A Content-Addressable Build Wrapper, Dependency Resolver, and Reflection Registry for C++ and WebAssembly},
  url = {https://github.com/xipid/sew},
  version = {1.0.0},
  year = {2026}
}
```

---

## License

Sew is licensed under the Apache License 2.0. See the `LICENSE` file for details.
