#ifndef NODE_PHP_VALUES_H
#define NODE_PHP_VALUES_H
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <sstream>

#include <nan.h>

#include <main/php.h>

#include "node_php_jsbuffer_class.h" /* to recognize buffers in PHP land */
#include "macros.h"

namespace node_php_embed {

class NonAssignable {
private:
    NonAssignable(NonAssignable const&);
    NonAssignable& operator=(NonAssignable const&);
public:
    NonAssignable() {}
};

typedef uint32_t objid_t;

// Methods in JsObjectMapper are/should be accessed only from the JS thread.
// The mapper will hold persistent references to the objects for which it
// has ids.
class JsObjectMapper {
 public:
    virtual objid_t IdForJsObj(const v8::Local<v8::Object> o) = 0;
    virtual v8::Local<v8::Object> JsObjForId(objid_t id) = 0;
};
// Methods in PhpObjectMapper are/should be accessed only from the PHP thread.
// The mapper will hold references for the zvals it returns.
class PhpObjectMapper {
 public:
    virtual objid_t IdForPhpObj(zval *o) = 0;
    virtual zval * PhpObjForId(objid_t id TSRMLS_DC) = 0;
};
class ObjectMapper : public JsObjectMapper, public PhpObjectMapper {
    /* an object mapper is used by both threads */
};

/** Helper for PHP zvals */
class ZVal : public NonAssignable {
 public:
    ZVal(ZEND_FILE_LINE_D) : transferred(false) {
        ALLOC_ZVAL_REL(zvalp);
        INIT_ZVAL(*zvalp);
    }
    ZVal(zval *z ZEND_FILE_LINE_DC) : zvalp(z), transferred(false) {
        if (zvalp) {
            Z_ADDREF_P(zvalp);
        } else {
            ALLOC_ZVAL_REL(zvalp);
            INIT_ZVAL(*zvalp);
        }
    }
    virtual ~ZVal() {
        if (transferred) {
            efree(zvalp);
        } else {
            zval_ptr_dtor(&zvalp);
        }
    }
    inline zval * Ptr() const { return zvalp; }
    inline zval ** PtrPtr() { return &zvalp; }
    inline zval * Escape() { Z_ADDREF_P(zvalp); return zvalp; }
    // Support a PHP calling convention where the actual zval object
    // is owned by the caller, but the contents are transferred to the
    // callee.
    inline zval * Transfer(TSRMLS_D) {
        if (IsObject()) {
            zend_objects_store_add_ref(zvalp TSRMLS_CC);
        } else {
            transferred=true;
        }
        return zvalp;
    }
    inline zval * operator*() const { return Ptr(); } // shortcut
    inline int Type() { return Z_TYPE_P(zvalp); }
    inline bool IsNull() { return Type() == IS_NULL; }
    inline bool IsBool() { return Type() == IS_BOOL; }
    inline bool IsLong() { return Type() == IS_LONG; }
    inline bool IsDouble() { return Type() == IS_DOUBLE; }
    inline bool IsString() { return Type() == IS_STRING; }
    inline bool IsArray() { return Type() == IS_ARRAY; }
    inline bool IsObject() { return Type() == IS_OBJECT; }
    inline bool IsResource() { return Type() == IS_RESOURCE; }
    inline void Set(zval *z ZEND_FILE_LINE_DC) {
        zval_ptr_dtor(&zvalp);
        zvalp=z;
        if (zvalp) {
            Z_ADDREF_P(zvalp);
        } else {
            ALLOC_ZVAL_REL(zvalp);
            INIT_ZVAL(*zvalp);
        }
    }
    inline void SetNull() { ZVAL_NULL(zvalp); }
    inline void SetBool(bool b) { ZVAL_BOOL(zvalp, b ? 1 : 0); }
    inline void SetLong(long l) { ZVAL_LONG(zvalp, l); }
    inline void SetDouble(double d) { ZVAL_DOUBLE(zvalp, d); }
    inline void SetString(const char *str, int len, bool dup) {
        ZVAL_STRINGL(zvalp, str, len, dup);
    }
 private:
    zval *zvalp;
    bool transferred;
};

/* A poor man's tagged union, so that we can stack allocate messages
 * containing values without having to pay for heap allocation.
 */
 class Value : public NonAssignable {
    class Base : public NonAssignable {
    public:
        explicit Base() { }
        virtual ~Base() { }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const = 0;
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const = 0;
        /* For debugging.  The returned value should not be deallocated. */
        virtual const char *TypeString() const = 0;
        /* For debugging purposes. Caller (implicitly) deallocates. */
        virtual std::string ToString() const {
            return std::string(TypeString());
        }
    };
    class Null : public Base {
    public:
        explicit Null() { }
        virtual const char *TypeString() const { return "Null"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::Null());
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            RETURN_NULL();
        }
    };
    template <class T>
    class Prim : public Base {
    public:
        T value_;
        explicit Prim(T value) : value_(value) { }
        virtual std::string ToString() const {
            std::stringstream ss;
            ss << TypeString() << "(" << value_ << ")";
            return ss.str();
        }
    };
    class Bool : public Prim<bool> {
    public:
        using Prim::Prim;
        virtual const char *TypeString() const { return "Bool"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(value_));
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            RETURN_BOOL(value_);
        }
    };
    class Int : public Prim<int64_t> {
    public:
        using Prim::Prim;
        virtual const char *TypeString() const { return "Int"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            if (value_ >= 0 && value_ <= std::numeric_limits<uint32_t>::max()) {
                return scope.Escape(Nan::New((uint32_t)value_));
            } else if (value_ >= std::numeric_limits<int32_t>::min() &&
                       value_ <= std::numeric_limits<int32_t>::max()) {
                return scope.Escape(Nan::New((int32_t)value_));
            }
            return scope.Escape(Nan::New((double)value_));
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            if (value_ >= std::numeric_limits<long>::min() &&
                value_ <= std::numeric_limits<long>::max()) {
                RETURN_LONG((long)value_);
            }
            RETURN_DOUBLE((double)value_);
        }
    };
    class Double : public Prim<double> {
    public:
        using Prim::Prim;
        virtual const char *TypeString() const { return "Double"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(value_));
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            RETURN_DOUBLE(value_);
        }
    };
    enum OwnerType { NOT_OWNED, PHP_OWNED, CPP_OWNED };
    class Str : public Base {
    protected:
        const char *data_;
        std::size_t length_;
        virtual OwnerType Owner() { return NOT_OWNED; }
    public:
        explicit Str(const char *data, std::size_t length)
            : data_(data), length_(length) { }
        virtual ~Str() {
            if (data_) {
                switch (Owner()) {
                case NOT_OWNED: break;
                case PHP_OWNED: efree((void*)data_); break;
                case CPP_OWNED: delete[] data_; break;
                }
            }
        }
        virtual const char *TypeString() const { return "Str"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::New(data_, length_).ToLocalChecked());
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            RETURN_STRINGL(data_, length_, 1);
        }
        virtual std::string ToString() const {
            std::stringstream ss;
            ss << TypeString() << "(" << length_ << ",";
            if (length_ > 10) {
                ss << std::string(data_, 7) << "...";
            } else {
                ss << std::string(data_, length_);
            }
            ss << ")";
            return ss.str();
        }
    };
    class OStr : public Str {
        // an "owned string", will copy data on creation and free it on delete.
    public:
        explicit OStr(const char *data, std::size_t length)
            : Str(NULL, length) {
            char *ndata = new char[length+1];
            memcpy(ndata, data, length);
            ndata[length] = 0;
            data_ = ndata;
        }
        virtual const char *TypeString() const { return "OStr"; }
    protected:
        virtual OwnerType Owner() { return CPP_OWNED; }
    };
    class Buf : public Str {
    public:
        Buf(const char *data, std::size_t length) : Str(data, length) { }
        virtual const char *TypeString() const { return "Buf"; }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(Nan::CopyBuffer(data_, length_).ToLocalChecked());
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            node_php_jsbuffer_create(return_value, data_, length_, 1 TSRMLS_CC);
        }
    };
    class OBuf : public Buf {
    public:
        OBuf(const char *data, std::size_t length) : Buf(NULL, length) {
            char *tmp = new char[length];
            memcpy(tmp, data, length);
            data_ = tmp;
        }
        virtual const char *TypeString() const { return "OBuf"; }
    protected:
        virtual OwnerType Owner() { return CPP_OWNED; }
    };
    class Obj : public Base {
        objid_t id_;
    public:
        explicit Obj(objid_t id) : id_(id) { }
        virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
            Nan::EscapableHandleScope scope;
            return scope.Escape(m->JsObjForId(id_));
        }
        virtual void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) const {
            zval_ptr_dtor(&return_value);
            *return_value_ptr = return_value = m->PhpObjForId(id_ TSRMLS_CC);
            // objectmapper owns the reference returned, but we need a
            // reference owned by the caller.  so increment reference count.
            Z_ADDREF_P(return_value);
        }
        virtual std::string ToString() const {
            std::stringstream ss;
            ss << TypeString() << "(" << id_ << ")";
            return ss.str();
        }
    };
    class JsObj : public Obj {
    public:
        using Obj::Obj;
        explicit JsObj(JsObjectMapper *m, v8::Local<v8::Object> o)
            : Obj(m->IdForJsObj(o)) { }
        virtual const char *TypeString() const { return "JsObj"; }
    };
    class PhpObj : public Obj {
    public:
        using Obj::Obj;
        explicit PhpObj(PhpObjectMapper *m, zval *o)
            : Obj(m->IdForPhpObj(o)) { }
        virtual const char *TypeString() const { return "PhpObj"; }
    };

 public:
    explicit Value() : type_(VALUE_EMPTY), empty_(0) { }
    virtual ~Value() { PerhapsDestroy(); }

    explicit Value(JsObjectMapper *m, v8::Local<v8::Value> v) : type_(VALUE_EMPTY), empty_(0) {
        Set(m, v);
    }
    explicit Value(PhpObjectMapper *m, zval *v TSRMLS_DC) : type_(VALUE_EMPTY), empty_(0) {
        Set(m, v TSRMLS_CC);
    }
    template <typename T>
    static Value *NewArray(PhpObjectMapper *m, int argc, T* argv TSRMLS_DC) {
        Value *result = new Value[argc];
        for (int i=0; i<argc; i++) {
            result[i].Set(m, argv[i] TSRMLS_CC);
        }
        return result;
    }
    void Set(JsObjectMapper *m, v8::Local<v8::Value> v) {
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
        } else if (node::Buffer::HasInstance(v)) {
            SetOwnedBuffer(node::Buffer::Data(v), node::Buffer::Length(v));
            return;
        } else if (v->IsObject()) {
            SetJsObject(m, Nan::To<v8::Object>(v).ToLocalChecked());
            return;
        }
        // Null for all other object types.
        SetNull();
    }
    void Set(PhpObjectMapper *m, zval *v TSRMLS_DC) {
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
            // special case JsBuffer
            if (Z_OBJCE_P(v) == php_ce_jsbuffer) {
                node_php_jsbuffer *b = (node_php_jsbuffer *)
                    zend_object_store_get_object(v TSRMLS_CC);
                // since PHP blocks, it is fine to let PHP
                // own the buffer; avoids needless copying
                SetBuffer(b->data, b->length);
                return;
            }
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
    void SetBuffer(const char *data, std::size_t length) {
        PerhapsDestroy();
        type_ = VALUE_BUF;
        new (&buf_) Buf(data, length);
    }
    void SetOwnedBuffer(const char *data, std::size_t length) {
        PerhapsDestroy();
        type_ = VALUE_OBUF;
        new (&obuf_) OBuf(data, length);
    }
    void SetJsObject(JsObjectMapper *m, v8::Local<v8::Object> o) {
        SetJsObject(m->IdForJsObj(o));
    }
    void SetJsObject(objid_t id) {
        PerhapsDestroy();
        type_ = VALUE_JSOBJ;
        new (&jsobj_) JsObj(id);
    }
    void SetPhpObject(PhpObjectMapper *m, zval *o) {
        SetPhpObject(m->IdForPhpObj(o));
    }
    void SetPhpObject(objid_t id) {
        PerhapsDestroy();
        type_ = VALUE_PHPOBJ;
        new (&phpobj_) PhpObj(id);
    }

    v8::Local<v8::Value> ToJs(JsObjectMapper *m) {
        // should we create a new escapablehandlescope here?
        return AsBase().ToJs(m);
    }
    // caller owns the zval
    void ToPhp(PhpObjectMapper *m, zval *return_value, zval **return_value_ptr TSRMLS_DC) {
        AsBase().ToPhp(m, return_value, return_value_ptr TSRMLS_CC);
    }
    // caller owns the ZVal, and is responsible for freeing it.
    inline void ToPhp(PhpObjectMapper *m, ZVal &z TSRMLS_DC) {
        ToPhp(m, z.Ptr(), z.PtrPtr() TSRMLS_CC);
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
    /* For debugging: describe the value. Caller implicitly deallocates. */
    std::string ToString() {
        return AsBase().ToString();
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
        VALUE_STR, VALUE_OSTR, VALUE_BUF, VALUE_OBUF,
        VALUE_JSOBJ, VALUE_PHPOBJ
    } type_;
    union {
        int empty_; Null null_; Bool bool_; Int int_; Double double_;
        Str str_; OStr ostr_; Buf buf_; OBuf obuf_;
        JsObj jsobj_; PhpObj phpobj_;
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
        case VALUE_BUF:
            return buf_;
        case VALUE_OBUF:
            return obuf_;
        case VALUE_JSOBJ:
            return jsobj_;
        case VALUE_PHPOBJ:
            return phpobj_;
        }
    }
};

}
#endif
