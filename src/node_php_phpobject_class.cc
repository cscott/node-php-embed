// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_phpobject_class.h"

#include <cassert>
#define __STDC_FORMAT_MACROS  // Sometimes necessary to get PRIu32
#include <cinttypes>  // For PRIu32
#include <cstdio>  // For snprintf
#include <vector>

#include "nan.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"  // for zend_call_method_with_*
#include "Zend/zend_types.h"
#include "ext/standard/php_string.h"  // for php_strtolower
}

#include "src/macros.h"
#include "src/messages.h"

// For some reason, travis still doesn't like PRIu32.:
#ifndef PRIu32
# warning "Your compiler seems to be broken."
# define PRIu32 "u"
#endif

#define THROW_IF_EXCEPTION(fallback_msg, defaultValue)                  \
  do {                                                                  \
    if (msg.HasException()) {                                           \
      v8::Local<v8::Value> e = msg.exception().ToJs(channel_);          \
      Nan::MaybeLocal<v8::String> msg_str = Nan::To<v8::String>(e);     \
      if (msg_str.IsEmpty()) {                                          \
        Nan::ThrowError(fallback_msg);                                  \
      } else {                                                          \
        Nan::ThrowError(msg_str.ToLocalChecked());                      \
      }                                                                 \
      return defaultValue;                                              \
    }                                                                   \
  } while (0)

namespace node_php_embed {

v8::Local<v8::Object> PhpObject::Create(MapperChannel *channel,
                                        objid_t id) {
  Nan::EscapableHandleScope scope;
  PhpObject *obj = new PhpObject(channel, id);
  v8::Local<v8::Value> argv[] = { Nan::New<v8::External>(obj) };
  return scope.Escape(constructor()->NewInstance(1, argv));
}

void PhpObject::MaybeNeuter(MapperChannel *channel, v8::Local<v8::Object> obj) {
  v8::Local<v8::FunctionTemplate> t = Nan::New(cons_template());
  if (!t->HasInstance(obj)) {
    // Nope, not a PhpObject.
    return;
  }
  PhpObject *p = Unwrap<PhpObject>(obj);
  if (p->channel_ != channel) {
    // Already neutered, or else not from this request.
    return;
  }
  p->channel_ = nullptr;
  p->id_ = 0;
}

NAN_MODULE_INIT(PhpObject::Init) {
  v8::Local<v8::String> class_name = NEW_STR("PhpObject");
  v8::Local<v8::FunctionTemplate> tpl =
    Nan::New<v8::FunctionTemplate>(PhpObject::New);
  tpl->SetClassName(class_name);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  cons_template().Reset(tpl);
  Nan::SetNamedPropertyHandler(tpl->InstanceTemplate(),
                               PhpObject::PropertyGet,
                               PhpObject::PropertySet,
                               PhpObject::PropertyQuery,
                               PhpObject::PropertyDelete,
                               PhpObject::PropertyEnumerate);
  Nan::SetIndexedPropertyHandler(tpl->InstanceTemplate(),
                                 PhpObject::IndexGet,
                                 PhpObject::IndexSet,
                                 PhpObject::IndexQuery,
                                 PhpObject::IndexDelete,
                                 PhpObject::IndexEnumerate);
  Nan::Set(target, class_name, constructor());
}

PhpObject::~PhpObject() {
  // XXX remove from mapping table, notify PHP.
  TRACE("JS deallocate");
}

NAN_METHOD(PhpObject::New) {
  if (!info.IsConstructCall()) {
    return Nan::ThrowTypeError("You must use `new` with this constructor.");
  }
  if (!info[0]->IsExternal()) {
    return Nan::ThrowTypeError("This constructor is for internal use only.");
  }
  TRACE(">");
  // This object was made by PhpObject::Create()
  PhpObject *obj = reinterpret_cast<PhpObject*>
    (v8::Local<v8::External>::Cast(info[0])->Value());
  obj->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
  TRACE("<");
}

NAN_PROPERTY_GETTER(PhpObject::PropertyGet) {
  TRACEX("> %s", *Nan::Utf8String(property));
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Property(PropertyOp::GETTER, property));
  TRACEX("< %s", *Nan::Utf8String(property));
}

NAN_INDEX_GETTER(PhpObject::IndexGet) {
  TRACEX("> [%" PRIu32 "]", index);
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Property(PropertyOp::GETTER, index));
  TRACEX("< [%" PRIu32 "]", index);
}

NAN_PROPERTY_SETTER(PhpObject::PropertySet) {
  TRACEX("> %s", *Nan::Utf8String(property));
  // XXX for async access, it seems like we might be able to lie about
  // what the result of the Set operation is -- that is,
  // `(x = y) !== y`.  Perhaps `x = y` could resolve to a `Promise`?
  // (or else v8 will just ignore our return value.)
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Property(PropertyOp::SETTER, property, value));
  TRACEX("< %s", *Nan::Utf8String(property));
}

NAN_INDEX_SETTER(PhpObject::IndexSet) {
  TRACEX("> [%" PRIu32 "]", index);
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Property(PropertyOp::SETTER, index, value));
  TRACEX("< [%" PRIu32 "]", index);
}

NAN_PROPERTY_DELETER(PhpObject::PropertyDelete) {
  TRACEX("> %s", *Nan::Utf8String(property));
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  v8::Local<v8::Value> r = p->Property(PropertyOp::DELETER, property);
  if (!r.IsEmpty()) {
    info.GetReturnValue().Set(Nan::To<v8::Boolean>(r).ToLocalChecked());
  }
  TRACEX("< %s", *Nan::Utf8String(property));
}

