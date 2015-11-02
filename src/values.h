// Value union type for representing values in messages in an
// engine-independent manner.
// Also: a ZVal helper for managing zvals on the stack.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_VALUES_H_
#define NODE_PHP_EMBED_VALUES_H_

#include <cassert>
#include <cstdlib>

#include <limits>
#include <new>
#include <sstream>
#include <string>

#include "nan.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend_interfaces.h"  // for zend_call_method_with_*
}

#include "src/macros.h"
#include "src/node_php_jsbuffer_class.h"  // ...to recognize buffers in PHP land
#include "src/node_php_jswait_class.h"  // ...to recognize JsWait in PHP land

namespace node_php_embed {

// The integer size used for "object identifiers" shared between threads.
typedef uint32_t objid_t;

// Methods in JsObjectMapper are/should be accessed only from the JS thread.
// The mapper will hold persistent references to the objects for which it
// has ids.
class JsObjectMapper {
 public:
  virtual ~JsObjectMapper() { }
  virtual objid_t IdForJsObj(const v8::Local<v8::Object> o) = 0;
  virtual v8::Local<v8::Object> JsObjForId(objid_t id) = 0;
};

// Methods in PhpObjectMapper are/should be accessed only from the PHP thread.
// The mapper will hold references for the zvals it returns.
class PhpObjectMapper {
 public:
  virtual ~PhpObjectMapper() { }
  virtual objid_t IdForPhpObj(zval *o) = 0;
  // Returned value is owned by PhpObjectMapper, caller should not
  // release it.
  virtual zval * PhpObjForId(objid_t id TSRMLS_DC) = 0;
};

// An ObjectMapper is used by both threads, so inherits both interfaces.
class ObjectMapper : public JsObjectMapper, public PhpObjectMapper {
 public:
  virtual ~ObjectMapper() { }
  // Allow clients to ask whether the mapper has been shut down.
  virtual bool IsValid() = 0;
};

/** Allocation helper for PHP zval objects. */
class ZVal {
 public:
  explicit ZVal(ZEND_FILE_LINE_D) : transferred_(false) {
    ALLOC_ZVAL_REL(zvalp);
    INIT_ZVAL(*zvalp);
  }
  explicit ZVal(zval *z ZEND_FILE_LINE_DC) : zvalp(z), transferred_(false) {
    if (zvalp) {
      Z_ADDREF_P(zvalp);
    } else {
      ALLOC_ZVAL_REL(zvalp);
      INIT_ZVAL(*zvalp);
    }
  }
  explicit ZVal(ZVal &&other)  // NOLINT(build/c++11)
      : zvalp(other.zvalp), transferred_(other.transferred_) {
    other.zvalp = NULL;
  }
  virtual ~ZVal() {
    if (!zvalp) {
      return;  // Move constructor was used
    } else if (transferred_) {
      efree(zvalp);
    } else {
      zval_ptr_dtor(&zvalp);
    }
  }
  inline zval * Ptr() const { return zvalp; }
  inline zval ** PtrPtr() { return &zvalp; }
  inline zval * Escape() { Z_ADDREF_P(zvalp); return zvalp; }

  // Ensure an unshared copy of this value.
  inline void Separate() {
    assert(!transferred_);
    SEPARATE_ZVAL_IF_NOT_REF(&zvalp);
  }
  // A static version that will work on unwrapped zval*
  static inline zval *Separate(zval *z) {
    SEPARATE_ZVAL_IF_NOT_REF(&z);
    return z;
  }

  // Unwrap references protected by Js\ByRef
  inline void UnwrapByRef(TSRMLS_D) {
    if (!IsObject()) { return; }
    assert(zvalp && !transferred_);
    zend_class_entry *ce = Z_OBJCE_P(zvalp);
    // XXX cache the zend_class_entry at startup so we can do a simple
    // pointer comparison instead of looking at the class name
    if (ce->name_length == 8 && strcmp("Js\\ByRef", ce->name) == 0) {
      // Unwrap!
      zval *rv;
      zend_call_method_with_0_params(&zvalp, nullptr, nullptr, "getValue", &rv);
      if (rv) {
        zval_ptr_dtor(&zvalp);
        zvalp = rv;
      }
    }
  }

