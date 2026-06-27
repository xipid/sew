#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>
#include <cstring>
#include <cstdio>

namespace Sew { namespace Reflect {

GlobalRegistry global;

Reflection::Reflection() : _isPrototype(true) {
    _dynamicProps = new Map<String, DynamicProp>();
}

Reflection::Reflection(void* inst, const StructDescriptor* desc)
    : _instance(inst), _desc(desc), _isPrototype(false) {}

Reflection::Reflection(void* inst, const String& typeName)
    : _instance(inst), _desc(ReflectionRegistry::getStruct(typeName)), _isPrototype(false) {}

Reflection::~Reflection() {
    if (_dynamicProps) {
        delete _dynamicProps;
    }
}

Reflection::Reflection(const Reflection& o) {
    _instance = o._instance;
    _desc = o._desc;
    _isPrototype = o._isPrototype;
    if (o._dynamicProps) {
        _dynamicProps = new Map<String, DynamicProp>(*o._dynamicProps);
    } else {
        _dynamicProps = nullptr;
    }
}

Reflection& Reflection::operator=(const Reflection& o) {
    if (this != &o) {
        _instance = o._instance;
        _desc = o._desc;
        _isPrototype = o._isPrototype;
        if (_dynamicProps) delete _dynamicProps;
        if (o._dynamicProps) {
            _dynamicProps = new Map<String, DynamicProp>(*o._dynamicProps);
        } else {
            _dynamicProps = nullptr;
        }
    }
    return *this;
}

String Reflection::type(const String& name) const {
    if (_isPrototype) {
        if (_dynamicProps) {
            const DynamicProp* prop = _dynamicProps->get(name);
            if (prop) return prop->typeName;
        }
        return "Unknown";
    }

    if (_desc) {
        const StructDescriptor* latestDesc = ReflectionRegistry::getStruct(_desc->name);
        if (!latestDesc) latestDesc = _desc;
        for (usz i = 0; i < latestDesc->fields.size(); ++i) {
            const auto& f = latestDesc->fields[i];
            if (f.name == name) {
                switch (f.kind) {
                    case TypeKind::Int: return "int";
                    case TypeKind::Float: return "float";
                    case TypeKind::String: return "String";
                    case TypeKind::Pointer: return f.customTypeName + "*";
                    case TypeKind::Custom: return f.customTypeName;
                }
            }
        }
    }
    return "Unknown";
}

// ptr() — returns the reflected instance pointer itself (after redirect resolution).
// Corresponds to reflect.md: reflect(rf.ptr("property")) where ptr("property") on a
// pointer field gives the contained address, and ptr() with no args gives the
// struct base address tracked by this Reflection.
void* Reflection::ptr() const {
    if (_isPrototype) return nullptr;
    if (!_instance) return nullptr;
    return (void*)ReflectionRegistry::resolvePointer((usz)_instance);
}

void* Reflection::ptr(const String& name) const {
    if (_isPrototype) {
        if (_dynamicProps) {
            DynamicProp* prop = const_cast<Map<String, DynamicProp>*>(_dynamicProps)->get(name);
            if (prop) return prop->data.data();
        }
        return nullptr;
    }

    if (_desc && _instance) {
        const StructDescriptor* latestDesc = ReflectionRegistry::getStruct(_desc->name);
        if (!latestDesc) latestDesc = _desc;
        void* resolvedInst = (void*)ReflectionRegistry::resolvePointer((usz)_instance);

        for (usz i = 0; i < latestDesc->fields.size(); ++i) {
            const auto& f = latestDesc->fields[i];
            if (f.name == name) {
                return (char*)resolvedInst + f.offset;
            }
        }
    }
    return nullptr;
}

Reflection Reflection::reflect(const String& name) const {
    void* p = ptr(name);
    if (!p) return Reflection();

    String t = type(name);
    if (t.endsWith("*")) {
        t = t.substring(0, t.length() - 1);
        if (p) {
            p = *(void**)p;
        }
    }
    // Automatically register the sub-instance at any depth so it is tracked for hot-reloading
    if (p && t != "Unknown" && t != "int" && t != "float" && t != "String" && t != "bytes") {
        ReflectionRegistry::registerInstance(t, p);
    }
    return Reflection(p, t);
}

Array<String> Reflection::list() const {
    Array<String> res;
    if (_isPrototype) {
        if (_dynamicProps) {
            for (auto& kv : *_dynamicProps) {
                res.push(kv.key + ": " + kv.value.typeName);
            }
        }
    } else if (_desc) {
        const StructDescriptor* latestDesc = ReflectionRegistry::getStruct(_desc->name);
        if (!latestDesc) latestDesc = _desc;
        for (usz i = 0; i < latestDesc->fields.size(); ++i) {
            const auto& f = latestDesc->fields[i];
            String typeStr;
            switch (f.kind) {
                case TypeKind::Int: typeStr = "int"; break;
                case TypeKind::Float: typeStr = "float"; break;
                case TypeKind::String: typeStr = "String"; break;
                case TypeKind::Pointer: typeStr = f.customTypeName + "*"; break;
                case TypeKind::Custom: typeStr = f.customTypeName; break;
            }
            res.push(f.name + ": " + typeStr);
        }
    }
    return res;
}

void Reflection::reset(const String& name) {
    if (_isPrototype) {
        if (_dynamicProps) {
            _dynamicProps->remove(name);
        }
        return;
    }

    if (_desc && _instance) {
        for (usz i = 0; i < _desc->fields.size(); ++i) {
            const auto& f = _desc->fields[i];
            if (f.name == name) {
                std::memset((char*)_instance + f.offset, 0, f.size);
                return;
            }
        }
    }
}

void Reflection::set(const String& name, const void* bytes, usz length) {
    if (_isPrototype) {
        if (_dynamicProps) {
            DynamicProp prop;
            prop.name = name;
            prop.typeName = "bytes";
            for (usz i = 0; i < length; ++i) {
                prop.data.push(((const u8*)bytes)[i]);
            }
            _dynamicProps->set(name, prop);
        }
        return;
    }

    if (_desc && _instance) {
        const StructDescriptor* latestDesc = ReflectionRegistry::getStruct(_desc->name);
        if (!latestDesc) latestDesc = _desc;
        void* resolvedInst = (void*)ReflectionRegistry::resolvePointer((usz)_instance);

        for (usz i = 0; i < latestDesc->fields.size(); ++i) {
            const auto& f = latestDesc->fields[i];
            if (f.name == name) {
                usz copyLen = (length < f.size) ? length : f.size;
                std::memcpy((char*)resolvedInst + f.offset, bytes, copyLen);
                return;
            }
        }
    }
}

void* Reflection::originalMethod(const String& name) const {
    if (_desc) {
        const StructDescriptor* latestDesc = ReflectionRegistry::getStruct(_desc->name);
        if (!latestDesc) latestDesc = _desc;

        // Always return the original (unoverridden) function pointer.
        // Use ReflectionRegistry::getOverride() directly if you want override access.
        for (usz i = 0; i < latestDesc->methods.size(); ++i) {
            if (latestDesc->methods[i].name == name) {
                return latestDesc->methods[i].functionPtr;
            }
        }
    }
    return nullptr;
}

Map<String, StructDescriptor>& ReflectionRegistry::structs() {
    static Map<String, StructDescriptor> s_structs;
    return s_structs;
}

Map<String, StructDescriptor>& ReflectionRegistry::oldStructs() {
    static Map<String, StructDescriptor> s_oldStructs;
    return s_oldStructs;
}

Map<usz, String>& ReflectionRegistry::activeInstances() {
    static Map<usz, String> s_instances;
    return s_instances;
}

Map<usz, usz>& ReflectionRegistry::redirectionTable() {
    static Map<usz, usz> s_redirections;
    return s_redirections;
}

Map<usz, Map<String, Xi::Func<void*(void*, void*)>>>& ReflectionRegistry::methodOverrides() {
    static Map<usz, Map<String, Xi::Func<void*(void*, void*)>>> s_overrides;
    return s_overrides;
}

Map<String, usz>& ReflectionRegistry::globalVariables() {
    static Map<String, usz> s_vars;
    return s_vars;
}

void ReflectionRegistry::registerVariable(const String& name, void* ptr) {
    usz addr = (usz)ptr;
    usz* oldAddr = globalVariables().get(name);
    if (oldAddr) {
        redirect(addr, *oldAddr);
    } else {
        globalVariables().set(name, addr);
    }
}

void ReflectionRegistry::registerInstance(const String& typeName, void* ptr) {
    if (!ptr) return;
    usz addr = (usz)ptr;
    usz resolved = resolvePointer(addr);
    
    if (activeInstances().has(resolved)) return;
    activeInstances().set(resolved, typeName);
    
    // Traverse fields recursively to register nested objects at all depths
    const StructDescriptor* desc = getStruct(typeName);
    if (desc) {
        for (usz i = 0; i < desc->fields.size(); ++i) {
            const FieldDescriptor& f = desc->fields[i];
            const StructDescriptor* fieldDesc = getStruct(f.customTypeName);
            if (fieldDesc) {
                if (f.kind == TypeKind::Pointer) {
                    void* subPtr = *(void**)((char*)resolved + f.offset);
                    if (subPtr) {
                        registerInstance(fieldDesc->name, subPtr);
                    }
                } else if (f.kind == TypeKind::Custom) {
                    void* subPtr = (void*)((char*)resolved + f.offset);
                    registerInstance(fieldDesc->name, subPtr);
                }
            }
        }
    }
}

void ReflectionRegistry::deregisterInstance(void* ptr) {
    activeInstances().remove((usz)ptr);
}

Map<usz, bool>& ReflectionRegistry::heapInstances() {
    static Map<usz, bool> s_heap;
    return s_heap;
}

void* ReflectionRegistry::create(const String& typeName) {
    const StructDescriptor* desc = getStruct(typeName);
    if (!desc || !desc->factory) return nullptr;
    void* ptr = desc->factory();
    registerInstance(typeName, ptr);
    heapInstances().set((usz)ptr, true);
    return ptr;
}

void ReflectionRegistry::destroy(void* ptr) {
    if (!ptr) return;
    usz addr = (usz)ptr;
    usz resolved = resolvePointer(addr);
    String* typeName = activeInstances().get(resolved);
    if (typeName) {
        const StructDescriptor* desc = getStruct(*typeName);
        bool* isHeap = heapInstances().get(resolved);
        if (isHeap && *isHeap) {
            if (desc && desc->destroy) {
                desc->destroy((void*)resolved);
            }
            heapInstances().remove(resolved);
        }
        activeInstances().remove(resolved);
    }
    redirectionTable().remove(addr);
}

void ReflectionRegistry::hotReload() {
    // 1. Gather active instances into arrays to avoid iterator invalidation
    Array<usz> instanceAddresses;
    Array<String> instanceTypes;
    for (auto it = activeInstances().begin(); it != activeInstances().end(); ++it) {
        instanceAddresses.push(it->key);
        instanceTypes.push(it->value);
    }

    for (usz idx = 0; idx < instanceAddresses.size(); ++idx) {
        usz oldAddr = instanceAddresses[idx];
        String typeName = instanceTypes[idx];

        // Retrieve descriptors
        const StructDescriptor* oldDesc = getOldStruct(typeName);
        const StructDescriptor* newDesc = getStruct(typeName);

        if (oldDesc && newDesc && newDesc->factory) {
            // Allocate new instance (also registers newPtr in activeInstances)
            void* newPtr = create(typeName);
            if (!newPtr) continue;

            // Migrate fields: copy matching fields from old layout to new layout
            for (usz fi = 0; fi < newDesc->fields.size(); ++fi) {
                const auto& newField = newDesc->fields[fi];
                for (usz fj = 0; fj < oldDesc->fields.size(); ++fj) {
                    const auto& oldField = oldDesc->fields[fj];
                    if (newField.name == oldField.name &&
                        newField.kind == oldField.kind &&
                        newField.customTypeName == oldField.customTypeName) {
                        if (newField.kind == TypeKind::Pointer) {
                            usz oldVal = *(usz*)((char*)oldAddr + oldField.offset);
                            usz newVal = resolvePointer(oldVal);
                            *(usz*)((char*)newPtr + newField.offset) = newVal;
                        } else {
                            usz copySize = (newField.size < oldField.size) ? newField.size : oldField.size;
                            std::memcpy((char*)newPtr + newField.offset,
                                        (char*)oldAddr + oldField.offset, copySize);
                        }
                        break;
                    }
                }
            }

            // Migrate any per-instance method overrides to the new address
            Map<String, Xi::Func<void*(void*, void*)>>* oldOverrides =
                methodOverrides().get(oldAddr);
            if (oldOverrides) {
                Map<String, Xi::Func<void*(void*, void*)>> movedOverrides = *oldOverrides;
                methodOverrides().set((usz)newPtr, movedOverrides);
                methodOverrides().remove(oldAddr);
            }

            // Redirect old pointer to new pointer in the global redirection table
            redirect(oldAddr, (usz)newPtr);

            // Remove old instance registration (it is now invalid)
            activeInstances().remove(oldAddr);

            // Destroy old instance using the old (V1) destructor ONLY if heap-allocated
            bool* isHeap = heapInstances().get(oldAddr);
            if (isHeap && *isHeap) {
                if (oldDesc->destroy) {
                    oldDesc->destroy((void*)oldAddr);
                }
                heapInstances().remove(oldAddr);
            }
        }
    }
}

}} // namespace Sew::Reflect