NAN_INDEX_DELETER(PhpObject::IndexDelete) {
  TRACEX("> [%" PRIu32 "]", index);
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  v8::Local<v8::Value> r = p->Property(PropertyOp::DELETER, index);
  if (!r.IsEmpty()) {
    info.GetReturnValue().Set(Nan::To<v8::Boolean>(r).ToLocalChecked());
  }
  TRACEX("< [%" PRIu32 "]", index);
}

NAN_PROPERTY_QUERY(PhpObject::PropertyQuery) {
  TRACEX("> %s", *Nan::Utf8String(property));
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  v8::Local<v8::Value> r = p->Property(PropertyOp::QUERY, property);
  if (!r.IsEmpty()) {
    info.GetReturnValue().Set(Nan::To<v8::Integer>(r).ToLocalChecked());
  }
  TRACEX("< %s", *Nan::Utf8String(property));
}

NAN_INDEX_QUERY(PhpObject::IndexQuery) {
  TRACEX("> [%" PRIu32 "]", index);
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  v8::Local<v8::Value> r = p->Property(PropertyOp::QUERY, index);
  if (!r.IsEmpty()) {
    info.GetReturnValue().Set(Nan::To<v8::Integer>(r).ToLocalChecked());
  }
  TRACEX("< [%" PRIu32 "]", index);
}

NAN_PROPERTY_ENUMERATOR(PhpObject::PropertyEnumerate) {
  TRACE(">");
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Enumerate(EnumOp::ONLY_PROPERTY));
  TRACE("<");
}

NAN_INDEX_ENUMERATOR(PhpObject::IndexEnumerate) {
  TRACE(">");
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Enumerate(EnumOp::ONLY_INDEX));
  TRACE("<");
}

// Helper function, called from PHP only
static bool IsArrayAccess(zval *z TSRMLS_DC) {
  if (Z_TYPE_P(z) != IS_OBJECT) {
    TRACE("false (not object)");
    return false;
  }
  zend_class_entry *ce = Z_OBJCE_P(z);
  bool has_array_access = false;
  bool has_countable = false;
  for (zend_uint i = 0; i < ce->num_interfaces; i++) {
    if (strcmp(ce->interfaces[i]->name, "ArrayAccess") == 0) {
      has_array_access = true;
    }
    if (strcmp(ce->interfaces[i]->name, "Countable") == 0) {
      has_countable = true;
    }
    if (has_array_access && has_countable) {
      // Bail early from loop, don't need to look further.
      TRACE("true");
      return true;
    }
  }
  TRACE("false");
  return false;
}

class PhpObject::PhpEnumerateMsg : public MessageToPhp {
 public:
  PhpEnumerateMsg(ObjectMapper *m, Nan::Callback *callback, bool is_sync,
                  EnumOp op, objid_t obj)
      : MessageToPhp(m, callback, is_sync), op_(op) {
    obj_.SetJsObject(obj);
  }
 protected:
  void InPhp(PhpObjectMapper *m TSRMLS_DC) override {
    ZVal obj{ZEND_FILE_LINE_C};

    obj_.ToPhp(m, obj TSRMLS_CC);
    assert(obj.IsObject() || obj.IsArray());
    bool is_array_access = IsArrayAccess(obj.Ptr() TSRMLS_CC);
    if (obj.IsArray() || is_array_access) {
      return ArrayEnum(m, op_, obj, is_array_access, &retval_, &exception_
                       TSRMLS_CC);
    }
    // XXX unimplemented
    retval_.SetArrayByValue(0, [](uint32_t idx, Value& v) { });
  }
 private:
  Value obj_;
  EnumOp op_;
};

v8::Local<v8::Array> PhpObject::Enumerate(EnumOp which) {
  Nan::EscapableHandleScope scope;
  if (id_ == 0) {
    Nan::ThrowError("Access to PHP request after it has completed.");
    return scope.Escape(v8::Local<v8::Array>());
  }
  PhpEnumerateMsg msg(channel_, nullptr, true,  // Sync call.
                      which, id_);
  channel_->SendToPhp(&msg, MessageFlags::SYNC);
  THROW_IF_EXCEPTION("PHP exception thrown during property enumeration",
                     v8::Local<v8::Array>());
  if (!msg.retval().IsEmpty()) {
    assert(msg.retval().IsArrayByValue());
    return scope.Escape(msg.retval().ToJs(channel_).As<v8::Array>());
  }
  return scope.Escape(v8::Local<v8::Array>());
}

class PhpObject::PhpPropertyMsg : public MessageToPhp {
 public:
  PhpPropertyMsg(ObjectMapper *m, Nan::Callback *callback, bool is_sync,
                 PropertyOp op, objid_t obj, v8::Local<v8::String> name,
                 v8::Local<v8::Value> value, bool is_index)
    : MessageToPhp(m, callback, is_sync), op_(op), name_(m, name),
      is_index_(is_index) {
    obj_.SetJsObject(obj);
    if (!value.IsEmpty()) {
      value_.Set(m, value);
    }
  }

