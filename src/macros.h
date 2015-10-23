// Helpful macros for this project.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_MACROS_H_
#define NODE_PHP_EMBED_MACROS_H_

#define NPE_ERRORX(msg, ...) \
  fprintf(stderr, msg " %s\n", __VA_ARGS__, __func__)
#define NPE_ERROR(msg) \
  NPE_ERRORX(msg "%s", "") /* hack to eat up required argument */

// Poor man's trace mechanism; helpful for tracking down crashes on obscure
// architectures using travis.
#ifdef NODE_PHP_EMBED_TRACE
# define TRACE(...) NPE_ERROR(__VA_ARGS__)
# define TRACEX(...) NPE_ERRORX(__VA_ARGS__)
#else
# define TRACE(...)
# define TRACEX(...)
#endif

/** v8/nan helpers **/

#define NEW_STR(str)                            \
  Nan::New<v8::String>(str).ToLocalChecked()

#define REQUIRE_ARGUMENTS(n)                                            \
  if (info.Length() < (n)) {                                            \
    return Nan::ThrowTypeError("Expected " #n " arguments");            \
  }

#define REQUIRE_ARGUMENT_STRING(i, var)                                 \
  if (info.Length() <= (i) || !info[i]->IsString()) {                   \
    return Nan::ThrowTypeError("Argument " #i " must be a string");     \
  }                                                                     \
  Nan::Utf8String var(info[i])

#define REQUIRE_ARGUMENT_NUMBER(i)                                      \
  if (info.Length() <= (i) || !info[i]->IsNumber()) {                   \
    return Nan::ThrowTypeError("Argument " #i " must be a number");     \
  }

#define REQUIRE_ARGUMENT_INTEGER(i, var)                                \
  if (info.Length() <= (i) || !(info[i]->IsInt32() || info[i]->IsUint32())) { \
    return Nan::ThrowTypeError("Argument " #i " must be an integer");   \
  }                                                                     \
  int64_t var = Nan::To<int64_t>(info[i]).FromMaybe(0)

#define OPTIONAL_ARGUMENT_BOOLEAN(i, var, default)                      \
  int var;                                                              \
  if (info.Length() <= (i)) {                                           \
    var = (default);                                                    \
  } else {                                                              \
    var = Nan::To<bool>(info[i]).FromMaybe(default);                    \
  }

#define OPTIONAL_ARGUMENT_INTEGER(i, var, default)                      \
  int var;                                                              \
  if (info.Length() <= (i)) {                                           \
    var = (default);                                                    \
  } else if (info[i]->IsInt32()) {                                      \
    var = NanTo<int32_t>(info[i]).FromMaybe(default);                   \
  } else {                                                              \
    return Nan::ThrowTypeError("Argument " #i " must be an integer");   \
  }

#define DEFINE_CONSTANT_INTEGER(target, constant, name)                 \
  Nan::ForceSet((target),                                               \
                NEW_STR(#name),                                         \
                Nan::New<Integer>(constant),                            \
                static_cast<PropertyAttribute>(ReadOnly | DontDelete))

#define DEFINE_CONSTANT_STRING(target, constant, name)                  \
  Nan::ForceSet((target),                                               \
                NEW_STR(#name),                                         \
                Nan::New<String>(constant).ToLocalChecked(),            \
                static_cast<PropertyAttribute>(ReadOnly | DontDelete))

#define GET_PROPERTY(source, property)                                  \
  Nan::Get((source), NEW_STR(property))                                 \
    .FromMaybe((v8::Local<v8::Value>)Nan::Undefined())

#if 0
#include <nan.h>
static NAN_INLINE int32_t CAST_INT(v8::Local<v8::Value> v,
                                   int32_t defaultValue) {
  Nan::HandleScope scope;
  return v->IsNumber() ? Nan::To<int32_t>(v).FromMaybe(defaultValue) :
    defaultValue;
}

static NAN_INLINE bool CAST_BOOL(v8::Local<v8::Value> v, bool defaultValue) {
  Nan::HandleScope scope;
  return v->IsBoolean() ? Nan::To<bool>(v).FromMaybe(defaultValue) :
    defaultValue;
}

static NAN_INLINE v8::Local<v8::String> CAST_STRING(
    v8::Local<v8::Value> v,
    v8::Local<v8::String> defaultValue) {
  Nan::EscapableHandleScope scope;
  return scope.Escape(
      v->IsString() ? Nan::To<v8::String>(v).FromMaybe(defaultValue) :
      defaultValue);
}
#endif

/* Zend helpers */
#include <Zend/zend_modules.h>
#if ZEND_MODULE_API_NO >= 20100409
# define ZEND_HASH_KEY_DC , const zend_literal *key
# define ZEND_HASH_KEY_CC , key
# define ZEND_HASH_KEY_NULL , NULL
#else
# define ZEND_HASH_KEY_DC
# define ZEND_HASH_KEY_CC
# define ZEND_HASH_KEY_NULL
#endif

/* Method signatures of zend_update_property and zend_read_property were
 * declared as 'char *' instead of 'const char *' before PHP 5.4 */
#if ZEND_MODULE_API_NO >= 20100525
# define ZEND_CONST_CHAR
#else
# define ZEND_CONST_CHAR (char *)
#endif

#endif  // NODE_PHP_EMBED_MACROS_H_
