# Native Compilation

Sew compiles C++ source files directly to native AMD64 architectures.

## CLI Options

Compile a native executable:

```bash
sew main.cpp -t amd -o build/my_executable
```

Compile a native shared library:

```bash
sew module.cpp -t amd -o build/my_library.so
```

## Compilation Process

1. **Preprocessing**: Resolves C++ macro expansions and file includes.
2. **Dependency Mapping**: Constructs the dependency tree.
3. **Cache Lookup**: Verifies if object files exist inside the global cache (`~/.cache/sew/cache/`).
4. **Compilation**: Invokes `clang++ -c` to compile C++ source files to object `.o` files.
5. **Linking**: Links all object files using `clang++ -fuse-ld=mold` where available.