 protected:
  // JS uses an empty return value to indicate lookup should continue
  // up the prototype chain.
  bool IsEmptyRetvalOk() override {
    return (op_ != PropertyOp::SETTER);
  }
  void InPhp(PhpObjectMapper *m TSRMLS_DC) override {
    ZVal obj{ZEND_FILE_LINE_C}, zname{ZEND_FILE_LINE_C};
    ZVal value{ZEND_FILE_LINE_C};

    obj_.ToPhp(m, obj TSRMLS_CC); name_.ToPhp(m, zname TSRMLS_CC);
    assert(obj.IsObject() || obj.IsArray());
    assert(zname.IsString());
    if (!value_.IsEmpty()) {
      value_.ToPhp(m, value TSRMLS_CC);
    }
    // Arrays are handled in a separate method (but the ZVals here will
    // handle the memory management for us).
    bool is_array_access = IsArrayAccess(obj.Ptr() TSRMLS_CC);
    if (obj.IsArray() || is_array_access) {
      return ArrayInPhp(m, obj, is_array_access, zname, value TSRMLS_CC);
    }
    const char *cname = Z_STRVAL_P(*zname);
    uint cname_len = Z_STRLEN_P(*zname);
    const char *method_name;
    uint method_name_len;
    zend_class_entry *scope, *ce;
    zend_function *method_ptr = nullptr;
    zval *php_value;
    ce = scope = Z_OBJCE_P(*obj);

    // Property names with embedded nulls are special to PHP.
    if (cname_len == 0 ||
        cname[0] == '\0' ||
        (cname_len > 1 && cname[0] == '$' && cname[1] == '\0')) {
      exception_.SetConstantString("Attempt to access private property");
      return;
    }
    /* First, check the (case-insensitive) method table */
    char *lower = static_cast<char*>(alloca(cname_len + 1));
    memcpy(lower, cname, cname_len + 1);
    php_strtolower(lower, cname_len);
    method_name = lower;
    method_name_len = cname_len;
    // toString() -> __tostring()
    if (cname_len == 8 && strcmp(cname, "toString") == 0) {
      method_name = ZEND_TOSTRING_FUNC_NAME;
      method_name_len = sizeof(ZEND_TOSTRING_FUNC_NAME) - 1;
    }
    bool is_constructor =
      (cname_len == 11 && strcmp(cname, "constructor") == 0);
    // Fake __call implementation.  If you wanted the PHP method named __call,
    // either use `_Call` (since PHP is case-insensitive), or else use
    // `obj.__call('__call', ...)`.
    bool is_magic_call = (cname_len == 6 && strcmp(cname, "__call") == 0);
    // Leading '$' means property, not method.
    bool is_forced_property = (cname_len > 0 && cname[0] == '$');
    if (is_constructor || is_magic_call ||
        ((!is_forced_property) &&
         zend_hash_find(&ce->function_table, method_name, method_name_len + 1,
                        reinterpret_cast<void**>(&method_ptr)) == SUCCESS &&
         // Allow only public methods:
         ((method_ptr->common.fn_flags & ZEND_ACC_PUBLIC) != 0) &&
         // No __construct, __destruct(), or __clone() functions
         ((method_ptr->common.fn_flags &
           (ZEND_ACC_CTOR|ZEND_ACC_DTOR|ZEND_ACC_CLONE)) == 0))) {
      if (op_ == PropertyOp::GETTER) {
        if (is_constructor) {
          // Don't set a return value here, i.e. indicate that we don't
          // have a special value.  V8 "knows" the constructor anyways
          // (from the template) and will use that.
          retval_.SetEmpty();
        } else {
          if (is_magic_call) {
            // Fake __call implementation
            // If there's an actual PHP __call, then you'd have to invoke
            // it as "__Call" or else use `obj.__call("__call", ...)`
            retval_.SetMethodThunk();
          } else {
            retval_.SetMethodThunk();
          }
        }
      } else if (op_ == PropertyOp::QUERY) {
        // Methods are not enumerable.
        retval_.SetInt(v8::ReadOnly|v8::DontEnum|v8::DontDelete);
      } else if (op_ == PropertyOp::SETTER) {
        // Lie: methods are read-only, don't allow setting this property.
        retval_.Set(m, *value TSRMLS_CC);
        retval_.TakeOwnership();
      } else if (op_ == PropertyOp::DELETER) {
        // Can't delete methods.
        retval_.SetBool(false);  // "property found here, but not deletable"
      } else {
        assert(false);  // Shouldn't reach here!
      }
    } else {
      if (is_forced_property) {
        // This is a property (not a method).
        cname = estrndup(cname + 1, --cname_len);
        zname.SetString(cname, cname_len, 0);
      }
      if (op_ == PropertyOp::GETTER) {
        /* Nope, not a method -- must be a (case-sensitive) property */
        zend_property_info *property_info =
          zend_get_property_info(ce, *zname, 1 TSRMLS_CC);

        if (property_info && property_info->flags & ZEND_ACC_PUBLIC) {
          php_value = zend_read_property(nullptr, obj.Ptr(), cname, cname_len,
                                         true TSRMLS_CC);
          // Special case uninitialized_zval_ptr and return an empty value
          // (indicating that we don't intercept this property) if the
          // property doesn't exist.
          if (php_value == EG(uninitialized_zval_ptr)) {
            retval_.SetEmpty();
          } else {
            retval_.Set(m, php_value TSRMLS_CC);
            retval_.TakeOwnership();
            /* We don't own the reference to php_value... unless the
             * returned refcount was 0, in which case the below code
             * will free it. */
            zval_add_ref(&php_value);
            zval_ptr_dtor(&php_value);
          }
        } else if ((SUCCESS ==
                    zend_hash_find(&ce->function_table, "__get", 6,
                                   reinterpret_cast<void**>(&method_ptr)))
                   /* Allow only public methods */
                   && ((method_ptr->common.fn_flags & ZEND_ACC_PUBLIC) != 0)) {
          /* Okay, let's call __get. */
          zend_call_method_with_1_params(obj.PtrPtr(), ce, nullptr, "__get",
                                         &php_value, zname.Ptr());
          retval_.Set(m, php_value TSRMLS_CC);
          retval_.TakeOwnership();
          zval_ptr_dtor(&php_value);
        } else {
          retval_.SetEmpty();
        }
      } else if (op_ == PropertyOp::SETTER) {
        assert(!value_.IsEmpty());
        zend_property_info *property_info =
          zend_get_property_info(ce, zname.Ptr(), 1 TSRMLS_CC);

        if (property_info && (property_info->flags & ZEND_ACC_PUBLIC) != 0) {
          zend_update_property(scope, obj.Ptr(), cname, cname_len,
                                 value.Ptr() TSRMLS_CC);
          retval_.Set(m, value.Ptr() TSRMLS_CC);
          retval_.TakeOwnership();
        } else if (
            zend_hash_find(&ce->function_table, "__set", 6,
                           reinterpret_cast<void**>(&method_ptr)) == SUCCESS
            /* Allow only public methods */
            && ((method_ptr->common.fn_flags & ZEND_ACC_PUBLIC) != 0)) {
          /* Okay, let's call __set. */
          zend_call_method_with_2_params
            (obj.PtrPtr(), ce, nullptr, "__set",
             &php_value, zname.Ptr(), value.Ptr());
          retval_.Set(m, value.Ptr() TSRMLS_CC);
          retval_.TakeOwnership();
          zval_ptr_dtor(&php_value);
        } else if (property_info) {
          // It's a property, but not a public one.
          // Shouldn't ever get here, since zend_get_property_info() will
          // return NULL if we don't have permission to access the property.
          exception_.SetConstantString("Attempt to set private property");
        } else {
          // Okay, we need to be a little careful here:
          // zend_get_property_info() returned NULL, but that may have
          // been because there was a private property of this name.
          // We don't want to create a new property shadowing it, that
          // would effectively defeat the access modifier.  So we need
          // to redo a bit of the acces control checks from
          // zend_get_property_info to ensure it's safe to create a
          // new property.
          if (zend_hash_find(&ce->properties_info, cname, cname_len + 1,
                             reinterpret_cast<void**>(&property_info)) ==
              SUCCESS) {
            // Hm, we found the property but zend_get_property_info
            // told us it wasn't here.  Must be an access issue.
            // Silently refuse to set the value.
          } else {
            // Doesn't exist yet, create it!
            zend_update_property(scope, obj.Ptr(), cname, cname_len,
                                 value.Ptr() TSRMLS_CC);
          }
          retval_.Set(m, value.Ptr() TSRMLS_CC);
          retval_.TakeOwnership();
        }
      } else if (op_ == PropertyOp::QUERY) {
        const zend_object_handlers *h = Z_OBJ_HT_P(obj.Ptr());
        if (h->has_property(obj.Ptr(), zname.Ptr(), 0
                            ZEND_HASH_KEY_NULL TSRMLS_CC)) {
          retval_.SetInt(v8::None);
        } else {
          retval_.SetEmpty();  // "property not found here; keep looking"
        }
      } else if (op_ == PropertyOp::DELETER) {
        const zend_object_handlers *h = Z_OBJ_HT_P(obj.Ptr());
        zend_property_info *property_info =
          zend_get_property_info(ce, zname.Ptr(), 1 TSRMLS_CC);

        if (property_info && (property_info->flags & ZEND_ACC_PUBLIC) != 0) {
          h->unset_property(obj.Ptr(), zname.Ptr()
                            ZEND_HASH_KEY_NULL TSRMLS_CC);
          retval_.SetBool(true);
        } else if (
            zend_hash_find(&ce->function_table, "__unset", 8,
                           reinterpret_cast<void**>(&method_ptr)) == SUCCESS
            /* Allow only public methods */
            && ((method_ptr->common.fn_flags & ZEND_ACC_PUBLIC) != 0)) {
          /* Okay, let's call __unset. */
          zend_call_method_with_1_params(obj.PtrPtr(), ce, nullptr, "__unset",
                                         &php_value, zname.Ptr());
          retval_.SetBool(true);
          zval_ptr_dtor(&php_value);
        } else if (property_info) {
          // It's a property, but not a public one.
          // Shouldn't ever get here, since zend_get_property_info() will
          // return NULL if we don't have permission to access the property.
          exception_.SetConstantString("Attempt to unset private property");
        } else {
          retval_.SetEmpty();  // "property not found here; keep looking"
        }
      } else {
        /* shouldn't reach here! */
        assert(false);
        retval_.SetEmpty();
      }
    }
  }
  void ArrayInPhp(PhpObjectMapper *m, const ZVal &arr, bool is_array_access,
                  const ZVal &name, const ZVal &value TSRMLS_DC) {
    assert(is_array_access ? arr.IsObject() : arr.IsArray());
    const char *cname = Z_STRVAL_P(name.Ptr());
    uint cname_len = Z_STRLEN_P(name.Ptr());
    // Length is special
    if (cname_len == 6 && 0 == strcmp(cname, "length")) {
      if (op_ == PropertyOp::QUERY) {
        // Length is not enumerable and not configurable, but *is* writable.
        retval_.SetInt(v8::DontEnum|v8::DontDelete);
      } else if (op_ == PropertyOp::GETTER || op_ == PropertyOp::SETTER) {
        // Length property is "maximum index in hash", not "number of items"
        return PhpObject::ArraySize(m, op_, EnumOp::ONLY_INDEX,
                                    arr, is_array_access, value,
                                    &retval_, &exception_ TSRMLS_CC);
      } else if (op_ == PropertyOp::DELETER) {
        // Can't delete the length property.
        retval_.SetBool(false);  // "property found here, but not deletable"
      } else {
        assert(false);
      }
      return;
    }
    // All-numeric keys are special
    if (is_index_) {
      return PhpObject::ArrayOp(m, op_, arr, is_array_access, name, value,
                                &retval_, &exception_ TSRMLS_CC);
    }
    // Special Map-like methods
    bool map_method = false;
    switch (cname_len) {
    case 3:
      map_method =
        (0 == strcmp(cname, "get") ||
         0 == strcmp(cname, "has") ||
         0 == strcmp(cname, "set"));
      break;
    case 4:
      map_method =
        (0 == strcmp(cname, "size") ||
         0 == strcmp(cname, "keys"));
      break;
    case 6:
      map_method = (0 == strcmp(cname, "delete"));
      break;
    default:
      break;
    }
    if (map_method) {
      if (op_ == PropertyOp::QUERY) {
        // Methods are not enumerable
        retval_.SetInt(v8::ReadOnly|v8::DontEnum|v8::DontDelete);
      } else if (op_ == PropertyOp::GETTER) {
        retval_.SetMethodThunk();
      } else if (op_ == PropertyOp::SETTER) {
        // Lie: methods are read-only, don't allow setting this property.
        retval_.Set(m, value TSRMLS_CC);
        retval_.TakeOwnership();
      } else if (op_ == PropertyOp::DELETER) {
        // Can't delete methods.
        retval_.SetBool(false);  // "property found here, but not deletable"
      } else {
        assert(false);
      }
      return;
    }
    // None of the above.
    if (op_ == PropertyOp::SETTER) {
      retval_.Set(m, value TSRMLS_CC);  // Lie
      retval_.TakeOwnership();
    } else {
      retval_.SetEmpty();  // "Not here, keep looking."
    }
  }

