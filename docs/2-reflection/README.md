# Reflection

Sew features an automated, static-reflection compiler pass that instruments C++ source files with reflection metadata. This metadata allows developers to introspect class layouts, query fields and methods, override virtual or non-virtual methods, and hot-swap memory instances dynamically at runtime.

## Core Mechanisms

1. **AST Analysis**: The Sew preprocessor and parser identify class structures, fields, and method signatures during compilation.
2. **Metadata Code Generation**: Sew generates `StructDescriptor` and `MethodDescriptor` structures, capturing offsets, types, and wrapper dispatch function pointers.
3. **Automatic Registration**: Generates global static initializers that register all parsed descriptors with the `ReflectionRegistry` when a library or module is loaded.
4. **Indirection Layer**: Modifies pointers to go through `resolveCast`, allowing the registry to swap underlying pointer targets seamlessly.
