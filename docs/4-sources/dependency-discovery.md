# Dependency Discovery

Sew parses source files to discover dependencies and build a topological build plan.

## AST Import Parsing

The parser scans JavaScript and TypeScript files using a token scanner:

* **Static ES6 Imports**: Resolves imports such as `import { XylemEngine } from "Xylem.hpp"`.
* **CommonJS Requires**: Resolves requires such as `const fs = require('fs')`.
* **C++ Headers**: If a JavaScript/TypeScript file imports a C++ header file directly:
  * Sew recognizes that it needs to compile that C++ library to WebAssembly.
  * Adds the C++ header file and all its `#include` dependencies to the compilation graph.
  * Generates flat C bindings and WebAssembly exports automatically.

This automated discovery creates a completely transparent developer experience (DX)—just write a standard import statement referencing your C++ code, and Sew handles the rest!
