/**
 * @file JS.cpp
 * @brief JavaScript execution context implementation.
 */

#include <Languages/JS/JS.hpp>
#include <cstdio>
#include <cstring>

namespace Sew { namespace Languages {

extern "C" JSContext *sew_global_ctx = nullptr;

// --- Class Binder Registry ---
Map<const void*, ClassBinderFunc>& getClassBinders() {
    static Map<const void*, ClassBinderFunc> binders;
    return binders;
}

// --- Callback Registry for JS set(lambda) ---
static JSClassID js_callback_class_id = 0;
struct CallbackWrapper {
    Func<JsValue(Array<JsValue>)> fn;
};

static void js_callback_finalizer(JSRuntime *rt, ::JSValue val) {
    CallbackWrapper *cb = (CallbackWrapper*)JS_GetOpaque(val, js_callback_class_id);
    delete cb;
}

static JSClassDef js_callback_class = {
    "CallbackWrapper",
    js_callback_finalizer,
    nullptr,
    nullptr,
    nullptr
};

// --- JsValue Implementation ---

JsValue::JsValue() {
    _val = JS_UNDEFINED;
}

JsValue::JsValue(JSContext* ctx, ::JSValue val) : _ctx(ctx), _val(val) {}

JsValue::JsValue(const JsValue& other) : _ctx(other._ctx) {
    if (_ctx) {
        _val = JS_DupValue(_ctx, other._val);
    } else {
        _val = JS_UNDEFINED;
    }
}

JsValue::JsValue(JsValue&& other) noexcept : _ctx(other._ctx), _val(other._val) {
    other._ctx = nullptr;
    other._val = JS_UNDEFINED;
}

JsValue& JsValue::operator=(const JsValue& other) {
    if (this != &other) {
        if (_ctx) {
            JS_FreeValue(_ctx, _val);
        }
        _ctx = other._ctx;
        if (_ctx) {
            _val = JS_DupValue(_ctx, other._val);
        } else {
            _val = JS_UNDEFINED;
        }
    }
    return *this;
}

JsValue& JsValue::operator=(JsValue&& other) noexcept {
    if (this != &other) {
        if (_ctx) {
            JS_FreeValue(_ctx, _val);
        }
        _ctx = other._ctx;
        _val = other._val;
        other._ctx = nullptr;
        other._val = JS_UNDEFINED;
    }
    return *this;
}

JsValue::~JsValue() {
    if (_ctx) {
        JS_FreeValue(_ctx, _val);
    }
}

bool JsValue::isUndefined() const { return JS_IsUndefined(_val); }
bool JsValue::isNull() const { return JS_IsNull(_val); }
bool JsValue::isBoolean() const { return JS_IsBool(_val); }
bool JsValue::isNumber() const { return JS_IsNumber(_val); }
bool JsValue::isString() const { return JS_IsString(_val); }
bool JsValue::isObject() const { return JS_IsObject(_val); }
bool JsValue::isFunction() const { return _ctx ? JS_IsFunction(_ctx, _val) : false; }

String JsValue::type() const {
    if (isUndefined()) return "undefined";
    if (isNull()) return "null";
    if (isBoolean()) return "boolean";
    if (isNumber()) return "number";
    if (isString()) return "string";
    if (isFunction()) return "function";
    if (isObject()) return "object";
    return "unknown";
}

String JsValue::toString() const {
    if (!_ctx) return "";
    const char* str = JS_ToCString(_ctx, _val);
    if (!str) return "";
    String res(str);
    JS_FreeCString(_ctx, str);
    return res;
}

double JsValue::toDouble() const {
    if (!_ctx) return 0.0;
    double d = 0.0;
    JS_ToFloat64(_ctx, &d, _val);
    return d;
}

int JsValue::toInt() const {
    if (!_ctx) return 0;
    int32_t i = 0;
    JS_ToInt32(_ctx, &i, _val);
    return i;
}

bool JsValue::toBool() const {
    if (!_ctx) return false;
    return JS_ToBool(_ctx, _val) != 0;
}

// --- Module Normalize & Loader helpers ---

static char *js_module_normalize(JSContext *ctx, const char *module_base_name,
                                 const char *module_name, void *opaque) {
    size_t len = strlen(module_name);
    char *res = (char *)js_malloc(ctx, len + 1);
    memcpy(res, module_name, len + 1);
    return res;
}

static JSModuleDef *js_module_loader(JSContext *ctx, const char *module_name, void *opaque) {
    JS *js = (JS*)opaque;
    JsValue impVal = js->get("imp");
    if (impVal.isFunction()) {
        ::JSValueConst argv[1];
        argv[0] = JS_NewString(ctx, module_name);
        ::JSValue res = JS_Call(ctx, impVal.getVal(), JS_UNDEFINED, 1, argv);
        JS_FreeValue(ctx, argv[0]);
        if (JS_IsException(res)) {
            return nullptr;
        }
        if (!JS_IsString(res)) {
            JS_FreeValue(ctx, res);
            return nullptr;
        }
        String code = JsValue(ctx, res).toString();
        ::JSValue modVal = JS_Eval(ctx, code.c_str(), code.size(), module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(modVal)) {
            return nullptr;
        }
        JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(modVal);
        JS_FreeValue(ctx, modVal);
        return m;
    }
    return nullptr;
}

// --- JS Implementation ---

JS::JS() {
    if (sew_global_ctx) {
        _ctx = sew_global_ctx;
        _rt = JS_GetRuntime(_ctx);
        _ownContext = false;
        _ownRuntime = false;
    } else {
        _rt = JS_NewRuntime();
        _ctx = JS_NewContext(_rt);
        _ownContext = true;
        _ownRuntime = true;
        // Set custom module loaders
        JS_SetModuleLoaderFunc(_rt, js_module_normalize, js_module_loader, this);
    }
}

JS::~JS() {
    if (_ownContext && _ctx) JS_FreeContext(_ctx);
    if (_ownRuntime && _rt) JS_FreeRuntime(_rt);
}

JsValue JS::eval(const String& code) {
    ::JSValue res = JS_Eval(_ctx, code.c_str(), code.size(), "<eval>", JS_EVAL_TYPE_GLOBAL);
    return JsValue(_ctx, res);
}

JsValue JS::eval(const Array<uint8_t>& bytecode) {
    ::JSValue obj = JS_ReadObject(_ctx, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        return JsValue(_ctx, obj);
    }
    ::JSValue res = JS_EvalFunction(_ctx, obj);
    return JsValue(_ctx, res);
}

Array<uint8_t> JS::compile(const String& code) {
    ::JSValue obj = JS_Eval(_ctx, code.c_str(), code.size(), "<eval>", JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(obj)) {
        ::JSValue exception = JS_GetException(_ctx);
        const char* str = JS_ToCString(_ctx, exception);
        fprintf(stderr, "[JS COMPILE ERROR] %s\n", str);
        JS_FreeCString(_ctx, str);
        JS_FreeValue(_ctx, exception);
        return {};
    }
    size_t bufLen = 0;
    uint8_t* buf = JS_WriteObject(_ctx, &bufLen, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(_ctx, obj);
    if (!buf) return {};
    Array<uint8_t> res;
    for (size_t i = 0; i < bufLen; ++i) {
        res.push(buf[i]);
    }
    js_free(_ctx, buf);
    return res;
}

JsValue JS::get(const String& path) {
    Array<String> parts = path.split(".");
    ::JSValue obj = JS_DupValue(_ctx, JS_GetGlobalObject(_ctx));
    for (usz i = 0; i < parts.size(); ++i) {
        JSAtom atom = JS_NewAtom(_ctx, parts[i].c_str());
        ::JSValue next = JS_GetProperty(_ctx, obj, atom);
        JS_FreeAtom(_ctx, atom);
        JS_FreeValue(_ctx, obj);
        obj = next;
        if (JS_IsUndefined(obj) || JS_IsNull(obj)) {
            break;
        }
    }
    return JsValue(_ctx, obj);
}

void JS::set(const String& path, const JsValue& val) {
    Array<String> parts = path.split(".");
    if (parts.size() == 0) return;
    ::JSValue obj = JS_DupValue(_ctx, JS_GetGlobalObject(_ctx));
    for (usz i = 0; i < parts.size() - 1; ++i) {
        JSAtom atom = JS_NewAtom(_ctx, parts[i].c_str());
        ::JSValue next = JS_GetProperty(_ctx, obj, atom);
        if (JS_IsUndefined(next) || JS_IsNull(next)) {
            JS_FreeValue(_ctx, next);
            next = JS_NewObject(_ctx);
            JS_SetProperty(_ctx, obj, atom, JS_DupValue(_ctx, next));
        }
        JS_FreeAtom(_ctx, atom);
        JS_FreeValue(_ctx, obj);
        obj = next;
    }
    JSAtom atom = JS_NewAtom(_ctx, parts[parts.size() - 1].c_str());
    JS_SetProperty(_ctx, obj, atom, JS_DupValue(_ctx, val.getVal()));
    JS_FreeAtom(_ctx, atom);
    JS_FreeValue(_ctx, obj);
}

void JS::set(const String& path, const String& val) {
    ::JSValue jsv = JS_NewString(_ctx, val.c_str());
    set(path, JsValue(_ctx, jsv));
}

void JS::set(const String& path, const char* val) {
    ::JSValue jsv = JS_NewString(_ctx, val);
    set(path, JsValue(_ctx, jsv));
}

void JS::set(const String& path, double val) {
    ::JSValue jsv = JS_NewFloat64(_ctx, val);
    set(path, JsValue(_ctx, jsv));
}

void JS::set(const String& path, int val) {
    ::JSValue jsv = JS_NewInt32(_ctx, val);
    set(path, JsValue(_ctx, jsv));
}

void JS::set(const String& path, bool val) {
    ::JSValue jsv = JS_NewBool(_ctx, val);
    set(path, JsValue(_ctx, jsv));
}

void JS::set(const String& path, Func<JsValue(Array<JsValue>)> fn) {
    if (js_callback_class_id == 0) {
        JS_NewClassID(&js_callback_class_id);
        JS_NewClass(_rt, js_callback_class_id, &js_callback_class);
    }
    ::JSValue obj = JS_NewObjectClass(_ctx, js_callback_class_id);
    CallbackWrapper *cb = new CallbackWrapper{fn};
    JS_SetOpaque(obj, cb);

    ::JSValue func = JS_NewCFunctionData(_ctx, [](JSContext *ctx, ::JSValueConst this_val, int argc, ::JSValueConst *argv, int magic, ::JSValue *data) -> ::JSValue {
        CallbackWrapper *wrapper = (CallbackWrapper*)JS_GetOpaque(data[0], js_callback_class_id);
        Array<JsValue> args;
        for (int i = 0; i < argc; ++i) {
            args.push(JsValue(ctx, JS_DupValue(ctx, argv[i])));
        }
        JsValue res = wrapper->fn(args);
        return JS_DupValue(ctx, res.getVal());
    }, 0, 0, 1, &obj);

    JS_FreeValue(_ctx, obj);

    JsValue val(_ctx, func);
    set(path, val);
}

String JS::type(const String& path) {
    JsValue val = get(path);
    return val.type();
}

void JS::del(const String& path) {
    Array<String> parts = path.split(".");
    if (parts.size() == 0) return;
    ::JSValue obj = JS_DupValue(_ctx, JS_GetGlobalObject(_ctx));
    for (usz i = 0; i < parts.size() - 1; ++i) {
        JSAtom atom = JS_NewAtom(_ctx, parts[i].c_str());
        ::JSValue next = JS_GetProperty(_ctx, obj, atom);
        JS_FreeAtom(_ctx, atom);
        JS_FreeValue(_ctx, obj);
        obj = next;
        if (JS_IsUndefined(obj) || JS_IsNull(obj)) {
            JS_FreeValue(_ctx, obj);
            return;
        }
    }
    JSAtom atom = JS_NewAtom(_ctx, parts[parts.size() - 1].c_str());
    JS_DeleteProperty(_ctx, obj, atom, 0);
    JS_FreeAtom(_ctx, atom);
    JS_FreeValue(_ctx, obj);
}

void JS::defTypeInternal(const void* typeId) {
    ClassBinderFunc* binder = getClassBinders().get(typeId);
    if (binder) {
        (*binder)(_ctx);
    }
}

}} // namespace Sew::Languages
