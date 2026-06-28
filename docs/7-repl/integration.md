# Integration & Native Libraries

The Sew REPL can compile bindings for large native C++ engines and run them at native speeds by linking directly against precompiled static libraries (like `.a` archive files). This completely bypasses compiling the external library from source during REPL preparation, resulting in instant startup.

## Running xylem database from JS REPL

To run the Xylem database engine inside the Sew JS REPL, specify the public database header (`Xylem.hpp`) and the precompiled static library (`libXylem.a`):

```bash
./build/sew /home/xi/Repo/xylem/include/Xylem/Xylem.hpp /home/xi/Repo/xylem/build/libXylem.a --stdin js
```

All classes in the library are now bound to QuickJS. You can run database commands interactively or pipe a test script:

```javascript
// test_xylem.js
const db = new XylemEngine();
db.config.deviceSize = 10 * 1024 * 1024;
db.config.blockSize = 4096;

db.format();
db.mount();

db.tee("/hello.txt", "Hello, Xylem from Sew REPL!", 0, 0);
const res = db.cat("/hello.txt", 0, 0);
console.log("Read back:", res.getRowsJson());
```

Evaluating this script takes **under 800 milliseconds** total (including compiling the bindings, dynamic loading, and executing database operations on disk/memory):

```bash
./build/sew /home/xi/Repo/xylem/include/Xylem/Xylem.hpp /home/xi/Repo/xylem/build/libXylem.a --stdin js < test_xylem.js
```

## Compilation and Linker Optimization

When the REPL is run, Sew optimizes the pipeline using several flags:
1. **`-export-dynamic` / `-Wl,--whole-archive`**: Used when linking the host `sew` binary to ensure reflection registers and global registries are exported dynamically and visible to `dlopen`'d modules.
2. **`SEW_NO_SIBLINGS=1`**: When `isRepl` is enabled, Sew disables auto-discovery and compilation of sibling source files for external headers, compiling only the bridge and binding code.
3. **`-fPIC`**: All linked static libraries (like `libXylem.a`) must be compiled with Position Independent Code enabled. In CMake:
   ```cmake
   set(CMAKE_POSITION_INDEPENDENT_CODE ON)
   ```
