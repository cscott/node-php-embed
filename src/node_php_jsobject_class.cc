// A JavaScript (node/v8) object, wrapped for access by PHP code.
// Inspired by v8js_v8object_class in the v8js PHP extension.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_jsobject_class.h"

#include "nan.h"

extern "C" {
#include <main/php.h>
#include <Zend/zend.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_types.h>
}

#include "src/macros.h"
#include "src/messages.h"
#include "src/values.h"

// An alternative approach where we use an __isset magic method.
// This doesn't let us properly implement property_exists(), though.
#define USE_MAGIC_ISSET 0

using node_php_embed::JsObjectMapper;
using node_php_embed::MessageFlags;
using node_php_embed::MessageToJs;
using node_php_embed::ObjectMapper;
using node_php_embed::Value;
using node_php_embed::ZVal;
using node_php_embed::node_php_jsobject;
using node_php_embed::objid_t;

/* Class Entries */
zend_class_entry *php_ce_jsobject;

/* Object Handlers */
static zend_object_handlers node_php_jsobject_handlers;

/* Helpful macros for common tasks in PHP magic methods. */

#define FETCH_OBJ_ELSE(method, this_ptr, defaultValue)                  \
  node_php_jsobject *obj = reinterpret_cast<node_php_jsobject *>        \
    (zend_object_store_get_object(this_ptr TSRMLS_CC));                 \
  if (obj->id == 0) {                                                   \
    zend_throw_exception(zend_exception_get_default(TSRMLS_C),          \
                         #method " after shutdown", 0 TSRMLS_CC);       \
    return defaultValue;                                                \
  }
#define FETCH_OBJ(method, this_ptr) FETCH_OBJ_ELSE(method, this_ptr, )

#define PARSE_PARAMS(method, ...)                                       \
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, __VA_ARGS__) ==  \
      FAILURE) {                                                        \
    zend_throw_exception(zend_exception_get_default(TSRMLS_C),          \
                         "bad args to " #method, 0 TSRMLS_CC);          \
    return;                                                             \
  }                                                                     \
  FETCH_OBJ(method, this_ptr)

#define THROW_IF_EXCEPTION(fmt, ...)                            \
  do {                                                          \
    if (msg.HasException()) {                                   \
      ZVal e{ZEND_FILE_LINE_C};                                 \
      msg.exception().ToPhp(obj->channel, e TSRMLS_CC);         \
      e.Separate();                                     \
      convert_to_string(e.Ptr());                               \
      /* XXX attach the wrapped JS exception XX */              \
      zend_throw_exception_ex(                                  \
        zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,      \
        fmt ": %*s", __VA_ARGS__, Z_STRLEN_P(*e), Z_STRVAL_P(*e)); \
      return;                                                   \
    }                                                           \
  } while (0)

/* JsObject handlers */

class JsHasPropertyMsg : public MessageToJs {
 public:
  JsHasPropertyMsg(ObjectMapper *m, zval *callback, bool isSync,
                   objid_t objId, zval *member, int has_set_exists TSRMLS_DC)
      : MessageToJs(m, callback, isSync),
        object_(), member_(m, member TSRMLS_CC),
        has_set_exists_(has_set_exists) {
    object_.SetJsObject(objId);
  }

 protected:
  void InJs(JsObjectMapper *m) override {
    TRACE("> JsHasPropertyMsg");
    retval_.SetBool(false);
    v8::Local<v8::Object> jsObj = Nan::To<v8::Object>(object_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::String> jsKey = Nan::To<v8::String>(member_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::Value> jsVal;

    /* Skip any prototype properties */
    if (Nan::HasRealNamedProperty(jsObj, jsKey).FromMaybe(false) ||
        Nan::HasRealNamedCallbackProperty(jsObj, jsKey).FromMaybe(false)) {
      if (has_set_exists_ == 2) {
        /* property_exists(), that's enough! */
        retval_.SetBool(true);
      } else {
        /* We need to look at the value. */
        jsVal = Nan::Get(jsObj, jsKey).FromMaybe
          ((v8::Local<v8::Value>)Nan::Undefined());
        if (has_set_exists_ == 0) {
          /* isset(): We make 'undefined' equivalent to 'null' */
          retval_.SetBool(!(jsVal->IsNull() || jsVal->IsUndefined()));
        } else {
          /* empty() */
          retval_.SetBool(Nan::To<bool>(jsVal).FromMaybe(false));
          /* for PHP compatibility, [] should also be empty */
          if (jsVal->IsArray() && retval_.AsBool()) {
            v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(jsVal);
            retval_.SetBool(array->Length() != 0);
          }
          /* for PHP compatibility, '0' should also be empty */
          if (jsVal->IsString() && retval_.AsBool()) {
            v8::Local<v8::String> str = Nan::To<v8::String>(jsVal)
              .ToLocalChecked();
            if (str->Length() == 1) {
              uint16_t c = 0;
              str->Write(&c, 0, 1);
              if (c == '0') {
                retval_.SetBool(false);
              }
            }
          }
        }
      }
    }
    TRACE("< JsHasPropertyMsg");
  }

 private:
  Value object_;
  Value member_;
  int has_set_exists_;
};

#if USE_MAGIC_ISSET

ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_isset_args, 0, 0, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __isset) {
  zval *member;
  TRACE(">");
  PARSE_PARAMS(__isset, "z/", &member);
  convert_to_string(member);
  JsHasPropertyMsg msg(obj->channel, nullptr, true,  // Sync call.
                       obj->id, member, 0 TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during __isset of \"%*s\"",
                     Z_STRLEN_P(member), Z_STRVAL_P(member));
  TRACE("<");
  RETURN_BOOL(msg.retval_.AsBool());
}

#else

// By overriding has_property we can implement property_exists correctly,
// and also handle empty arrays.
static int node_php_jsobject_has_property(zval *object, zval *member,
                                          int has_set_exists
                                          ZEND_HASH_KEY_DC TSRMLS_DC) {
  /* The has_set_exists parameter:
   * 0 (has) whether property exists and is not NULL  - isset()
   * 1 (set) whether property exists and is true-ish  - empty()
   * 2 (exists) whether property exists               - property_exists()
   */
  TRACE(">");
  if (Z_TYPE_P(member) != IS_STRING) {
    return false;
  }
  FETCH_OBJ_ELSE(has_property, object, false);
  JsHasPropertyMsg msg(obj->channel, nullptr, true,  // Sync call.
                       obj->id, member, has_set_exists TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  // Ok, result is in msg.retval_ or msg.exception_
  if (msg.HasException()) { return false; /* sigh */ }
  TRACE("<");
  return msg.retval().AsBool();
}

#endif /* USE_MAGIC_ISSET */

class JsReadPropertyMsg : public MessageToJs {
 public:
  JsReadPropertyMsg(ObjectMapper* m, zval *callback, bool isSync,
                    objid_t objId, zval *member, int type TSRMLS_DC)
      : MessageToJs(m, callback, isSync),
        object_(), member_(m, member TSRMLS_CC), type_(type) {
    object_.SetJsObject(objId);
  }

 protected:
  void InJs(JsObjectMapper *m) override {
    TRACE("> JsReadPropertyMsg");
    v8::Local<v8::Object> jsObj = Nan::To<v8::Object>(object_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::String> jsKey = Nan::To<v8::String>(member_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::Value> jsVal;

    /* Skip any prototype properties */
    if (Nan::HasRealNamedProperty(jsObj, jsKey).FromMaybe(false) ||
        Nan::HasRealNamedCallbackProperty(jsObj, jsKey).FromMaybe(false)) {
      jsVal = Nan::Get(jsObj, jsKey).FromMaybe
        ((v8::Local<v8::Value>)Nan::Undefined());
      retval_.Set(m, jsVal);
    } else {
      retval_.SetNull();
    }
    TRACE("< JsReadPropertyMsg");
  }

 private:
  Value object_;
  Value member_;
  int type_;
};


ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_get_args, 0, 1/*return by ref*/, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __get) {
  TRACE(">");
  zval *member;
  PARSE_PARAMS(__get, "z/", &member);
  convert_to_string(member);
  JsReadPropertyMsg msg(obj->channel, nullptr, true,  // Sync call.
                        obj->id, member, 0 TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during __get of \"%*s\"",
                     Z_STRLEN_P(member), Z_STRVAL_P(member));
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
  TRACE("<");
}

class JsWritePropertyMsg : public MessageToJs {
 public:
  JsWritePropertyMsg(ObjectMapper* m, zval *callback, bool isSync,
                     objid_t objId, zval *member, zval *value TSRMLS_DC)
      : MessageToJs(m, callback, isSync),
        object_(), member_(m, member TSRMLS_CC), value_(m, value TSRMLS_CC) {
    object_.SetJsObject(objId);
  }

 protected:
  void InJs(JsObjectMapper *m) override {
    TRACE("> HasPropertyMsg");
    v8::Local<v8::Object> jsObj = Nan::To<v8::Object>(object_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::String> jsKey = Nan::To<v8::String>(member_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::Value> jsVal = value_.ToJs(m);

    if (Nan::Set(jsObj, jsKey, jsVal).FromMaybe(false)) {
      retval_.SetBool(true);
    }
    TRACE("< HasPropertyMsg");
  }

 private:
  Value object_;
  Value member_;
  Value value_;
};

ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_set_args, 0, 0, 2)
    ZEND_ARG_INFO(0, member)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __set) {
  zval *member; zval *value;
  TRACE(">");
  PARSE_PARAMS(__set, "z/z", &member, &value);
  convert_to_string(member);
  JsWritePropertyMsg msg(obj->channel, nullptr, true,  // Sync call.
                         obj->id, member, value TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during __set of \"%*s\"",
                     Z_STRLEN_P(member), Z_STRVAL_P(member));
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
  TRACE("<");
}

class JsDeletePropertyMsg : public MessageToJs {
 public:
  JsDeletePropertyMsg(ObjectMapper* m, zval *callback, bool isSync,
                      objid_t objId, zval *member TSRMLS_DC)
      : MessageToJs(m, callback, isSync),
        object_(), member_(m, member TSRMLS_CC) {
    object_.SetJsObject(objId);
  }

 protected:
  void InJs(JsObjectMapper *m) override {
    TRACE("> DeletePropertyMsg");
    v8::Local<v8::Object> jsObj = Nan::To<v8::Object>(object_.ToJs(m))
      .ToLocalChecked();
    v8::Local<v8::String> jsKey = Nan::To<v8::String>(member_.ToJs(m))
      .ToLocalChecked();

    if (Nan::Delete(jsObj, jsKey).FromMaybe(false)) {
      retval_.SetBool(true);
    }
    TRACE("< DeletePropertyMsg");
  }

 private:
  Value object_;
  Value member_;
};

ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_unset_args, 0, 0, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __unset) {
  zval *member;
  TRACE(">");
  PARSE_PARAMS(__unset, "z/", &member);
  convert_to_string(member);
  JsDeletePropertyMsg msg(obj->channel, nullptr, true,  // Sync call.
                          obj->id, member TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during __unset of \"%*s\"",
                     Z_STRLEN_P(member), Z_STRVAL_P(member));
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
  TRACE("<");
}

class JsInvokeMsg : public MessageToJs {
 public:
  JsInvokeMsg(ObjectMapper *m, zval *callback, bool isSync,
              objid_t objId, zval *member, ulong argc, zval **argv TSRMLS_DC)
      : MessageToJs(m, callback, isSync),
        object_(), member_(m, member TSRMLS_CC),
        argc_(argc), argv_(Value::NewArray(m, argc, argv TSRMLS_CC)) {
    object_.SetJsObject(objId);
  }
  ~JsInvokeMsg() override { delete[] argv_; }

 protected:
  void InJs(JsObjectMapper *m) override {
    TRACE("> JsInvokeMsg");
    Nan::MaybeLocal<v8::Object> jsObj =
      Nan::To<v8::Object>(object_.ToJs(m));
    if (jsObj.IsEmpty()) {
      return Nan::ThrowTypeError("receiver is not an object");
    }

    v8::Local<v8::Value> member = member_.ToJs(m);
    Nan::MaybeLocal<v8::Object> method;
    if (member->IsNull()) {
      // Invoke function, not method.
      method = jsObj;
      // Should be null, but https://github.com/nodejs/nan/issues/497
      // doesn't let us pass null to functions yet.
      jsObj = Nan::MakeMaybe(Nan::New<v8::Object>());
    } else {
      method = Nan::To<v8::Object>(
        Nan::Get(jsObj.ToLocalChecked(), member)
          .FromMaybe<v8::Value>(Nan::Undefined()));
    }
    if (method.IsEmpty()) {
      return Nan::ThrowTypeError("method is not an object");
    }
    if (!method.ToLocalChecked()->IsFunction()) {
      return Nan::ThrowTypeError("method is not a function");
    }
    v8::Local<v8::Function> func = method.ToLocalChecked().As<v8::Function>();
    ulong argc = argc_;
    v8::Local<v8::Value> *argv =
      static_cast<v8::Local<v8::Value>*>
      (alloca(sizeof(v8::Local<v8::Value>) * argc));
    bool sawWait = false;
    for (ulong i = 0; i < argc; i++) {
      new(&argv[i]) v8::Local<v8::Value>;
      if (argv_[i].IsWait() && !sawWait) {
        sawWait = true;
        argv[i] = MakeCallback();
      } else {
        argv[i] = argv_[i].ToJs(m);
      }
    }
    Nan::MaybeLocal<v8::Value> result =
      Nan::CallAsFunction(func, jsObj.ToLocalChecked(), argc, argv);
    if (!result.IsEmpty() && !sawWait) {
      // XXX perhaps if the result is a promise, we should wait on it?
      // Better would be a PHP-side function we could call using the
      // existing JsWait, ie:
      //   $p = promiseReturningFunction();
      //   $p.wait(new JsWait);
      // where:
      //   Promise.prototype.wait = function(cb) {
      //     this.then(function(v) { cb(null, v); }, cb);
      //   }
      // This is actually Promise#nodify from prfun, so perhaps
      // all that is needed is to call prfun's Promise.resolve()
      // on any wrapped promise objects?
      retval_.Set(m, result.ToLocalChecked());
    }
    TRACE("< JsInvokeMsg");
  }

 private:
  Value object_;
  Value member_;
  ulong argc_;
  Value *argv_;
};

// XXX Figure out how to actually invoke methods async.
// (or async methods sync, by passing a JS callback which will unblock
// the PHP thread)
ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_call_args, 0, 1/*return by ref*/, 1)
    ZEND_ARG_INFO(0, member)
    ZEND_ARG_INFO(0, args)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __call) {
  zval *member; zval *args;
  TRACE(">");
  PARSE_PARAMS(__unset, "z/a", &member, &args);
  convert_to_string(member);
  HashTable *arrht = Z_ARRVAL_P(args);
  ulong argc = zend_hash_next_free_element(arrht);  // Maximum index in hash.
  zval **argv = static_cast<zval**>(alloca(sizeof(zval*) * argc));
  for (ulong i = 0; i < argc; i++) {
    zval **z;
    if (zend_hash_index_find(arrht, i, reinterpret_cast<void**>(&z)) ==
        FAILURE) {
      argv[i] = EG(uninitialized_zval_ptr);
    } else {
      argv[i] = *z;
    }
  }
  JsInvokeMsg msg(obj->channel, nullptr, true,      // Sync call.
                  obj->id, member, argc, argv TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during __call of \"%*s\"",
                     Z_STRLEN_P(member), Z_STRVAL_P(member));
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
  TRACE("<");
}

PHP_METHOD(JsObject, __invoke) {
  TRACE(">");
  FETCH_OBJ(__invoke, this_ptr);
  zval member; INIT_ZVAL(member);
  int argc = ZEND_NUM_ARGS();
  zval **argv = reinterpret_cast<zval**>(safe_emalloc(argc, sizeof(*argv), 0));
  if (argc > 0 && zend_get_parameters_array(ht, argc, argv) == FAILURE) {
    efree(argv);
    zend_throw_exception(zend_exception_get_default(TSRMLS_C),
                         "bad args to __invoke", 0 TSRMLS_CC);
    return;
  }
  JsInvokeMsg msg(obj->channel, nullptr, true,      // Sync call.
                  obj->id, &member, argc, argv TSRMLS_CC);
  efree(argv);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during %s", "__invoke");
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
  TRACE("<");
}

/* Backdoor invocation, for use in node_php_embed before the request has
 * been started. */
void node_php_embed::node_php_jsobject_call_method(
    zval *object, zval *member, ulong argc, zval **argv,
    zval *return_value, zval **return_value_ptr TSRMLS_DC) {
  assert(Z_TYPE_P(object) == IS_OBJECT && Z_OBJCE_P(object) == php_ce_jsobject);
  FETCH_OBJ("node_php_embed_call_method", object);
  JsInvokeMsg msg(obj->channel, nullptr, true,  // Sync call
                  obj->id, member, argc, argv TSRMLS_CC);
  obj->channel->SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
  THROW_IF_EXCEPTION("JS exception thrown during node_php_embed_call_method"
                     "of \"%*s\"", Z_STRLEN_P(member), Z_STRVAL_P(member));
  msg.retval().ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
}

ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_tostring_args, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(JsObject, __tostring) {
  // Implement `__tostring` by calling JS `toString` method.
  // Use plain zval to avoid allocating copy of method name
  zval method; ZVAL_STRINGL(&method, "toString", 8, 0);
  zval *args = nullptr;
  call_user_function(EG(function_table), &this_ptr, &method,
                     return_value, 0, &args TSRMLS_CC);
}

/* Use (slightly thunked) versions of the has/read/write property handlers
 * for dimensions as well, so that $obj['foo'] acts like $obj->foo. */

static int node_php_jsobject_has_dimension(zval *obj, zval *idx,
                                           int chk_type TSRMLS_DC) {
  TRACE(">");
  // Thunk!
  if (chk_type == 0) { chk_type = 2; }
  // Use standard has_property method with new chk_type.
  return node_php_jsobject_handlers.has_property(obj, idx, chk_type
                                                 ZEND_HASH_KEY_NULL TSRMLS_CC);
}

static zval *node_php_jsobject_read_dimension(zval *obj, zval *off, int type
                                              TSRMLS_DC) {
  TRACE(">");
  // Use standard read_property method.
  return node_php_jsobject_handlers.read_property(obj, off, type
                                                  ZEND_HASH_KEY_NULL TSRMLS_CC);
}

static void node_php_jsobject_write_dimension(zval *obj, zval *off, zval *val
                                              TSRMLS_DC) {
  TRACE(">");
  // Use standard write_property method.
  node_php_jsobject_handlers.write_property(obj, off, val
                                            ZEND_HASH_KEY_NULL TSRMLS_CC);
}

static void node_php_jsobject_unset_dimension(zval *obj, zval *off TSRMLS_DC) {
  TRACE(">");
  // Use standard unset_property method.
  node_php_jsobject_handlers.unset_property(obj, off ZEND_HASH_KEY_NULL
                                            TSRMLS_CC);
}

static void node_php_jsobject_free_storage(void *object,
                                           zend_object_handle handle
                                           TSRMLS_DC) {
  TRACE(">");
  node_php_jsobject *c = reinterpret_cast<node_php_jsobject *>(object);

  zend_object_std_dtor(&c->std TSRMLS_CC);

  // XXX We ought to deregister the id here.
  TRACE("PHP deallocate");

  // You'd first remove the zval from the id->zval mapping table,
  // since that could always been recreated, and then fire-and-forget
  // a message to JS land.  On receipt JS would mark the value as
  // 'dying', then fire off a new messge to PHP land.  PHP would
  // respond with whether the object has been resurrected.  When JS
  // gets this response, if the object has not been resurrected and
  // the JS `dying` flag is still set (it would be removed if another
  // reference to that object was made), then it could actually remove
  // the ref from its table on the JS side.

  efree(object);
  TRACE("<");
}

static zend_object_value node_php_jsobject_new(zend_class_entry *ce TSRMLS_DC) {
  TRACE(">");
  zend_object_value retval;
  node_php_jsobject *c;

  c = reinterpret_cast<node_php_jsobject *>(ecalloc(1, sizeof(*c)));

  zend_object_std_init(&c->std, ce TSRMLS_CC);

  retval.handle = zend_objects_store_put(
    c, nullptr,
    (zend_objects_free_object_storage_t) node_php_jsobject_free_storage,
    nullptr TSRMLS_CC);
  retval.handlers = &node_php_jsobject_handlers;

  TRACE("<");
  return retval;
}

void node_php_embed::node_php_jsobject_create(zval *res,
                                              MapperChannel *channel,
                                              objid_t id TSRMLS_DC) {
  TRACE(">");
  node_php_jsobject *c;

  object_init_ex(res, php_ce_jsobject);

  c = reinterpret_cast<node_php_jsobject *>
    (zend_object_store_get_object(res TSRMLS_CC));

  c->channel = channel;
  c->id = id;
  TRACE("<");
}

void node_php_embed::node_php_jsobject_maybe_neuter(zval *o TSRMLS_DC) {
  if (Z_TYPE_P(o) != IS_OBJECT || Z_OBJCE_P(o) != php_ce_jsobject) {
    return;
  }
  node_php_jsobject *obj = reinterpret_cast<node_php_jsobject *>
    (zend_object_store_get_object(o TSRMLS_CC));
  if (!obj) { return; }
  obj->id = 0;
  obj->channel = nullptr;
}

#define STUB_METHOD(name)                                               \
  PHP_METHOD(JsObject, name) {                                          \
    TRACE(">");                                                         \
    zend_throw_exception(                                               \
      zend_exception_get_default(TSRMLS_C),                             \
      "Can't directly construct, serialize, or unserialize JsObject.",  \
      0 TSRMLS_CC);                                                     \
    TRACE("<");                                                         \
    RETURN_FALSE;                                                       \
  }

/* NOTE: We could also override node_php_jsobject_handlers.get_constructor
 * to throw an exception when invoked, but doing so causes the
 * half-constructed object to leak -- this seems to be a PHP bug.  So
 * we'll define magic __construct methods instead. */
STUB_METHOD(__construct)
STUB_METHOD(__sleep)
STUB_METHOD(__wakeup)

static const zend_function_entry node_php_jsobject_methods[] = {
  PHP_ME(JsObject, __construct, nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
  PHP_ME(JsObject, __sleep,     nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __wakeup,    nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
#if USE_MAGIC_ISSET
  PHP_ME(JsObject, __isset, node_php_jsobject_isset_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
#endif
  PHP_ME(JsObject, __get, node_php_jsobject_get_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __set, node_php_jsobject_set_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __unset, node_php_jsobject_unset_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __call, node_php_jsobject_call_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __invoke, nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsObject, __tostring, node_php_jsobject_tostring_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  ZEND_FE_END
};


PHP_MINIT_FUNCTION(node_php_jsobject_class) {
  TRACE("> PHP_MINIT_FUNCTION");
  zend_class_entry ce;
  /* JsObject Class */
  INIT_CLASS_ENTRY(ce, "Js\\Object", node_php_jsobject_methods);
  php_ce_jsobject = zend_register_internal_class(&ce TSRMLS_CC);
  php_ce_jsobject->ce_flags |= ZEND_ACC_FINAL;
  php_ce_jsobject->create_object = node_php_jsobject_new;

  /* JsObject handlers */
  memcpy(&node_php_jsobject_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  node_php_jsobject_handlers.clone_obj = nullptr;
  node_php_jsobject_handlers.get_property_ptr_ptr = nullptr;
#if !USE_MAGIC_ISSET
  node_php_jsobject_handlers.has_property = node_php_jsobject_has_property;
#endif
  /*
  node_php_jsobject_handlers.read_property = node_php_jsobject_read_property;
  node_php_jsobject_handlers.write_property = node_php_jsobject_write_property;
  node_php_jsobject_handlers.unset_property = node_php_jsobject_unset_property;
  node_php_jsobject_handlers.get_properties = node_php_jsobject_get_properties;
  node_php_jsobject_handlers.get_method = node_php_jsobject_get_method;
  node_php_jsobject_handlers.call_method = node_php_jsobject_call_method;
  node_php_jsobject_handlers.get_debug_info = node_php_jsobject_get_debug_info;
  node_php_jsobject_handlers.get_closure = node_php_jsobject_get_closure;
  */

  /* Array access handlers: slightly thunked versions of property handlers. */
  node_php_jsobject_handlers.read_dimension =
    node_php_jsobject_read_dimension;
  node_php_jsobject_handlers.write_dimension =
    node_php_jsobject_write_dimension;
  node_php_jsobject_handlers.has_dimension =
    node_php_jsobject_has_dimension;
  node_php_jsobject_handlers.unset_dimension =
    node_php_jsobject_unset_dimension;

  TRACE("< PHP_MINIT_FUNCTION");
  return SUCCESS;
}
