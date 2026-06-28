# API Reference

The reflection engine provides APIs to introspect class structures, read/write fields, get descriptors, and override methods.

## Typings and Descriptors

Reflected struct descriptors and types are stored in the global registry:

```cpp
#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>

using namespace Sew::Reflect;
```

### Initializing Reflection

Instantiate a `Reflection` object by passing an instance reference:

```cpp
Player player;
Reflection rf = reflect(player);
```

To reflect an instance address or raw pointer:

```cpp
Reflection rf = reflect(&player);
```

To reflect a nested field or dynamic pointer field:

```cpp
Reflection rfItem = reflect(rf.ptr("item"));
```

## Introspection Methods

* **Base Address Introspection**:
  `void* ptr() const;`
  Returns the base address of the tracked instance, following any redirection offsets resolved from hot-reloads.
  ```cpp
  void* addr = rf.ptr(); // Returns address of player
  ```

* **Field Address Resolution**:
  `void* ptr(const String& name) const;`
  Returns a pointer to the field within the tracked structure.
  ```cpp
  int* scorePtr = (int*)rf.ptr("score");
  ```

* **Field Type Queries**:
  `String type(const String& name) const;`
  Returns the typename string of the target field.
  ```cpp
  String t = rf.type("score"); // Returns "int"
  ```

* **Listing Fields**:
  `Array<String> list() const;`
  Returns a list of all reflected field names.
  ```cpp
  Array<String> fields = rf.list();
  ```

* **Sub-reflected Fields**:
  `Reflection reflect(const String& name) const;`
  Returns a new `Reflection` wrapper for the nested field.
  ```cpp
  Reflection rfItem = rf.reflect("item");
  ```

## Mutation Methods

* **Updating Field Data**:
  `void set(const String& name, const void* bytes, usz length);`
  Writes raw bytes directly to a field's offset.
  ```cpp
  int newScore = 150;
  rf.set("score", &newScore, sizeof(int));
  ```

## Method Dispatch and Overrides

* **Original Method Lookup**:
  `void* originalMethod(const String& name) const;`
  Returns the function pointer to the wrapper method dispatch function.
  ```cpp
  void* originalDmgFn = rf.originalMethod("takeDamage");
  ```

* **Method Overriding**:
  `void override(const String& name, Func<void*(void*, void*)> func);`
  Overwrites the method registration globally for this instance.
  ```cpp
  rf.override("takeDamage", [](void* self, void* args) -> void* {
      int amount = *(int*)((void**)args)[0];
      // Custom damage reduction logic...
      return nullptr;
  });
  ```