 private:
  PropertyOp op_;
  Value obj_;
  Value name_;
  Value value_;
  bool is_index_;
};

v8::Local<v8::Value> PhpObject::Property(PropertyOp op,
                                         uint32_t index,
                                         v8::Local<v8::Value> new_value) {
  // Convert index to string
  char buf[11];  // strlen("4294967295") == 10, plus null byte
  snprintf(buf, sizeof(buf), "%" PRIu32, index);
  return Property(op, NEW_STR(buf), new_value, true);
}
v8::Local<v8::Value> PhpObject::Property(PropertyOp op,
                                         v8::Local<v8::String> property,
                                         v8::Local<v8::Value> new_value,
                                         bool is_index) {
  Nan::EscapableHandleScope scope;
  // First, check to see if this is a closed object.
  if (id_ == 0) {
    bool silent = (op == PropertyOp::QUERY);
    // Special case requests to get `then`, since that's used when the
    // main request code returns a value via a Promise, and we don't want
    // that to fail until the user actually tries to do something with the
    // returned value.
    if (op == PropertyOp::GETTER && property->Length() == 4 &&
        strcmp("then", *Nan::Utf8String(property)) == 0) {
      silent = true;
    }
    if (!silent) {
      Nan::ThrowError("Access to PHP request after it has completed.");
    }
    return scope.Escape(v8::Local<v8::Value>());
  }
  // XXX For async property access, might make a PromiseResolver
  // and use that to create a callback.
  PhpPropertyMsg msg(channel_, nullptr, true,  // Sync call.
                     op, id_, property, new_value, is_index);
  channel_->SendToPhp(&msg, MessageFlags::SYNC);
  THROW_IF_EXCEPTION("PHP exception thrown during property access",
                     v8::Local<v8::Value>());
  if (msg.retval().IsMethodThunk()) {
    // Create a method thunk
    v8::Local<v8::Array> data = Nan::New<v8::Array>(2);
    Nan::Set(data, 0, handle()).FromJust();
    Nan::Set(data, 1, property).FromJust();
    return scope.Escape(Nan::New<v8::Function>(MethodThunk, data));
  } else if (!msg.retval().IsEmpty()) {
    return scope.Escape(msg.retval().ToJs(channel_));
  }
  return scope.Escape(v8::Local<v8::Value>());
}

