#pragma once
#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <Xi/Func.hpp>

namespace Sew { namespace Reflect {

using namespace Collection;

enum class TypeKind {
    Int,
    Float,
    String,
    Pointer,
    Custom
};

struct FieldDescriptor {
    String name;
    TypeKind kind;
    usz offset;
    usz size;
    String customTypeName;
};

struct MethodDescriptor {
    String name;
    String returnType;
    Array<String> paramTypes;
    void* functionPtr; // Type-erased original method pointer
};

struct StructDescriptor {
    String name;
    usz size;
    Array<String> parentClasses;
    Array<FieldDescriptor> fields;
    Array<MethodDescriptor> methods;
    Xi::Func<void*()> factory;
    Xi::Func<void(void*)> destroy;
};

}} // namespace Sew::Reflect