  // Support a PHP calling convention where the actual zval object
  // is owned by the caller, but the contents are transferred to the
  // callee.
  inline zval * Transfer(TSRMLS_D) {
    if (IsObject()) {
      zend_objects_store_add_ref(zvalp TSRMLS_CC);
    } else {
      transferred_ = true;
    }
    return zvalp;
  }
  inline zval * operator*() const { return Ptr(); }  // Shortcut operator.
  inline int Type() const { return Z_TYPE_P(zvalp); }
  inline bool IsNull() const { return Type() == IS_NULL; }
  inline bool IsBool() const { return Type() == IS_BOOL; }
  inline bool IsLong() const { return Type() == IS_LONG; }
  inline bool IsDouble() const { return Type() == IS_DOUBLE; }
  inline bool IsString() const { return Type() == IS_STRING; }
  inline bool IsArray() const { return Type() == IS_ARRAY; }
  inline bool IsObject() const { return Type() == IS_OBJECT; }
  inline bool IsResource() const { return Type() == IS_RESOURCE; }
  inline bool IsUninitialized(TSRMLS_D) const {
    return zvalp == EG(uninitialized_zval_ptr);
  }
  inline void Set(zval *z ZEND_FILE_LINE_DC) {
    zval_ptr_dtor(&zvalp);
    zvalp = z;
    if (zvalp) {
      Z_ADDREF_P(zvalp);
    } else {
      ALLOC_ZVAL_REL(zvalp);
      INIT_ZVAL(*zvalp);
    }
  }
  inline void SetNull() { PerhapsDestroy(); ZVAL_NULL(zvalp); }
  inline void SetBool(bool b) { PerhapsDestroy(); ZVAL_BOOL(zvalp, b ? 1 : 0); }
  inline void SetLong(long l) {  // NOLINT(runtime/int)
    PerhapsDestroy();
    ZVAL_LONG(zvalp, l);
  }
  inline void SetDouble(double d) { PerhapsDestroy(); ZVAL_DOUBLE(zvalp, d); }
  inline void SetString(const char *str, int len, bool dup) {
    PerhapsDestroy();
    ZVAL_STRINGL(zvalp, str, len, dup);
  }
  inline void SetStringConstant(const char *str) {
    PerhapsDestroy();
    ZVAL_STRINGL(zvalp, str, strlen(str), 0);
    transferred_ = true;
  }

 private:
  void PerhapsDestroy() {
    if (!transferred_) {
      zval_dtor(zvalp);
    }
    transferred_ = false;
  }

  zval *zvalp;
  bool transferred_;
  NAN_DISALLOW_ASSIGN_COPY(ZVal)
};

/* A poor man's tagged union, so that we can stack allocate messages
 * containing values without having to pay for heap allocation.
 * It also provides safe storage for values independent of the PHP or
 * JS runtimes.
 */
class Value {
  class Base {
   public:
    Base() { }
    virtual ~Base() { }
    virtual v8::Local<v8::Value> ToJs(JsObjectMapper *m) const = 0;
    virtual void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const = 0;
    /* For debugging.  The returned value should not be deallocated. */
    virtual const char *TypeString() const = 0;
    /* For debugging purposes. Caller (implicitly) deallocates. */
    virtual std::string ToString() const {
      return std::string(TypeString());
    }
    NAN_DISALLOW_ASSIGN_COPY_MOVE(Base)
  };
  class Null : public Base {
   public:
    Null() { }
    const char *TypeString() const override { return "Null"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(Nan::Null());
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      RETURN_NULL();
    }
  };
  template <class T>
  class Prim : public Base {
    friend class Value;
   public:
    explicit Prim(T value) : value_(value) { }
    std::string ToString() const override {
      std::stringstream ss;
      ss << TypeString() << "(" << value_ << ")";
      return ss.str();
    }