class PhpObject::PhpInvokeMsg : public MessageToPhp {
 public:
  PhpInvokeMsg(ObjectMapper *m, Nan::Callback *callback, bool is_sync,
               objid_t obj, v8::Local<v8::String> method,
               const Nan::FunctionCallbackInfo<v8::Value> *info)
      : MessageToPhp(m, callback, is_sync), method_(m, method),
        argc_(info->Length()), argv_(),
        should_convert_array_to_iterator_(false) {
    obj_.SetJsObject(obj);
    argv_.SetArrayByValue(argc_, [m, info](uint32_t idx, Value& v) {
      v.Set(m, (*info)[idx]);
    });
  }
  inline bool should_convert_array_to_iterator() {
    return should_convert_array_to_iterator_;
  }

 protected:
  bool IsEmptyRetvalOk() override {
    // We use the empty value to indicate "undefined", which can't
    // otherwise be represented in PHP.
    return true;
  }
  void InPhp(PhpObjectMapper *m TSRMLS_DC) override {
    ZVal obj{ZEND_FILE_LINE_C}, method{ZEND_FILE_LINE_C};
    obj_.ToPhp(m, obj TSRMLS_CC);
    method_.ToPhp(m, method TSRMLS_CC);
    std::vector<ZVal> args;
    args.reserve(argc_);
    for (int i = 0; i < argc_; i++) {
      args.emplace_back(ZEND_FILE_LINE_C);
      argv_[i].ToPhp(m, args[i] TSRMLS_CC);
    }
    assert(obj.IsObject() || obj.IsArray());
    assert(method.IsString());
    // Arrays are handled in a separate method (but the ZVals here will
    // handle the memory management for us).
    bool is_array_access = IsArrayAccess(obj.Ptr() TSRMLS_CC);
    if (obj.IsArray() || is_array_access) {
      return ArrayInPhp(m, obj, is_array_access,
                        method, args.size(), args.data() TSRMLS_CC);
    }
    ZVal retval{ZEND_FILE_LINE_C};
    // If the method name is __call, then shift the new method name off
    // the front of the argument array.
    zval *name = method.Ptr();
    int first = 0;
    if (Z_STRLEN_P(name) == 6 && 0 == strcmp(Z_STRVAL_P(name), "__call")) {
      if (argc_ == 0 || !args[0].IsString()) {
        zend_throw_exception_ex(
          zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
          "__call needs at least one argument, a string");
        return;
      }
      name = args[0].Ptr(); first = 1;
    }
    std::vector<zval*> nargs(argc_ - first);
    for (int i = first; i < argc_; i++) {
      nargs[i - first] = args[i].Ptr();
    }
    // Ok, now actually invoke that PHP method!
    call_user_function(EG(function_table), obj.PtrPtr(), name,
                       retval.Ptr(), nargs.size(), nargs.data() TSRMLS_CC);
    if (EG(exception)) {
      exception_.Set(m, EG(exception) TSRMLS_CC);
      zend_clear_exception(TSRMLS_C);
    } else {
      retval_.Set(m, retval.Ptr() TSRMLS_CC);
      retval_.TakeOwnership();  // This will outlive scope of `retval`
    }
  }
  void ArrayInPhp(PhpObjectMapper *m, const ZVal &arr, bool is_array_access,
                  const ZVal &name, int argc, ZVal* argv TSRMLS_DC) {
    assert(is_array_access ? arr.IsObject() : arr.IsArray());
    assert(name.IsString());
#define THROW_IF_BAD_ARGS(meth, n)                                       \
    if (argc < n) {                                                      \
      zend_throw_exception_ex(                                           \
        zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,               \
        "%s needs at least %d argument%s", meth, n, (n > 1) ? "s" : ""); \
      return;                                                            \
    }                                                                    \
    if (!argv[0].IsString()) {                                           \
      argv[0].Separate();                                                \
      convert_to_string(argv[0].Ptr());                                  \
    }                                                                    \
    assert(argv[0].IsString())
    const char *cname = Z_STRVAL_P(name.Ptr());
    uint cname_len = Z_STRLEN_P(name.Ptr());
    // Special Map-like methods
    switch (cname_len) {
    case 3:
      if (0 == strcmp(cname, "get")) {
        THROW_IF_BAD_ARGS("get", 1);
        ZVal ignore{ZEND_FILE_LINE_C};
        return PhpObject::ArrayOp(m, PropertyOp::GETTER, arr, is_array_access,
                                  argv[0], ignore, &retval_, &exception_
                                  TSRMLS_CC);
      }
      if (0 == strcmp(cname, "has")) {
        THROW_IF_BAD_ARGS("has", 1);
        ZVal ignore{ZEND_FILE_LINE_C};
        PhpObject::ArrayOp(m, PropertyOp::QUERY, arr, is_array_access,
                           argv[0], ignore, &retval_, &exception_ TSRMLS_CC);
        // return true only if the property exists & is enumerable
        if (retval_.IsEmpty()) {
          retval_.SetBool(false);
        } else {
          retval_.ToPhp(m, ignore TSRMLS_CC);
          assert(ignore.IsLong());
          if ((Z_LVAL_P(ignore.Ptr()) & v8::DontEnum) != 0) {
            retval_.SetBool(false);
          } else {
            retval_.SetBool(true);
          }
        }
        return;
      }
      if (0 == strcmp(cname, "set")) {
        THROW_IF_BAD_ARGS("set", 2);
        return PhpObject::ArrayOp(m, PropertyOp::SETTER, arr, is_array_access,
                                  argv[0], argv[1], &retval_, &exception_
                                  TSRMLS_CC);
      }
      break;
    case 4:
      if (0 == strcmp(cname, "size")) {
        // Size of all items, not just maximum index in hash.
        ZVal ignore{ZEND_FILE_LINE_C};
        return PhpObject::ArraySize(m, PropertyOp::GETTER, EnumOp::ALL,
                                    arr, is_array_access, ignore,
                                    &retval_, &exception_ TSRMLS_CC);
      }
      if (0 == strcmp(cname, "keys")) {
        // Map#keys() should actually return an Iterator, not an array.
        should_convert_array_to_iterator_ = true;
        return PhpObject::ArrayEnum(m, EnumOp::ALL, arr, is_array_access,
                                    &retval_, &exception_ TSRMLS_CC);
      }
      break;
    case 6:
      if (0 == strcmp(cname, "delete")) {
        THROW_IF_BAD_ARGS("delete", 1);
        ZVal ignore{ZEND_FILE_LINE_C};
        PhpObject::ArrayOp(m, PropertyOp::DELETER, arr, is_array_access,
                           argv[0], ignore,
                           &retval_, &exception_ TSRMLS_CC);
        retval_.SetBool(!retval_.IsEmpty());
        return;
      }
      break;
    default:
      break;
    }
    // Shouldn't get here.
    zend_throw_exception_ex(
      zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
      "bad method name");
  }

