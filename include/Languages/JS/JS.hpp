/**
 * @file JS.hpp
 * @brief JavaScript context and value wrapper using QuickJS.
 */

#pragma once

#include <Languages/Context.hpp>
#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Xi/Func.hpp>
#include <quickjs.h>
#include <type_traits>


namespace Sew { namespace Languages {

using namespace Collection;
using namespace Xi;

class JS;

/**
 * @class JsValue
 * @brief Opaque wrapper around QuickJS JSValue. Handles ref-counting automatically.
 */
class JsValue {
public:
    JsValue();
    JsValue(JSContext* ctx, ::JSValue val);
    JsValue(const JsValue& other);
    JsValue(JsValue&& other) noexcept;
    JsValue& operator=(const JsValue& other);
    JsValue& operator=(JsValue&& other) noexcept;
    ~JsValue();

    // Type queries
    bool isUndefined() const;
    bool isNull() const;
    bool isBoolean() const;
    bool isNumber() const;
    bool isString() const;
    bool isObject() const;
    bool isFunction() const;
    String type() const;

    // Coercions
    String toString() const;
    double toDouble() const;
    int toInt() const;
    bool toBool() const;

    // Internals
    ::JSValue getVal() const { return _val; }
    JSContext* getCtx() const { return _ctx; }

private:
    JSContext* _ctx = nullptr;
    ::JSValue _val;
};

// C++ type registry helpers for defType<T>()
typedef void (*ClassBinderFunc)(JSContext* ctx);
extern Map<const void*, ClassBinderFunc>& getClassBinders();

template<typename T>
struct TypeRegistry {
    static const void* id() { return &id_val; }
    static char id_val;
};
template<typename T>
char TypeRegistry<T>::id_val = 0;

/**
 * @class JS
 * @brief JavaScript execution context.
 */
class JS : public Context {
public:
    JS();
    ~JS();

    JsValue eval(const String& code);
    JsValue eval(const Array<uint8_t>& bytecode);
    Array<uint8_t> compile(const String& code);

    // Variable getters / setters
    JsValue get(const String& path);
    void set(const String& path, const JsValue& val);
    void set(const String& path, const String& val);
    void set(const String& path, const char* val);
    void set(const String& path, double val);
    void set(const String& path, int val);
    void set(const String& path, bool val);

    // Support lambda/function wrapping
    void set(const String& path, Func<JsValue(Array<JsValue>)> fn);

    // Support setting generic callables (like lambdas)
    template<typename F, typename = typename std::enable_if<!std::is_convertible<F, JsValue>::value &&
                                                           !std::is_convertible<F, String>::value &&
                                                           !std::is_convertible<F, const char*>::value &&
                                                           !std::is_convertible<F, double>::value &&
                                                           !std::is_convertible<F, int>::value &&
                                                           !std::is_convertible<F, bool>::value>::type>
    void set(const String& path, F fn) {
        set(path, Func<JsValue(Array<JsValue>)>([fn](Array<JsValue> args) mutable -> JsValue {
            return fn(args);
        }));
    }

    String type(const String& path);
    void del(const String& path);

    // Class binding registry
    template<typename T>
    void defType() {
        defTypeInternal(TypeRegistry<T>::id());
    }

    void defTypeInternal(const void* typeId);

    // Helper proxy class for bracket operators
    struct Proxy {
        JS& js;
        String path;

        Proxy(JS& js, const String& path) : js(js), path(path) {}

        Proxy operator[](const String& key) {
            return Proxy(js, path.isEmpty() ? key : (path + "." + key));
        }

        template<typename T>
        Proxy& operator=(const T& val) {
            js.set(path, val);
            return *this;
        }

        operator JsValue() const {
            return js.get(path);
        }
    };

    Proxy operator[](const String& key) {
        return Proxy(*this, key);
    }

    JSContext* getCtx() const { return _ctx; }
    JSRuntime* getRt() const { return _rt; }

private:
    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;
    bool _ownRuntime = false;
    bool _ownContext = false;
};

}} // namespace Sew::Languages
