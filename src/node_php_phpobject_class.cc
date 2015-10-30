// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_phpobject_class.h"

#include <cassert>

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
                               // XXX PropertyEnumerate is not yet implemented.
                               0/*PhpObject::PropertyEnumerate*/);
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

NAN_PROPERTY_SETTER(PhpObject::PropertySet) {
  TRACEX("> %s", *Nan::Utf8String(property));
  // XXX for async access, it seems like we might be able to lie about
  // what the result of the Set operation is -- that is,
  // `(x = y) !== y`.  Perhaps `x = y` could resolve to a `Promise`?
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  info.GetReturnValue().Set(p->Property(PropertyOp::SETTER, property, value));
  TRACEX("< %s", *Nan::Utf8String(property));
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

NAN_PROPERTY_QUERY(PhpObject::PropertyQuery) {
  TRACEX("> %s", *Nan::Utf8String(property));
  PhpObject *p = Unwrap<PhpObject>(info.Holder());
  v8::Local<v8::Value> r = p->Property(PropertyOp::QUERY, property);
  if (!r.IsEmpty()) {
    info.GetReturnValue().Set(Nan::To<v8::Integer>(r).ToLocalChecked());
  }
  TRACEX("< %s", *Nan::Utf8String(property));
}

NAN_PROPERTY_ENUMERATOR(PhpObject::PropertyEnumerate) {
  assert(false);  // XXX unimplemented.
}

class PhpObject::PhpPropertyMsg : public MessageToPhp {
 public:
  PhpPropertyMsg(ObjectMapper *m, Nan::Callback *callback, bool is_sync,
                 PropertyOp op, objid_t obj, v8::Local<v8::String> name,
                 v8::Local<v8::Value> value = v8::Local<v8::Value>())
    : MessageToPhp(m, callback, is_sync), op_(op), name_(m, name) {
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
    assert(obj.IsObject() && zname.IsString());
    if (!value_.IsEmpty()) {
      value_.ToPhp(m, value TSRMLS_CC);
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

            // XXX make a PHP closure and return it.
          } else {
            // XXX return a PHP closure for this method invocation
          }
        }
      } else if (op_ == PropertyOp::QUERY) {
        // Methods are not enumerable.
        retval_.SetInt(v8::ReadOnly|v8::DontEnum|v8::DontDelete);
      } else if (op_ == PropertyOp::SETTER) {
        // Lie: methods are read-only, don't allow setting this property.
        retval_.Set(m, *value TSRMLS_CC);
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

 private:
  PropertyOp op_;
  Value obj_;
  Value name_;
  Value value_;
};


v8::Local<v8::Value> PhpObject::Property(PropertyOp op,
                                         v8::Local<v8::String> property,
                                         v8::Local<v8::Value> newValue) {
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
                     op, id_, property, newValue);
  channel_->SendToPhp(&msg, MessageFlags::SYNC);
  THROW_IF_EXCEPTION("PHP exception thrown during property access",
                     v8::Local<v8::Value>());
  if (!msg.retval().IsEmpty()) {
    return scope.Escape(msg.retval().ToJs(channel_));
  }
  return scope.Escape(v8::Local<v8::Value>());
}

}  // namespace node_php_embed