 private:
  Value obj_;
  Value method_;
  int argc_;
  Value argv_;
  bool should_convert_array_to_iterator_;
};


void PhpObject::MethodThunk(const Nan::FunctionCallbackInfo<v8::Value>& info) {
  v8::Local<v8::Array> data = info.Data().As<v8::Array>();
  v8::Local<v8::Object> obj =
    Nan::Get(data, 0).ToLocalChecked().As<v8::Object>();
  v8::Local<v8::String> method =
    Nan::Get(data, 1).ToLocalChecked().As<v8::String>();
  PhpObject *p = Unwrap<PhpObject>(obj);
  // Do the rest in a non-static method.
  return p->MethodThunk_(method, info);
}
void PhpObject::MethodThunk_(v8::Local<v8::String> method,
                             const Nan::FunctionCallbackInfo<v8::Value>& info) {
  if (id_ == 0) {
    return Nan::ThrowError("Invocation after PHP request has completed.");
  }
  PhpInvokeMsg msg(channel_, nullptr, true,  // Sync call.
                   id_, method, &info);
  channel_->SendToPhp(&msg, MessageFlags::SYNC);
  THROW_IF_EXCEPTION("PHP exception thrown during method invocation", /* */);
  if (msg.retval().IsEmpty()) {
    info.GetReturnValue().Set(Nan::Undefined());
  } else if (msg.should_convert_array_to_iterator()) {
    // Map#keys() method should actually return an iterator, not an
    // array, so call Array#values() on the result.
    v8::Local<v8::Array> r = msg.retval().ToJs(channel_).As<v8::Array>();
    v8::Local<v8::Object> values = Nan::Get(r, NEW_STR("values"))
      .ToLocalChecked().As<v8::Object>();
    Nan::MaybeLocal<v8::Value> newr =
      Nan::CallAsFunction(values, r, 0, nullptr);
    if (!newr.IsEmpty()) { info.GetReturnValue().Set(newr.ToLocalChecked()); }
  } else {
    info.GetReturnValue().Set(msg.retval().ToJs(channel_));
  }
}
void PhpObject::ArrayAccessOp(PhpObjectMapper *m, PropertyOp op,
                        const ZVal &arr, const ZVal &name, const ZVal &value,
                        Value *retval, Value *exception TSRMLS_DC) {
  assert(arr.IsObject() && name.IsString());
  zval *rv = nullptr;

  zval **objpp = const_cast<ZVal&>(arr).PtrPtr();
  // Make sure calling the interface method doesn't screw with `name`;
  // this is done in spl_array.c, presumably for good reason.
  ZVal offset(name.Ptr() ZEND_FILE_LINE_CC);
  offset.Separate();
  if (op == PropertyOp::QUERY) {
    zend_call_method_with_1_params(objpp, nullptr, nullptr, "offsetExists",
                                   &rv, offset.Ptr());
    if (rv && zend_is_true(rv)) {
      retval->SetInt(v8::None);
    } else {
      retval->SetEmpty();
    }
    if (rv) { zval_ptr_dtor(&rv); }
  } else if (op == PropertyOp::GETTER) {
    zval *rv2;
    // We need to call offsetExists to distinguish between "missing offset"
    // and "offset present, but with value NULL."
    zend_call_method_with_1_params(objpp, nullptr, nullptr, "offsetExists",
                                   &rv2, offset.Ptr());
    if (rv2 && zend_is_true(rv2)) {
      zend_call_method_with_1_params(objpp, nullptr, nullptr, "offsetGet",
                                     &rv, offset.Ptr());
    }
    if (rv) {
      retval->Set(m, rv TSRMLS_CC);
      retval->TakeOwnership();
      zval_ptr_dtor(&rv);
    } else {
      retval->SetEmpty();
    }
    if (rv2) { zval_ptr_dtor(&rv2); }
  } else if (op == PropertyOp::SETTER) {
    zend_call_method_with_2_params(objpp, nullptr, nullptr, "offsetSet",
                                   NULL, offset.Ptr(), value.Ptr());
    retval->Set(m, value TSRMLS_CC);
    retval->TakeOwnership();
  } else if (op == PropertyOp::DELETER) {
    zend_call_method_with_1_params(objpp, nullptr, nullptr, "offsetUnset",
                                   NULL, offset.Ptr());
    retval->SetBool(true);
  } else {
    assert(false);
  }
}

