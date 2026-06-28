# Hot Reloading

Sew provides an automated runtime memory migration and hot-swizzling framework for dynamically loaded shared libraries (`.so` modules).

## Pointer Indirection and Cast Rewriting

To allow pointers to be redirected dynamically, the Sew compiler automatically rewrites C-style casts and `static_cast`/`reinterpret_cast`/`const_cast` inside your source files:

* `(Player*)(addr)` -> `ReflectionRegistry::resolveCast<Player*>(addr)`
* `static_cast<Player*>(addr)` -> `ReflectionRegistry::resolveCast<Player*>(addr)`

When a pointer is resolved via `resolveCast`, the registry checks the global `redirectionTable`. If a redirection exists for that address, it follows the redirect chain to return the newest version of the instance.

```cpp
usz initialAddr = (usz)&player;
Player* resolved = ReflectionRegistry::resolveCast<Player*>(initialAddr);
```

## Variable Redirection API

When a new module version is loaded, call `redirect` to register a memory pointer redirection:

```cpp
// Map the old memory location to the new memory location
ReflectionRegistry::redirect(oldAddr, newAddr);
```

Any subsequent call to `resolveCast` or `rf.ptr()` will automatically return the new address:

```cpp
// Returns the redirected address
Player* current = ReflectionRegistry::resolveCast<Player*>(oldAddr);
```

## Layout Migration and HotReload Magic

If you register a structure descriptor using the `global` registry proxy, loading a new module version automatically triggers structural data migration (`ReflectionRegistry::hotReload()`):

```cpp
// Register structure descriptor of V1
global["Player"] = captureStruct(Player);

// When V2 module is loaded, registering it by name triggers automatic hot-reload:
global["Player"] = "Player";
```

### The `hotReload()` Pipeline

When `hotReload()` is invoked, Sew runs the following steps:
1. **Gathers active instances**: Collects all registered instance addresses and types.
2. **Retrieves Old/New Descriptors**: Obtains the older structural layout metadata and the newer layout metadata.
3. **Allocates New Instances**: Invokes the newer class factory to instantiate the new memory layout.
4. **Field Migration**: Copies matching fields from the old layout to the new layout using field names, kinds, and types.
   * If a field's position or order has changed, Sew resolves the new offset.
   * New fields receive their default values.
   * Redirection pointers are automatically updated.
5. **Method Overrides**: Transfers active per-instance method overrides to the new instance addresses.
6. **Redirection Registry**: Updates the redirection table to map the old instance address to the new instance address.
7. **Destruction**: Invokes the destructor on the old instance if it was heap-allocated.
