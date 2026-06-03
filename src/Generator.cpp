#include <Sew/Generator.hpp>
#include <Collection/Map.hpp>

namespace Sew {

static String replaceColons(const String& s) {
    String res;
    for (usz i = 0; i < s.length(); ++i) {
        if (i + 1 < s.length() && s.data()[i] == ':' && s.data()[i+1] == ':') {
            res += "_";
            i++;
        } else {
            res += (char)s.data()[i];
        }
    }
    return res;
}

static String getJsName(const String& fullName) {
    long long lastColon = -1;
    for (usz i = 0; i + 1 < fullName.length(); ++i) {
        if (fullName.data()[i] == ':' && fullName.data()[i+1] == ':') {
            lastColon = (long long)i;
        }
    }
    if (lastColon >= 0) {
        return fullName.substring((usz)lastColon + 2);
    }
    return fullName;
}

static bool isClassType(const String& typeStr, const Array<ParsedClass>& classes, String& outClassName) {
    if (typeStr.indexOf('<') >= 0 || typeStr.indexOf('>') >= 0) {
        return false;
    }
    for (usz i = 0; i < classes.size(); ++i) {
        String fullName = classes[i].name;
        String shortName = getJsName(fullName);

        const String* names[] = { &fullName, &shortName };
        for (int n = 0; n < 2; ++n) {
            const String& name = *names[n];
            long long pos = typeStr.indexOf(name);
            if (pos >= 0) {
                bool left = (pos == 0 || !( (typeStr.data()[pos-1] >= 'a' && typeStr.data()[pos-1] <= 'z') || (typeStr.data()[pos-1] >= 'A' && typeStr.data()[pos-1] <= 'Z') || (typeStr.data()[pos-1] >= '0' && typeStr.data()[pos-1] <= '9') || typeStr.data()[pos-1] == '_' || typeStr.data()[pos-1] == ':' ));
                usz endPos = (usz)pos + name.length();
                bool right = (endPos == typeStr.length() || !( (typeStr.data()[endPos] >= 'a' && typeStr.data()[endPos] <= 'z') || (typeStr.data()[endPos] >= 'A' && typeStr.data()[endPos] <= 'Z') || (typeStr.data()[endPos] >= '0' && typeStr.data()[endPos] <= '9') || typeStr.data()[endPos] == '_' || typeStr.data()[endPos] == ':' ));
                if (left && right) {
                    outClassName = fullName;
                    return true;
                }
            }
        }
    }
    return false;
}

static String getBridgeType(const String& typeStr, const Array<ParsedClass>& classes) {
    String className;
    if (isClassType(typeStr, classes, className)) {
        return className + "*";
    }
    return typeStr;
}

static String getPassValue(const ParsedParam& param, const Array<ParsedClass>& classes) {
    String className;
    if (isClassType(param.type, classes, className)) {
        if (param.type.indexOf('*') < 0) {
            return "*" + param.name;
        }
    }
    return param.name;
}

static bool isStringAndLengthPattern(const ParsedParam& p1, const ParsedParam& p2) {
    bool p1Str = (p1.type == "const char *" || p1.type == "const char*" || p1.type == "char *" || p1.type == "char*" ||
                  p1.type == "const u8 *" || p1.type == "const u8*" || p1.type == "u8 *" || p1.type == "u8*");
    bool p2Len = (p2.type == "usz" || p2.type == "size_t" || p2.type == "int" || p2.type == "unsigned" || p2.type == "u32" || p2.type == "u8");
    if (p1Str && p2Len) {
        String name = p2.name.toLowerCase();
        return (name == "len" || name == "length" || name == "c" || name == "count" || name == "l");
    }
    return false;
}

static String getJsType(const String& cppType, const Array<ParsedClass>& classes) {
    if (cppType == "void") return "void";
    if (cppType == "bool") return "boolean";
    if (cppType == "const char*" || cppType == "const char *" || cppType == "char*" || cppType == "char *") return "string";
    String className;
    if (isClassType(cppType, classes, className)) {
        return getJsName(className);
    }
    return "number";
}

String BindingGenerator::generateCppBridge(const Array<ParsedClass>& classes,
                                           const Array<ParsedFunction>& functions,
                                           const Array<String>& namespaces,
                                           const Array<String>& headerIncludePaths) {
    String out;
    out += "// Auto-generated C++ bridge by Sew\n\n";
    for (usz i = 0; i < headerIncludePaths.size(); ++i) {
        out += "#include \"" + headerIncludePaths[i] + "\"\n";
    }
    out += "#include <cstdlib>\n\n";
    out += "using namespace Collection;\n";
    out += "using namespace Xi;\n";
    for (usz i = 0; i < namespaces.size(); ++i) {
        if (namespaces[i] != "Collection" && namespaces[i] != "Xi") {
            out += "using namespace " + namespaces[i] + ";\n";
        }
    }
    out += "\n";
    out += "extern \"C\" {\n\n";

    // Allocator helpers
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* alloc_buf(int size) {\n";
    out += "    return malloc(size);\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void free_buf(void* ptr) {\n";
    out += "    free(ptr);\n";
    out += "}\n\n";

    // For each class
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String bridgeClsName = replaceColons(cls.name);

        // Constructors
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (m.isConstructor) {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                out += cls.name + "* export_" + bridgeClsName + "_new_" + String((long long)j) + "(";
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    out += getBridgeType(m.params[k].type, classes) + " " + m.params[k].name;
                }
                out += ") {\n";
                out += "    return new " + cls.name + "(";
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    out += getPassValue(m.params[k], classes);
                }
                out += ");\n";
                out += "}\n\n";
            }
        }

        // Destructor
        out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
        out += "void export_" + bridgeClsName + "_delete(" + cls.name + "* self) {\n";
        out += "    delete self;\n";
        out += "}\n\n";

        // Methods
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                String retBridgeType = getBridgeType(m.returnType, classes);
                out += retBridgeType + " export_" + bridgeClsName + "_" + m.name + "_" + String((long long)j) + "(";
                if (!m.isStatic) {
                    out += cls.name + "* self";
                    if (m.params.size() > 0) out += ", ";
                }
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    out += getBridgeType(m.params[k].type, classes) + " " + m.params[k].name;
                }
                out += ") {\n";
                out += "    ";

                if (m.returnType != "void") {
                    out += "return ";
                    String className;
                    if (isClassType(m.returnType, classes, className)) {
                        if (m.returnType.indexOf('*') >= 0) {
                            // Returns pointer
                        } else if (m.returnType.indexOf('&') >= 0) {
                            // Returns reference
                            out += "&";
                        } else {
                            // Returns by value, copy to heap using new
                            out += "new " + className + "(";
                        }
                    }
                }

                if (m.isStatic) {
                    out += cls.name + "::" + m.name + "(";
                } else {
                    out += "self->" + m.name + "(";
                }

                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    out += getPassValue(m.params[k], classes);
                }
                out += ")";

                String className;
                if (m.returnType != "void" && isClassType(m.returnType, classes, className) && m.returnType.indexOf('*') < 0 && m.returnType.indexOf('&') < 0) {
                    out += ")";
                }
                out += ";\n";
                out += "}\n\n";
            }
        }

        // Fields (Getters / Setters)
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            String fBridgeType = getBridgeType(f.type, classes);

            // Getter
            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += fBridgeType + " export_" + bridgeClsName + "_get_" + f.name + "(" + cls.name + "* self) {\n";
            if (f.isStatic) {
                if (fBridgeType.endsWith("*") && f.type.indexOf('*') < 0) {
                    out += "    return &" + cls.name + "::" + f.name + ";\n";
                } else {
                    out += "    return " + cls.name + "::" + f.name + ";\n";
                }
            } else {
                if (fBridgeType.endsWith("*") && f.type.indexOf('*') < 0) {
                    out += "    return &self->" + f.name + ";\n";
                } else {
                    out += "    return self->" + f.name + ";\n";
                }
            }
            out += "}\n\n";

            // Setter
            if (!f.isConst) {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                out += "void export_" + bridgeClsName + "_set_" + f.name + "(" + cls.name + "* self, " + fBridgeType + " value) {\n";
                if (f.isStatic) {
                    if (fBridgeType.endsWith("*") && f.type.indexOf('*') < 0) {
                        out += "    " + cls.name + "::" + f.name + " = *value;\n";
                    } else {
                        out += "    " + cls.name + "::" + f.name + " = value;\n";
                    }
                } else {
                    if (fBridgeType.endsWith("*") && f.type.indexOf('*') < 0) {
                        out += "    self->" + f.name + " = *value;\n";
                    } else {
                        out += "    self->" + f.name + " = value;\n";
                    }
                }
                out += "}\n\n";
            }
        }
    }

    // Global Functions
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
        String retBridgeType = getBridgeType(fn.returnType, classes);
        out += retBridgeType + " export_" + replaceColons(fn.name) + "(";
        for (usz j = 0; j < fn.params.size(); ++j) {
            if (j > 0) out += ", ";
            out += getBridgeType(fn.params[j].type, classes) + " " + fn.params[j].name;
        }
        out += ") {\n";
        out += "    ";

        if (fn.returnType != "void") {
            out += "return ";
            String className;
            if (isClassType(fn.returnType, classes, className)) {
                if (fn.returnType.indexOf('*') >= 0) {
                    // Returns pointer
                } else if (fn.returnType.indexOf('&') >= 0) {
                    out += "&";
                } else {
                    out += "new " + className + "(";
                }
            }
        }

        out += fn.name + "(";
        for (usz j = 0; j < fn.params.size(); ++j) {
            if (j > 0) out += ", ";
            out += getPassValue(fn.params[j], classes);
        }
        out += ")";

        String className;
        if (fn.returnType != "void" && isClassType(fn.returnType, classes, className) && fn.returnType.indexOf('*') < 0 && fn.returnType.indexOf('&') < 0) {
            out += ")";
        }
        out += ";\n";
        out += "}\n\n";
    }

    out += "}\n";
    return out;
}