void PhpObject::ArrayOp(PhpObjectMapper *m, PropertyOp op,
                        const ZVal &arr, bool is_array_access,
                        const ZVal &name, const ZVal &value,
                        Value *retval, Value *exception TSRMLS_DC) {
  if (is_array_access) {
    // Split this case into its own function to avoid cluttering this one
    // with two dissimilar cases.
    return ArrayAccessOp(m, op, arr, name, value, retval, exception TSRMLS_CC);
  }
  assert(arr.IsArray() && name.IsString());
  HashTable *arrht = Z_ARRVAL_P(arr.Ptr());
  const char *cname = Z_STRVAL_P(name.Ptr());
  uint cname_len = Z_STRLEN_P(name.Ptr());
  zval **r;
  if (op == PropertyOp::QUERY) {
    if (zend_symtable_find(arrht, cname, cname_len + 1,
                           reinterpret_cast<void**>(&r)) == SUCCESS) {
      if (Z_TYPE_PP(r) == IS_NULL) {
        // "isset" semantics, say it's not enumerable if null
        retval->SetInt(v8::DontEnum);
      } else {
        retval->SetInt(v8::None);
      }
    } else {
      retval->SetEmpty();
    }
  } else if (op == PropertyOp::GETTER) {
    if (zend_symtable_find(arrht, cname, cname_len + 1,
                           reinterpret_cast<void**>(&r)) == SUCCESS) {
      retval->Set(m, *r TSRMLS_CC);
      retval->TakeOwnership();
    } else {
      retval->SetEmpty();
    }
  } else if (op == PropertyOp::SETTER) {
    zval *z = const_cast<ZVal &>(value).Escape();
    zend_symtable_update(arrht, cname, cname_len + 1, &z, sizeof(z), NULL);
    retval->Set(m, z TSRMLS_CC);
    retval->TakeOwnership();
  } else if (op == PropertyOp::DELETER) {
    if (SUCCESS == zend_symtable_del(arrht, cname, cname_len + 1)) {
      retval->SetBool(true);
    } else {
      retval->SetEmpty();
    }
  } else {
    assert(false);
  }
}

