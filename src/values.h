#ifndef NODE_PHP_VALUES_H
#define NODE_PHP_VALUES_H
#include <cstdlib>
#include <limits>
#include <new>

#include "nan.h"

#include "php.h"

namespace node_php_embed {

class NonAssignable {
private:
    NonAssignable(NonAssignable const&);
    NonAssignable& operator=(NonAssignable const&);
public:
    NonAssignable() {}
};

class ObjectMapper {
 public:
    virtual uint32_t IdForJsObj(const v8::Local<v8::Object> o) = 0;
    virtual uint32_t IdForPhpObj(const zval *o) = 0;
    virtual void GetPhp(uint32_t id, zval *return_value TSRMLS_DC) = 0;
    virtual v8::Local<v8::Object> GetJs(uint32_t id) = 0;
};

/** Helper for PHP zvals */
class ZVal : public NonAssignable {
 public:
    ZVal() { ALLOC_INIT_ZVAL(zvalp); }
    virtual ~ZVal() { zval_ptr_dtor(&zvalp); }
    inline zval * operator*() const { return zvalp; }
    inline zval * Escape() { Z_ADDREF_P(zvalp); return zvalp; }
    inline int Type() { return Z_TYPE_P(zvalp); }
    inline bool IsNull() { return Type() == IS_NULL; }
    inline bool IsBool() { return Type() == IS_BOOL; }
    inline bool IsLong() { return Type() == IS_LONG; }
    inline bool IsDouble() { return Type() == IS_DOUBLE; }
    inline bool IsString() { return Type() == IS_STRING; }
    inline bool IsArray() { return Type() == IS_ARRAY; }
    inline bool IsObject() { return Type() == IS_OBJECT; }
    inline bool IsResource() { return Type() == IS_RESOURCE; }
    inline void SetNull() { ZVAL_NULL(zvalp); }
    inline void SetBool(bool b) { ZVAL_BOOL(zvalp, b ? 1 : 0); }
    inline void SetLong(long l) { ZVAL_LONG(zvalp, l); }
    inline void SetDouble(double d) { ZVAL_DOUBLE(zvalp, d); }
    inline void SetString(const char *str, int len, bool dup) { ZVAL_STRINGL(zvalp, str, len, dup); }
 private:
    zval *zvalp;
};

/* A poor man's tagged union, so that we can stack allocate messages
 * containing values without having to pay for heap allocation.
 */
 class Value : public NonAssignable {
    class Base : public NonAssignable {
    public:
        explicit Base() { }
        virtual ~Base() { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const = 0;
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const = 0;
    };
    class Null : public Base {
    public:
        explicit Null() { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::Null());
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETURN_NULL();
        }
    };
    class Bool : public Base {
    public:
        bool value_;
        explicit Bool(bool value) : value_(value) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(value_));
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETURN_BOOL(value_);
        }
    };
    class Int : public Base {
    public:
        int64_t value_;
        explicit Int(int64_t value) : value_(value) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            if (value_ >= 0 && value_ <= std::numeric_limits<uint32_t>::max()) {
                return scope.Escape(Nan::New((uint32_t)value_));
            } else if (value_ >= std::numeric_limits<int32_t>::min() &&
                       value_ <= std::numeric_limits<int32_t>::max()) {
                return scope.Escape(Nan::New((int32_t)value_));
            }
            return scope.Escape(Nan::New((double)value_));
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETURN_LONG((long)value_);
        }
    };
    class Double : public Base {
        double value_;
    public:
        explicit Double(double value) : value_(value) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(value_));
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETURN_DOUBLE(value_);
        }
    };
    class Str : public Base {
    protected:
        const char *data_;
        std::size_t length_;
    public:
        explicit Str(const char *data, std::size_t length)
            : data_(data), length_(length) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(data_, length_).ToLocalChecked());
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETURN_STRINGL(data_, length_, 1);
        }
    };
    class OStr : public Str {
        // an "owned string", will copy data on creation and free it on delete.
    public:
        explicit OStr(const char *data, std::size_t length)
            : Str(estrndup(data, length+1), length) { }
        virtual ~OStr() {
            if (data_) {
                efree(const_cast<char*>(data_));
            }
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            RETVAL_STRINGL(data_, length_, 0/*no dup required*/);
            //data_ = NULL;
        }
    };
    class JsObj : public Base {
        uint32_t id_;
    public:
        explicit JsObj(ObjectMapper *m, v8::Local<v8::Object> o)
            : id_(m->IdForJsObj(o)) { }
        explicit JsObj(uint32_t id)
            : id_(id) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(m->GetJs(id_));
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            // wrap js object
            node_php_jsobject_create(return_value, m, id_ TSRMLS_CC);
        }
    };
    class PhpObj : public Base {
        uint32_t id_;
    public:
        explicit PhpObj(ObjectMapper *m, zval *o)
            : id_(m->IdForPhpObj(o)) { }
        explicit PhpObj(uint32_t id)
            : id_(id) { }
        virtual v8::Local<v8::Value> ToJs(ObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            // XXX wrap php object
            return scope.Escape(Nan::Null());
        }
        virtual void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) const {
            m->GetPhp(id_, return_value TSRMLS_CC);
        }
    };

 public:
    explicit Value() : type_(VALUE_EMPTY), empty_(0) { }
    virtual ~Value() { PerhapsDestroy(); }

    explicit Value(ObjectMapper *m, v8::Local<v8::Value> v) : type_(VALUE_EMPTY), empty_(0) {
        Set(m, v);
    }
    explicit Value(ObjectMapper *m, zval *v) : type_(VALUE_EMPTY), empty_(0) {
        Set(m, v);
    }
    template <typename T>
    static Value *NewArray(ObjectMapper *m, int argc, T* argv) {
        Value *result = new Value[argc];
        for (int i=0; i<argc; i++) {
            result[i].Set(m, argv[i]);
        }
        return result;
    }
    void Set(ObjectMapper *m, v8::Local<v8::Value> v) {
        if (v->IsUndefined() || v->IsNull()) {
            /* fall through to the default case */
        } else if (v->IsBoolean()) {
            SetBool(Nan::To<bool>(v).FromJust());
            return;
        } else if (v->IsInt32() || v->IsUint32()) {
            SetInt(Nan::To<int64_t>(v).FromJust());
            return;
        } else if (v->IsNumber()) {
            SetDouble(Nan::To<double>(v).FromJust());
            return;
        } else if (v->IsString()) {
            Nan::Utf8String str(v);
            if (*str) {
                SetOwnedString(*str, str.length());
                return;
            }
        } else if (v->IsObject()) {
            SetJsObject(m, Nan::To<v8::Object>(v).ToLocalChecked());
            return;
        }
        // Null for all other object types.
        SetNull();
    }
    void Set(ObjectMapper *m, zval *v) {
        long l;
        switch (Z_TYPE_P(v)) {
        default:
        case IS_NULL:
            SetNull();
            return;
        case IS_BOOL:
            SetBool(Z_BVAL_P(v));
            return;
        case IS_LONG:
            l = Z_LVAL_P(v);
            if (l >= std::numeric_limits<int32_t>::min() &&
                l <= (int64_t)std::numeric_limits<uint32_t>::max()) {
                SetInt((int64_t)l);
            } else {
                SetDouble((double)l);
            }
            return;
        case IS_DOUBLE:
            SetDouble(Z_DVAL_P(v));
            return;
        case IS_STRING:
            // since PHP blocks, it is fine to let PHP
            // own the buffer; avoids needless copying.
            SetString(Z_STRVAL_P(v), Z_STRLEN_P(v));
            return;
        case IS_OBJECT:
            SetPhpObject(m, v);
            return;
            /*
        case IS_ARRAY:
            */
        }
    }
    void SetEmpty() {
        PerhapsDestroy();
        type_ = VALUE_EMPTY;
        empty_ = 0;
    }
    void SetNull() {
        PerhapsDestroy();
        type_ = VALUE_NULL;
        new (&null_) Null();
    }
    void SetBool(bool value) {
        PerhapsDestroy();
        type_ = VALUE_BOOL;
        new (&bool_) Bool(value);
    }
    void SetInt(int64_t value) {
        PerhapsDestroy();
        type_ = VALUE_INT;
        new (&int_) Int(value);
    }
    void SetDouble(double value) {
        PerhapsDestroy();
        type_ = VALUE_DOUBLE;
        new (&double_) Double(value);
    }
    void SetString(const char *data, std::size_t length) {
        PerhapsDestroy();
        type_ = VALUE_STR;
        new (&str_) Str(data, length);
    }
    void SetOwnedString(const char *data, std::size_t length) {
        PerhapsDestroy();
        type_ = VALUE_OSTR;
        new (&ostr_) OStr(data, length);
    }
    void SetJsObject(ObjectMapper *m, v8::Local<v8::Object> o) {
        SetJsObject(m->IdForJsObj(o));
    }
    void SetJsObject(uint32_t id) {
        PerhapsDestroy();
        type_ = VALUE_JSOBJ;
        new (&jsobj_) JsObj(id);
    }
    void SetPhpObject(ObjectMapper *m, zval *o) {
        SetPhpObject(m->IdForPhpObj(o));
    }
    void SetPhpObject(uint32_t id) {
        PerhapsDestroy();
        type_ = VALUE_PHPOBJ;
        new (&phpobj_) PhpObj(id);
    }

    v8::Local<v8::Value> ToJs(ObjectMapper *m) {
        // should we create a new escapablehandlescope here?
        return AsBase().ToJs(m);
    }
    void ToPhp(ObjectMapper *m, zval *return_value TSRMLS_DC) {
        AsBase().ToPhp(m, return_value TSRMLS_CC);
    }
    bool IsEmpty() {
        return (type_ == VALUE_EMPTY);
    }
    bool AsBool() {
        switch(type_) {
        case VALUE_BOOL:
            return bool_.value_;
        case VALUE_INT:
            return int_.value_ != 0;
        default:
            return false;
        }
    }
 private:
    void PerhapsDestroy() {
        if (!IsEmpty()) {
            AsBase().~Base();
        }
        type_ = VALUE_EMPTY;
    }
    enum ValueTypes {
        VALUE_EMPTY, VALUE_NULL, VALUE_BOOL, VALUE_INT, VALUE_DOUBLE,
        VALUE_STR, VALUE_OSTR, VALUE_JSOBJ, VALUE_PHPOBJ
    } type_;
    union {
        int empty_; Null null_; Bool bool_; Int int_; Double double_;
        Str str_; OStr ostr_; JsObj jsobj_; PhpObj phpobj_;
    };
    const Base &AsBase() {
        switch(type_) {
        default:
            // should never get here.
        case VALUE_NULL:
            return null_;
        case VALUE_BOOL:
            return bool_;
        case VALUE_INT:
            return int_;
        case VALUE_DOUBLE:
            return double_;
        case VALUE_STR:
            return str_;
        case VALUE_OSTR:
            return ostr_;
        case VALUE_JSOBJ:
            return jsobj_;
        case VALUE_PHPOBJ:
            return phpobj_;
        }
    }
};

}
#endif