   private:
    T value_;
  };
  class Bool : public Prim<bool> {
   public:
    using Prim::Prim;
    const char *TypeString() const override { return "Bool"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(Nan::New(value_));
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      RETURN_BOOL(value_);
    }
  };
  class Int : public Prim<int64_t> {
   public:
    using Prim::Prim;
    const char *TypeString() const override { return "Int"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      if (value_ >= 0 && value_ <= std::numeric_limits<uint32_t>::max()) {
        return scope.Escape(Nan::New((uint32_t)value_));
      } else if (value_ >= std::numeric_limits<int32_t>::min() &&
                 value_ <= std::numeric_limits<int32_t>::max()) {
        return scope.Escape(Nan::New((int32_t)value_));
      }
      return scope.Escape(Nan::New(static_cast<double>(value_)));
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      if (value_ >= std::numeric_limits<long>::min() &&  // NOLINT(runtime/int)
          value_ <= std::numeric_limits<long>::max()) {  // NOLINT(runtime/int)
        RETURN_LONG((long)value_);                       // NOLINT(runtime/int)
      }
      RETURN_DOUBLE((double)value_);
    }
  };
  class Double : public Prim<double> {
   public:
    using Prim::Prim;
    const char *TypeString() const override { return "Double"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(Nan::New(value_));
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      RETURN_DOUBLE(value_);
    }
  };
  enum OwnerType { NOT_OWNED, PHP_OWNED, CPP_OWNED };
  class Str : public Base {
    friend class Value;
   protected:
    const char *data_;
    std::size_t length_;
    virtual OwnerType Owner() const { return NOT_OWNED; }

