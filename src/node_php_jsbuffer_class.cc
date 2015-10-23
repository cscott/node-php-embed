// This is a PHP class which wraps a node Buffer object.
// This helps paper over a difference between PHP and JS: PHP strings
// are bytestrings (like node Buffers); JavaScript strings are unicode
// strings (nominally encoded in UTF-8 when converted to bytestrings).
// In order to avoid incorrect decoding/encoding, we sometimes must
// pass strings from PHP to Node as Buffer objects, not String objects.
// This class provides a PHP-accessible wrapper to allow doing so.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_jsbuffer_class.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
#include "Zend/zend_exceptions.h"
}

#include "src/macros.h"

using node_php_embed::OwnershipType;
using node_php_embed::node_php_jsbuffer;

/* Class entries */
zend_class_entry *php_ce_jsbuffer;

/*Object handlers */
static zend_object_handlers node_php_jsbuffer_handlers;

/* Constructors and destructors */
static void node_php_jsbuffer_free_storage(
    void *object,
    zend_object_handle handle TSRMLS_DC) {
  TRACE(">");
  node_php_jsbuffer *c = reinterpret_cast<node_php_jsbuffer *>(object);
  TRACEX("- dealloc %p", c);

  zend_object_std_dtor(&c->std TSRMLS_CC);
  if (c->z) {
    TRACEX("- freeing zval refcount %d", Z_REFCOUNT_P(c->z));
    zval_ptr_dtor(&(c->z));
  }
  if (c->owner == OwnershipType::PHP_OWNED) {
    TRACE("- freeing PHP owned data");
    efree(const_cast<char*>(c->data));
  } else if (c->owner == OwnershipType::CPP_OWNED) {
    TRACE("- freeing C++ owned data");
    delete[] c->data;
  }

  efree(object);
  TRACE("<");
}

static zend_object_value node_php_jsbuffer_new(zend_class_entry *ce TSRMLS_DC) {
  TRACE(">");
  zend_object_value retval;
  node_php_jsbuffer *c;

  c = reinterpret_cast<node_php_jsbuffer *>(ecalloc(1, sizeof(*c)));
  TRACEX("- alloc %p", c);

  zend_object_std_init(&c->std, ce TSRMLS_CC);

  retval.handle = zend_objects_store_put(
      c, NULL,
      (zend_objects_free_object_storage_t) node_php_jsbuffer_free_storage,
      NULL TSRMLS_CC);
  retval.handlers = &node_php_jsbuffer_handlers;

  TRACE("<");
  return retval;
}

void node_php_embed::node_php_jsbuffer_create(zval *res,
                                              const char *data, ulong length,
                                              OwnershipType owner TSRMLS_DC) {
  TRACE(">");

  object_init_ex(res, php_ce_jsbuffer);

  node_php_jsbuffer *c = reinterpret_cast<node_php_jsbuffer *>
    (zend_object_store_get_object(res TSRMLS_CC));

  if (owner != OwnershipType::NOT_OWNED) {
    char *tmp = (owner == OwnershipType::PHP_OWNED) ?
      reinterpret_cast<char*>(ecalloc(length, 1)) :
      new char[length];  // NOLINT(runtime/arrays)
    memcpy(tmp, data, length);
    data = tmp;
  }
  c->data = data;
  c->length = length;
  c->owner = owner;
  c->z = NULL;

  TRACE("<");
}

/* Methods */
#define PARSE_PARAMS(method, ...)                                       \
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, __VA_ARGS__) ==  \
      FAILURE) {                                                        \
    zend_throw_exception(zend_exception_get_default(TSRMLS_C),          \
                         "bad args to " #method, 0 TSRMLS_CC);          \
    return;                                                             \
  }                                                                     \

ZEND_BEGIN_ARG_INFO_EX(node_php_jsbuffer_construct_args, 0, 0, 1)
  ZEND_ARG_INFO(0, str)
ZEND_END_ARG_INFO()

PHP_METHOD(JsBuffer, __construct) {
  TRACE(">");
  node_php_jsbuffer *obj = reinterpret_cast<node_php_jsbuffer *>
    (zend_object_store_get_object(this_ptr TSRMLS_CC));
  zval *str;
  PARSE_PARAMS(__construct, "z/", &str);
  convert_to_string(str);
  obj->z = str; Z_ADDREF_P(str);
  obj->data = Z_STRVAL_P(str);
  obj->length = Z_STRLEN_P(str);
  // Mark this as not owned directly; the ref to str will take care of it.
  obj->owner = OwnershipType::NOT_OWNED;
  TRACE("<");
}

ZEND_BEGIN_ARG_INFO_EX(node_php_jsbuffer_toString_args, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(JsBuffer, __toString) {
  node_php_jsbuffer *obj = reinterpret_cast<node_php_jsbuffer *>
    (zend_object_store_get_object(this_ptr TSRMLS_CC));
  RETURN_STRINGL(obj->data, obj->length, 1);
}

ZEND_BEGIN_ARG_INFO_EX(node_php_jsbuffer_debugInfo_args, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(JsBuffer, __debugInfo) {
  node_php_jsbuffer *obj = reinterpret_cast<node_php_jsbuffer *>
    (zend_object_store_get_object(this_ptr TSRMLS_CC));
  array_init_size(return_value, 1);
  add_assoc_stringl_ex(return_value, "value", 6,
                       const_cast<char*>(obj->data), obj->length, 1);
}

#define STUB_METHOD(name)                                               \
  PHP_METHOD(JsBuffer, name) {                                          \
    TRACE(">");                                                         \
    zend_throw_exception(                                               \
        zend_exception_get_default(TSRMLS_C),                           \
        "Can't directly serialize or unserialize JsBuffer.",            \
        0 TSRMLS_CC);                                                   \
    TRACE("<");                                                         \
    RETURN_FALSE;                                                       \
  }

/* NOTE: We could also override node_php_jsbuffer_handlers.get_constructor
 * to throw an exception when invoked, but doing so causes the
 * half-constructed object to leak -- this seems to be a PHP bug.  So
 * we'll define magic __construct methods instead. */
STUB_METHOD(__sleep)
STUB_METHOD(__wakeup)

static const zend_function_entry node_php_jsbuffer_methods[] = {
  PHP_ME(JsBuffer, __construct, node_php_jsbuffer_construct_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
  PHP_ME(JsBuffer, __sleep,     NULL,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsBuffer, __wakeup,    NULL,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsBuffer, __toString, node_php_jsbuffer_toString_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsBuffer, __debugInfo, node_php_jsbuffer_debugInfo_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  ZEND_FE_END
};

PHP_MINIT_FUNCTION(node_php_jsbuffer_class) {
  TRACE("> PHP_MINIT_FUNCTION");
  zend_class_entry ce;
  /* JsBuffer class */
  INIT_CLASS_ENTRY(ce, "JsBuffer", node_php_jsbuffer_methods);
  php_ce_jsbuffer = zend_register_internal_class(&ce TSRMLS_CC);
  php_ce_jsbuffer->ce_flags |= ZEND_ACC_FINAL;
  php_ce_jsbuffer->create_object = node_php_jsbuffer_new;

  /* JsBuffer handlers */
  memcpy(&node_php_jsbuffer_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  node_php_jsbuffer_handlers.clone_obj = NULL;
  node_php_jsbuffer_handlers.cast_object = NULL;
  node_php_jsbuffer_handlers.get_property_ptr_ptr = NULL;

  TRACE("< PHP_MINIT_FUNCTION");
  return SUCCESS;
}