String BindingGenerator::generateTsGlue(const Array<ParsedClass>& classes,
                                        const Array<ParsedFunction>& functions,
                                        const String& wasmFileName) {
    String out;
    out += "// Auto-generated TypeScript bindings by Sew\n\n";
    out += "let wasmInstance: WebAssembly.Instance;\n";
    out += "let wasmMemory: WebAssembly.Memory;\n";
    out += "let exports: any;\n\n";
    out += "const INTERNAL = Symbol('internal');\n\n";

    // Finalization Registry
    out += "const registry = new FinalizationRegistry((info: { ptr: number, type: string }) => {\n";
    out += "  if (!exports) return;\n";
    for (usz i = 0; i < classes.size(); ++i) {
        out += "  if (info.type === '" + classes[i].name + "') exports.export_" + replaceColons(classes[i].name) + "_delete(info.ptr);\n";
    }
    out += "});\n\n";

    // Helpers
    out += "function writeString(str: string): number {\n";
    out += "  const encoder = new TextEncoder();\n";
    out += "  const bytes = encoder.encode(str);\n";
    out += "  const ptr = exports.alloc_buf(bytes.length + 1);\n";
    out += "  const view = new Uint8Array(exports.memory.buffer, ptr, bytes.length + 1);\n";
    out += "  view.set(bytes);\n";
    out += "  view[bytes.length] = 0;\n";
    out += "  return ptr;\n";
    out += "}\n\n";

    out += "function writeBuffer(buf: Uint8Array | string): { ptr: number, len: number } {\n";
    out += "  const bytes = typeof buf === 'string' ? new TextEncoder().encode(buf) : buf;\n";
    out += "  const ptr = exports.alloc_buf(bytes.length);\n";
    out += "  const view = new Uint8Array(exports.memory.buffer, ptr, bytes.length);\n";
    out += "  view.set(bytes);\n";
    out += "  return { ptr, len: bytes.length };\n";
    out += "}\n\n";

    out += "function readString(ptr: number): string {\n";
    out += "  const view = new Uint8Array(exports.memory.buffer, ptr);\n";
    out += "  let len = 0;\n";
    out += "  while (view[len] !== 0) len++;\n";
    out += "  const bytes = new Uint8Array(exports.memory.buffer, ptr, len);\n";
    out += "  return new TextDecoder().decode(bytes);\n";
    out += "}\n\n";

    // Init
    out += "export async function init(wasmUrl: string): Promise<void> {\n";
    out += "  const response = await fetch(wasmUrl);\n";
    out += "  const buffer = await response.arrayBuffer();\n";
    out += "  wasmMemory = new WebAssembly.Memory({ initial: 256 });\n";
    out += "  const imports = {\n";
    out += "    env: { memory: wasmMemory },\n";
    out += "    wasi_snapshot_preview1: {\n";
    out += "      proc_exit: (code: number) => { throw new Error(`exit: ${code}`); },\n";
    out += "      fd_write: () => 0,\n";
    out += "      fd_seek: () => 0,\n";
    out += "      fd_close: () => 0,\n";
    out += "      clock_time_get: (id: number, precision: bigint, timeOutPtr: number) => {\n";
    out += "        const now = BigInt(Date.now()) * 1000000n;\n";
    out += "        const view = new DataView(wasmMemory.buffer);\n";
    out += "        view.setBigUint64(timeOutPtr, now, true);\n";
    out += "        return 0;\n";
    out += "      },\n";
    out += "    },\n";
    out += "  };\n";
    out += "  const { instance } = await WebAssembly.instantiate(buffer, imports);\n";
    out += "  wasmInstance = instance;\n";
    out += "  exports = instance.exports;\n";
    out += "}\n\n";

    // Exported helper
    out += "export function getExports(): WebAssembly.Exports {\n";
    out += "  return exports;\n";
    out += "}\n\n";

    // For each class
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String jsClsName = getJsName(cls.name);
        String bridgeClsName = replaceColons(cls.name);

        if (cls.docComment.length() > 0) {
            out += "/**\n";
            Array<String> docLines = cls.docComment.split("\n");
            for (usz j = 0; j < docLines.size(); ++j) {
                out += " * " + docLines[j] + "\n";
            }
            out += " */\n";
        }

        out += "export class " + jsClsName + " {\n";
        out += "  ptr: number;\n\n";

        // Constructor
        out += "  constructor(...args: any[]) {\n";
        out += "    if (args.length === 2 && args[1] === INTERNAL) {\n";
        out += "      this.ptr = args[0];\n";
        out += "    } else {\n";

        // Constructor overload dispatch
        bool hasOverloads = false;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (m.isConstructor) {
                hasOverloads = true;
                out += "      if (args.length === " + String((long long)m.params.size());

                // Perform heuristic type checking on JS arguments
                for (usz k = 0; k < m.params.size(); ++k) {
                    out += " && ";
                    String jsT = getJsType(m.params[k].type, classes);
                    if (jsT == "string") {
                        out += "typeof args[" + String((long long)k) + "] === 'string'";
                    } else if (jsT == "boolean") {
                        out += "typeof args[" + String((long long)k) + "] === 'boolean'";
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                        // Class type
                        out += "args[" + String((long long)k) + "] instanceof " + jsT;
                    } else {
                        // Number
                        out += "typeof args[" + String((long long)k) + "] === 'number'";
                    }
                }
                out += ") {\n";

                // Marshalling string literals
                Array<String> toFree;
                for (usz k = 0; k < m.params.size(); ++k) {
                    String jsT = getJsType(m.params[k].type, classes);
                    if (jsT == "string") {
                        out += "        const p" + String((long long)k) + " = writeString(args[" + String((long long)k) + "]);\n";
                        toFree.push("p" + String((long long)k));
                    }
                }

                out += "        this.ptr = exports.export_" + bridgeClsName + "_new_" + String((long long)j) + "(";
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    String jsT = getJsType(m.params[k].type, classes);
                    if (jsT == "string") {
                        out += "p" + String((long long)k);
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                        out += "args[" + String((long long)k) + "].ptr";
                    } else {
                        out += "args[" + String((long long)k) + "]";
                    }
                }
                out += ");\n";

                for (usz k = 0; k < toFree.size(); ++k) {
                    out += "        exports.free_buf(" + toFree[k] + ");\n";
                }
                out += "        registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
                out += "        return;\n";
                out += "      }\n";
            }
        }

        if (hasOverloads) {
            out += "      throw new Error('No constructor overload matched given arguments');\n";
        } else {
            // Default parameterless new
            out += "      this.ptr = exports.export_" + bridgeClsName + "_new_0 ? exports.export_" + bridgeClsName + "_new_0() : 0;\n";
            out += "      registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
        }

        out += "    }\n";
        out += "    registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
        out += "  }\n\n";

        // Group methods by name to handle overloading
        Map<String, Array<usz>> overloadedMethods;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                Array<usz>* grp = overloadedMethods.get(m.name);
                if (grp) {
                    grp->push(j);
                } else {
                    Array<usz> newGrp;
                    newGrp.push(j);
                    overloadedMethods.set(m.name, newGrp);
                }
            }
        }

        // Generate methods
        for (auto &entry : overloadedMethods) {
            String mName = entry.key;
            const Array<usz>& overloads = entry.value;
            const ParsedMethod& firstMethod = cls.methods[overloads[0]];

            // TS JSDoc
            if (firstMethod.docComment.length() > 0) {
                out += "  /**\n";
                Array<String> docLines = firstMethod.docComment.split("\n");
                for (usz k = 0; k < docLines.size(); ++k) {
                    out += "   * " + docLines[k] + "\n";
                }
                out += "   */\n";
            }

            // Method Signature
            out += "  ";
            out += (firstMethod.isStatic ? "static " : "");
            out += mName + "(...args: any[]): any {\n";

            for (usz k = 0; k < overloads.size(); ++k) {
                usz methodIdx = overloads[k];
                const ParsedMethod& m = cls.methods[methodIdx];

                // Detect string and length patterns to match combined parameters
                // E.g. (const u8* buffer, usz l) -> single parameter string
                Array<int> paramMap; // index of arg mapping: -1: normal, or index of paired len
                Array<bool> isLenParam;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    paramMap.push(-1);
                    isLenParam.push(false);
                }

                for (usz pIdx = 0; pIdx + 1 < m.params.size(); ++pIdx) {
                    if (isStringAndLengthPattern(m.params[pIdx], m.params[pIdx + 1])) {
                        paramMap.data()[pIdx] = (int)(pIdx + 1);
                        isLenParam.data()[pIdx + 1] = true;
                    }
                }

                int jsArgsCount = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (!isLenParam[pIdx]) jsArgsCount++;
                }

                out += "    if (args.length === " + String((long long)jsArgsCount);

                int jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (isLenParam[pIdx]) continue;
                    out += " && ";
                    String jsT = getJsType(m.params[pIdx].type, classes);
                    if (paramMap[pIdx] >= 0 || jsT == "string") {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'string'";
                    } else if (jsT == "boolean") {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'boolean'";
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                        out += "args[" + String((long long)jsArgIdx) + "] instanceof " + jsT;
                    } else {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'number'";
                    }
                    jsArgIdx++;
                }
                out += ") {\n";

                // Marshalling string literals and string-and-length parameters
                Array<String> toFree;
                jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (isLenParam[pIdx]) continue;
                    if (paramMap[pIdx] >= 0) {
                        // String and length combined
                        out += "      const p" + String((long long)pIdx) + " = writeBuffer(args[" + String((long long)jsArgIdx) + "]);\n";
                        toFree.push("p" + String((long long)pIdx) + ".ptr");
                    } else {
                        String jsT = getJsType(m.params[pIdx].type, classes);
                        if (jsT == "string") {
                            out += "      const p" + String((long long)pIdx) + " = writeString(args[" + String((long long)jsArgIdx) + "]);\n";
                            toFree.push("p" + String((long long)pIdx));
                        }
                    }
                    jsArgIdx++;
                }

                // Call WASM export
                out += "      ";
                bool returnsClass = false;
                String retClassName;
                if (m.returnType != "void") {
                    if (isClassType(m.returnType, classes, retClassName)) {
                        returnsClass = true;
                    }
                }

                if (returnsClass) {
                    out += "const resPtr = ";
                } else if (m.returnType == "const char*" || m.returnType == "const char *" || m.returnType == "char*" || m.returnType == "char *") {
                    out += "const resPtr = ";
                } else if (m.returnType != "void") {
                    out += "const res = ";
                }

                out += "exports.export_" + replaceColons(cls.name) + "_" + m.name + "_" + String((long long)methodIdx) + "(";
                if (!m.isStatic) {
                    out += "this.ptr";
                    if (m.params.size() > 0) out += ", ";
                }

                jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (pIdx > 0) out += ", ";
                    if (isLenParam[pIdx]) {
                        // Passed as part of string-and-length
                        out += "p" + String((long long)(pIdx - 1)) + ".len";
                        continue;
                    }
                    if (paramMap[pIdx] >= 0) {
                        out += "p" + String((long long)pIdx) + ".ptr";
                    } else {
                        String jsT = getJsType(m.params[pIdx].type, classes);
                        if (jsT == "string") {
                            out += "p" + String((long long)pIdx);
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                            out += "args[" + String((long long)jsArgIdx) + "].ptr";
                        } else {
                            out += "args[" + String((long long)jsArgIdx) + "]";
                        }
                    }
                    jsArgIdx++;
                }
                out += ");\n";

                // Free allocated strings
                for (usz k = 0; k < toFree.size(); ++k) {
                    out += "      exports.free_buf(" + toFree[k] + ");\n";
                }

                // Return result
                if (returnsClass) {
                    out += "      return new " + getJsName(retClassName) + "(resPtr, INTERNAL);\n";
                } else if (m.returnType == "const char*" || m.returnType == "const char *" || m.returnType == "char*" || m.returnType == "char *") {
                    out += "      return readString(resPtr);\n";
                } else if (m.returnType != "void") {
                    out += "      return res;\n";
                }

                out += "    }\n";
            }

            out += "    throw new Error('No method overload of \"" + mName + "\" matched given arguments');\n";
            out += "  }\n\n";
        }

        // Generate fields (getters / setters)
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            String jsT = getJsType(f.type, classes);

            if (f.docComment.length() > 0) {
                out += "  /**\n";
                Array<String> docLines = f.docComment.split("\n");
                for (usz k = 0; k < docLines.size(); ++k) {
                    out += "   * " + docLines[k] + "\n";
                }
                out += "   */\n";
            }

            // Getter
            out += "  get " + f.name + "(): " + jsT + " {\n";
            if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                out += "    return new " + jsT + "(exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr), INTERNAL);\n";
            } else if (jsT == "string") {
                out += "    return readString(exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr));\n";
            } else {
                out += "    return exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr);\n";
            }
            out += "  }\n\n";

            // Setter
            if (!f.isConst) {
                out += "  set " + f.name + "(val: " + jsT + ") {\n";
                if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, val.ptr);\n";
                } else if (jsT == "string") {
                    out += "    const ptr = writeString(val);\n";
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, ptr);\n";
                    out += "    exports.free_buf(ptr);\n";
                } else {
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, val);\n";
                }
                out += "  }\n\n";
            }
        }

        out += "}\n\n";
    }

    // Global Functions JS wraps
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        String jsRetT = getJsType(fn.returnType, classes);

        if (fn.docComment.length() > 0) {
            out += "/**\n";
            Array<String> docLines = fn.docComment.split("\n");
            for (usz j = 0; j < docLines.size(); ++j) {
                out += " * " + docLines[j] + "\n";
            }
            out += " */\n";
        }

        out += "export function " + getJsName(fn.name) + "(";

        // TS arguments
        Array<int> paramMap;
        Array<bool> isLenParam;
        for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
            paramMap.push(-1);
            isLenParam.push(false);
        }
        for (usz pIdx = 0; pIdx + 1 < fn.params.size(); ++pIdx) {
            if (isStringAndLengthPattern(fn.params[pIdx], fn.params[pIdx + 1])) {
                paramMap.data()[pIdx] = (int)(pIdx + 1);
                isLenParam.data()[pIdx + 1] = true;
            }
        }

        int jsArgIdx = 0;
        for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
            if (isLenParam[pIdx]) continue;
            if (jsArgIdx > 0) out += ", ";
            String jsT = getJsType(fn.params[pIdx].type, classes);
            if (paramMap[pIdx] >= 0) {
                out += fn.params[pIdx].name + ": string";
            } else {
                out += fn.params[pIdx].name + ": " + jsT;
            }
            jsArgIdx++;
        }
        out += "): " + jsRetT + " {\n";

        // Marshalling
        Array<String> toFree;
        for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
            if (isLenParam[pIdx]) continue;
            if (paramMap[pIdx] >= 0) {
                out += "  const p" + String((long long)pIdx) + " = writeBuffer(" + fn.params[pIdx].name + ");\n";
                toFree.push("p" + String((long long)pIdx) + ".ptr");
            } else {
                String jsT = getJsType(fn.params[pIdx].type, classes);
                if (jsT == "string") {
                    out += "  const p" + String((long long)pIdx) + " = writeString(" + fn.params[pIdx].name + ");\n";
                    toFree.push("p" + String((long long)pIdx));
                }
            }
        }

        // Call WASM
        out += "  ";
        bool returnsClass = false;
        String retClassName;
        if (fn.returnType != "void") {
            if (isClassType(fn.returnType, classes, retClassName)) {
                returnsClass = true;
            }
        }

        if (returnsClass) {
            out += "const resPtr = ";
        } else if (fn.returnType == "const char*" || fn.returnType == "const char *" || fn.returnType == "char*" || fn.returnType == "char *") {
            out += "const resPtr = ";
        } else if (fn.returnType != "void") {
            out += "const res = ";
        }

        out += "exports.export_" + replaceColons(fn.name) + "(";
        for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
            if (pIdx > 0) out += ", ";
            if (isLenParam[pIdx]) {
                out += "p" + String((long long)(pIdx - 1)) + ".len";
                continue;
            }
            if (paramMap[pIdx] >= 0) {
                out += "p" + String((long long)pIdx) + ".ptr";
            } else {
                String jsT = getJsType(fn.params[pIdx].type, classes);
                if (jsT == "string") {
                    out += "p" + String((long long)pIdx);
                } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                    out += fn.params[pIdx].name + ".ptr";
                } else {
                    out += fn.params[pIdx].name;
                }
            }
        }
        out += ");\n";

        // Free
        for (usz k = 0; k < toFree.size(); ++k) {
            out += "  exports.free_buf(" + toFree[k] + ");\n";
        }

        // Return
        if (returnsClass) {
            out += "  return new " + getJsName(retClassName) + "(resPtr, INTERNAL);\n";
        } else if (fn.returnType == "const char*" || fn.returnType == "const char *" || fn.returnType == "char*" || fn.returnType == "char *") {
            out += "  return readString(resPtr);\n";
        } else if (fn.returnType != "void") {
            out += "  return res;\n";
        }

        out += "}\n\n";
    }

    return out;
}

} // namespace Sew