   public:
    explicit Str(const char *data, std::size_t length)
        : data_(data), length_(length) { }
    virtual ~Str() { Destroy(); }
    const char *TypeString() const override { return "Str"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(Nan::New(data_, length_).ToLocalChecked());
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      // If we ever wanted to set `dup=0`, we'd need to ensure that the
      // data was null-terminated, since Buffers aren't, necessarily,
      // and PHP expects null-terminated strings.
      RETURN_STRINGL(data_, length_, 1);
    }
    std::string ToString() const override {
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

   private:
    void Destroy() {
      if (data_ == nullptr) { return; }
      switch (Owner()) {
      case NOT_OWNED: break;
      case PHP_OWNED: efree(const_cast<char*>(data_)); break;
      case CPP_OWNED: delete[] data_; break;
      }
      data_ = nullptr;
    }
  };
  class OStr : public Str {
    // An "owned string", will copy data on creation and free it on delete.
   public:
    explicit OStr(const char *data, std::size_t length)
        : Str(nullptr, length) {
      char *ndata = new char[length + 1];
      memcpy(ndata, data, length);
      ndata[length] = 0;
      data_ = ndata;
    }
    virtual ~OStr() { Destroy(); }
    const char *TypeString() const override { return "OStr"; }
   protected:
    OwnerType Owner() const override { return CPP_OWNED; }
  };
  class Buf : public Str {
    friend class Value;
   public:
    Buf(const char *data, std::size_t length) : Str(data, length) { }
    virtual ~Buf() { Destroy(); }
    const char *TypeString() const override { return "Buf"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(Nan::CopyBuffer(data_, length_).ToLocalChecked());
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      node_php_jsbuffer_create(return_value, data_, length_,
                               OwnershipType::PHP_OWNED TSRMLS_CC);
    }
  };
  class OBuf : public Buf {
   public:
    OBuf(const char *data, std::size_t length) : Buf(nullptr, length) {
      char *tmp = new char[length];
      memcpy(tmp, data, length);
      data_ = tmp;
    }
    virtual ~OBuf() { Destroy(); }
    const char *TypeString() const override { return "OBuf"; }
   protected:
    OwnerType Owner() const override { return CPP_OWNED; }
  };
  class Obj : public Base {
    objid_t id_;
   public:
    explicit Obj(objid_t id) : id_(id) { }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      return scope.Escape(m->JsObjForId(id_));
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      zval_ptr_dtor(&return_value);
      *return_value_ptr = return_value = m->PhpObjForId(id_ TSRMLS_CC);
      // The object mapper owns the reference returned, but we need a
      // reference owned by the caller --- so increment reference count.
      Z_ADDREF_P(return_value);
    }
    std::string ToString() const override {
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
    const char *TypeString() const override { return "JsObj"; }
  };
  class PhpObj : public Obj {
   public:
    using Obj::Obj;
    explicit PhpObj(PhpObjectMapper *m, zval *o)
        : Obj(m->IdForPhpObj(o)) { }
    const char *TypeString() const override { return "PhpObj"; }
  };
  // Wait objects are empty marker values used to indicate that
  // the callee should substitute a node-style callback function
  // for this value.
  class Wait : public Base {
   public:
    Wait() { }
    const char *TypeString() const override { return "Wait"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      // Default serialize as a null for safety; the MessageToJs should
      // handle this specially by calling MessageToJs::MakeCallback() and
      // replacing the value.
      return scope.Escape(Nan::Null());
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      node_php_jswait_create(return_value TSRMLS_CC);
    }
  };
  // Method thunks are empty marker values which are returned to
  // signal that the caller should create a callback thunk.
  // (That is, that the named property is a method on the PHP side.)
  class MethodThunk : public Base {
   public:
    MethodThunk() { }
    const char *TypeString() const override { return "MethodThunk"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      assert(false); /* should never reach here */
      return scope.Escape(Nan::Undefined());
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
                       zval **return_value_ptr TSRMLS_DC) const override {
      assert(false); /* should never reach here */
      RETURN_NULL();
    }
  };
  // Normally arrays are passed "by reference" between Node and PHP;
  // that is, they are wrapped in proxies and the actual manipulation
  // happens on the "host" side.  However, for implementing certain
  // messages (invocations with variable arguments, property enumeration)
  // it can be useful to transfer multiple Value objects as a single
  // Value.  This type allows that, and it provides ToJs and ToPhp
  // implementations that create appropriate "native" arrays.
  // However `ArrayByValue` is never created by the `Set` methods
  // which convert native values; it is only created explicitly for
  // internal use.
  class ArrayByValue : public Base {
    friend class Value;
    uint32_t length_;
    Value *item_;

   public:
    explicit ArrayByValue(uint32_t length)
        : length_(length), item_(new Value[length]) { }
    virtual ~ArrayByValue() { delete[] item_; }
    const char *TypeString() const override { return "ArrayByValue"; }
    v8::Local<v8::Value> ToJs(JsObjectMapper *m) const override {
      Nan::EscapableHandleScope scope;
      v8::Local<v8::Array> arr = Nan::New<v8::Array>(length_);
      for (uint32_t i = 0; i < length_; i++) {
        Nan::Set(arr, i, item_[i].ToJs(m));
      }
      return scope.Escape(arr);
    }
    void ToPhp(PhpObjectMapper *m, zval *return_value,
               zval **return_value_ptr TSRMLS_DC) const override {
      array_init(return_value);
      for (uint32_t i = 0; i < length_; i++) {
        ZVal item{ZEND_FILE_LINE_C};
        item_[i].ToPhp(m, item TSRMLS_CC);
        add_index_zval(return_value, i, item.Escape());
      }
    }
    std::string ToString() const override {
      std::stringstream ss;
      ss << TypeString() << "[" << length_ << "](";
      for (uint32_t i = 0; i < length_ && i < 10; i++) {
        if (i > 0) { ss << ", "; }
        ss << item_[i].ToString();
      }
      if (length_ > 10) { ss << ", ..."; }
      ss << ")";
      return ss.str();
    }
  };