void PhpObject::ArrayEnum(PhpObjectMapper *m, EnumOp op,
                          const ZVal &arr, bool is_array_access,
                          Value *retval, Value *exception TSRMLS_DC) {
  // XXX unimplemented
  retval->SetArrayByValue(0, [](uint32_t idx, Value& v) { });
}

void PhpObject::ArraySize(PhpObjectMapper *m, PropertyOp op, EnumOp which,
                          const ZVal &arr, bool is_array_access,
                          const ZVal &value,
                          Value *retval, Value *exception TSRMLS_DC) {
  if (is_array_access) {
    assert(arr.IsObject());
    zval **objpp = const_cast<ZVal&>(arr).PtrPtr();
    zval *rv;
    if (op == PropertyOp::GETTER) {
      if (which == EnumOp::ALL) {
        zend_call_method_with_0_params(objpp, nullptr, nullptr, "count", &rv);
        ZVal r(rv ZEND_FILE_LINE_CC);
        if (rv) { zval_ptr_dtor(&rv); }
        r.Separate();
        convert_to_long(r.Ptr());
        retval->Set(m, r TSRMLS_CC);
        retval->TakeOwnership();
      } else if (which == EnumOp::ONLY_INDEX) {
        // XXX Not supported by standard ArrayAccess API.
        // XXX Define our own Js\Array interface, and try to call
        //     a `getLength` method in it, iff the object implements
        //     Js\Array?
        retval->SetInt(0);
      }
    } else if (op == PropertyOp::SETTER && which == EnumOp::ONLY_INDEX) {
      // XXX Not supported by standard ArrayAccess API.
      // XXX Define our own Js\Array interface, and try to call
      //     a `setLength` method in it, iff the object implements
      //     Js\Array?
      retval->Set(m, value TSRMLS_CC);
      retval->TakeOwnership();
    }
  } else {
    assert(arr.IsArray());
    HashTable *arrht = Z_ARRVAL_P(arr.Ptr());
    if (op == PropertyOp::GETTER) {
      if (which == EnumOp::ALL) {
        // This is "number of items" (including string keys), not "max index"
        retval->SetInt(zend_hash_num_elements(arrht));
        return;
      } else if (which == EnumOp::ONLY_INDEX) {
        retval->SetInt(zend_hash_next_free_element(arrht));
      }
    } else if (op == PropertyOp::SETTER && which == EnumOp::ONLY_INDEX) {
        if (!value.IsLong()) {
          // convert to int
          const_cast<ZVal&>(value).Separate();
          convert_to_long(value.Ptr());
        }
        if (value.IsLong() && Z_LVAL_P(value.Ptr()) >= 0) {
          ulong nlen = Z_LVAL_P(value.Ptr());
          ulong olen = zend_hash_next_free_element(arrht);
          if (nlen < olen) {
            // We have to iterate here, rather unfortunate.
            // XXX We could look at zend_hash_num_elements() and
            // iterate over the elements if num_elements < (olen - nlen)
            for (ulong i = nlen; i < olen; i++) {
              zend_hash_index_del(arrht, i);
            }
          }
          // This is quite dodgy, since we're going to write the
          // nNextFreeElement field directly.  Perhaps not portable!
          arrht->nNextFreeElement = nlen;
        }
        retval->Set(m, value TSRMLS_CC);
        retval->TakeOwnership();
    }
  }
}


}  // namespace node_php_embed
