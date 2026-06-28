# TypeScript Generation

Sew compiles C++ source files directly into WebAssembly binaries while automatically generating a fully typed TypeScript class wrapper interface.

## Targeting Web Environments

To build your project, run the compilation targeting the JS engine and specify `.ts` in the output path:

```bash
sew index.js -t js -o build/sew.ts
```

This compiles your code and outputs exactly two files:
1. **`build/sew.ts`**: The unified TypeScript library containing class definitions, field mappings, method dispatches, and JSDoc comments.
2. **`build/sew.wasm`**: The compiled WebAssembly binary containing the C++ code.

## Bundling C++ and JS

When targeting the web environment, Sew performs a transparent bundling pass:

1. **Dependency Analysis**: Discovers C++ headers imported inside JS/TS files.
2. **WebAssembly Compilation**: Compiles the C++ source files to WASM bytecode.
3. **Bridge Generation**:
   * Generates a flat C WebAssembly wrapper interface (`sew_bridge.cpp`) and compiles it.
   * Generates the JS glue bindings wrapper containing WebAssembly memory views, pointers, and class declarations.
4. **Types and Documentation Preservation**:
   * Reads docstrings on your C++ class methods and variables.
   * Maps C++ structures to TypeScript types.
   * Outputs the complete TypeScript interface directly into the generated `.ts` file, with docstrings preserved.

This means you can consume C++ structures, instantiate classes, access fields, and invoke methods in TypeScript with complete type safety and inline JSDoc documentation, without writing any Embind or WASI loader boilerplate.