 public:
  Value() : type_(VALUE_EMPTY), empty_(0) { }
  virtual ~Value() { PerhapsDestroy(); }

  explicit Value(JsObjectMapper *m, v8::Local<v8::Value> v)
      : type_(VALUE_EMPTY), empty_(0) {
    Set(m, v);
  }
  explicit Value(PhpObjectMapper *m, zval *v TSRMLS_DC)
      : type_(VALUE_EMPTY), empty_(0) {
    Set(m, v TSRMLS_CC);
  }
  template <typename T>
  static Value *NewArray(PhpObjectMapper *m, int argc, T* argv TSRMLS_DC) {
    Value *result = new Value[argc];
    for (int i = 0; i < argc; i++) {
      result[i].Set(m, argv[i] TSRMLS_CC);
    }
    return result;
  }
  static Value *NewArray(JsObjectMapper *m,
                         const Nan::FunctionCallbackInfo<v8::Value> &info) {
    Value *result = new Value[info.Length()];
    for (int i = 0; i < info.Length(); i++) {
      result[i].Set(m, info[i]);
    }
    return result;
  }
  void Set(JsObjectMapper *m, v8::Local<v8::Value> v) {
    if (v->IsUndefined() || v->IsNull()) {
      /* Fall through to the default case. */
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
  inline void Set(PhpObjectMapper *m, const ZVal &z TSRMLS_DC) {
    Set(m, z.Ptr() TSRMLS_CC);
  }
  void Set(PhpObjectMapper *m, const zval *v TSRMLS_DC) {
    switch (Z_TYPE_P(v)) {
    default:
    case IS_NULL:
      SetNull();
      return;
    case IS_BOOL:
      SetBool(Z_BVAL_P(v));
      return;
    case IS_LONG:
      long l;  // NOLINT(runtime/int)
      l = Z_LVAL_P(v);
      if (l >= std::numeric_limits<int32_t>::min() &&
          l <= (int64_t)std::numeric_limits<uint32_t>::max()) {
        SetInt((int64_t)l);
      } else {
        SetDouble(static_cast<double>(l));
      }
      return;
    case IS_DOUBLE:
      SetDouble(Z_DVAL_P(v));
      return;
    case IS_STRING:
      // Since PHP blocks, it is fine to let PHP
      // own the buffer; avoids needless copying.
      SetString(Z_STRVAL_P(v), Z_STRLEN_P(v));
      return;
    case IS_OBJECT:
      // Special case for JsBuffer wrappers.
      if (Z_OBJCE_P(v) == php_ce_jsbuffer) {
        node_php_jsbuffer *b = reinterpret_cast<node_php_jsbuffer *>
          (zend_object_store_get_object(v TSRMLS_CC));
        // Since PHP blocks, it is fine to let PHP
        // own the buffer; avoids needless copying.
        SetBuffer(b->data, b->length);
        return;
      }
      // Special case for JsWait objects.
      if (Z_OBJCE_P(v) == php_ce_jswait) {
        SetWait();
        return;
      }
      SetPhpObject(m, v);
      return;
    case IS_ARRAY:
      SetPhpObject(m, v);
      return;
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
  void SetPhpObject(PhpObjectMapper *m, const zval *o) {
    SetPhpObject(m->IdForPhpObj(const_cast<zval*>(o)));
  }
  void SetPhpObject(objid_t id) {
    PerhapsDestroy();
    type_ = VALUE_PHPOBJ;
    new (&phpobj_) PhpObj(id);
  }
  void SetWait() {
    PerhapsDestroy();
    type_ = VALUE_WAIT;
    new (&wait_) Wait();
  }
  void SetMethodThunk() {
    PerhapsDestroy();
    type_ = VALUE_METHOD_THUNK;
    new (&method_thunk_) MethodThunk();
  }
  template<typename Func>
  void SetArrayByValue(uint32_t length, Func func) {
    PerhapsDestroy();
    type_ = VALUE_ARRAY_BY_VALUE;
    new (&array_by_value_) ArrayByValue(length);
    for (uint32_t i = 0; i < length; i++) {
      func(i, array_by_value_.item_[i]);
    }
  }

  // Helper.
  void SetConstantString(const char *str) {
      SetString(str, strlen(str));
  }

  v8::Local<v8::Value> ToJs(JsObjectMapper *m) const {
    return AsBase().ToJs(m);
  }
  // The caller owns the zval.
  void ToPhp(PhpObjectMapper *m, zval *return_value,
             zval **return_value_ptr TSRMLS_DC) const {
    AsBase().ToPhp(m, return_value, return_value_ptr TSRMLS_CC);
  }
  // Caller owns the ZVal, and is responsible for freeing it.
  inline void ToPhp(PhpObjectMapper *m, ZVal &z TSRMLS_DC) const {
    if (!z.IsNull()) { z.SetNull(); /* deallocate previous value */ }
    ToPhp(m, z.Ptr(), z.PtrPtr() TSRMLS_CC);
  }
  inline bool IsEmpty() const {
    return (type_ == VALUE_EMPTY);
  }
  inline bool IsWait() const {
    return (type_ == VALUE_WAIT);
  }
  inline bool IsMethodThunk() const {
    return (type_ == VALUE_METHOD_THUNK);
  }
  inline bool IsArrayByValue() const {
    return (type_ == VALUE_ARRAY_BY_VALUE);
  }
  inline Value& operator[](int i) const {
    assert(IsArrayByValue());
    return array_by_value_.item_[i];
  }
  bool AsBool() const {
    switch (type_) {
    case VALUE_BOOL:
      return bool_.value_;
    case VALUE_INT:
      return int_.value_ != 0;
    default:
      return false;
    }
  }
  // Convert unowned values into owned values so the caller can disappear.
  void TakeOwnership() {
    switch (type_) {
    case VALUE_STR:
      SetOwnedString(str_.data_, str_.length_);
      break;
    case VALUE_BUF:
      SetOwnedBuffer(buf_.data_, buf_.length_);
      break;
    case VALUE_ARRAY_BY_VALUE:
      for (uint32_t i = 0; i < array_by_value_.length_; i++) {
        array_by_value_.item_[i].TakeOwnership();
      }
      break;
    default:
      break;
    }
  }
  /* For debugging: describe the value. Caller implicitly deallocates. */
  std::string ToString() const {
    if (IsEmpty()) {
      return std::string("Empty");
    } else {
      return AsBase().ToString();
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
    VALUE_STR, VALUE_OSTR, VALUE_BUF, VALUE_OBUF,
    VALUE_JSOBJ, VALUE_PHPOBJ,
    VALUE_WAIT, VALUE_METHOD_THUNK, VALUE_ARRAY_BY_VALUE
  } type_;
  union {
    int empty_; Null null_; Bool bool_; Int int_; Double double_;
    Str str_; OStr ostr_; Buf buf_; OBuf obuf_;
    JsObj jsobj_; PhpObj phpobj_;
    Wait wait_; MethodThunk method_thunk_;
    ArrayByValue array_by_value_;
  };

  const Base &AsBase() const {
    switch (type_) {
    default:
      assert(false);  // Should never get here.
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
    case VALUE_WAIT:
      return wait_;
    case VALUE_METHOD_THUNK:
      return method_thunk_;
    case VALUE_ARRAY_BY_VALUE:
      return array_by_value_;
    }
  }
  NAN_DISALLOW_ASSIGN_COPY_MOVE(Value)
};

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_VALUES_H_
