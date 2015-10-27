// This is an opaque object, which can be created in PHP code, which
// can be passed to JavaScript functions in the slot normally occupied
// by the callback.  It is converted into a callback which blocks the
// PHP thread until it is resolved, and the value passed to the callback
// is substituted for the value returned by the function.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_jswait_class.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
#include "Zend/zend_exceptions.h"
}

#include "src/macros.h"

using node_php_embed::node_php_jswait;

/* Class entries */
zend_class_entry *php_ce_jswait;

/*Object handlers */
static zend_object_handlers node_php_jswait_handlers;

/* Constructors and destructors */
static void node_php_jswait_free_storage(
    void *object,
    zend_object_handle handle TSRMLS_DC) {
  TRACE(">");
  node_php_jswait *c = reinterpret_cast<node_php_jswait *>(object);

  zend_object_std_dtor(&c->std TSRMLS_CC);
  efree(object);
  TRACE("<");
}

static zend_object_value node_php_jswait_new(zend_class_entry *ce TSRMLS_DC) {
  TRACE(">");
  zend_object_value retval;
  node_php_jswait *c;

  c = reinterpret_cast<node_php_jswait *>(ecalloc(1, sizeof(*c)));

  zend_object_std_init(&c->std, ce TSRMLS_CC);

  retval.handle = zend_objects_store_put(
      c, nullptr,
      (zend_objects_free_object_storage_t) node_php_jswait_free_storage,
      nullptr TSRMLS_CC);
  retval.handlers = &node_php_jswait_handlers;

  TRACE("<");
  return retval;
}

void node_php_embed::node_php_jswait_create(zval *res TSRMLS_DC) {
  TRACE(">");

  object_init_ex(res, php_ce_jswait);

#if 0
  node_php_jswait *c = reinterpret_cast<node_php_jswait *>
    (zend_object_store_get_object(res TSRMLS_CC));

  // Normally we'd initialize the fields of `c` here, but there's
  // really nothing to do in this case.
#endif

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

ZEND_BEGIN_ARG_INFO_EX(node_php_jswait_construct_args, 0, 0, 1)
  ZEND_ARG_INFO(0, str)
ZEND_END_ARG_INFO()

PHP_METHOD(JsWait, __construct) {
  TRACE(">");
#if 0
  node_php_jswait *obj = reinterpret_cast<node_php_jswait *>
    (zend_object_store_get_object(this_ptr TSRMLS_CC));

  // Normally we'd parse the arguments and stash the data somewhere
  // in `obj`, but nothing to do in this case.
#endif
  TRACE("<");
}

#define STUB_METHOD(name)                                               \
  PHP_METHOD(JsWait, name) {                                          \
    TRACE(">");                                                         \
    zend_throw_exception(                                               \
        zend_exception_get_default(TSRMLS_C),                           \
        "Can't directly serialize or unserialize JsWait.",            \
        0 TSRMLS_CC);                                                   \
    TRACE("<");                                                         \
    RETURN_FALSE;                                                       \
  }

/* NOTE: We could also override node_php_jswait_handlers.get_constructor
 * to throw an exception when invoked, but doing so causes the
 * half-constructed object to leak -- this seems to be a PHP bug.  So
 * we'll define magic __construct methods instead. */
STUB_METHOD(__sleep)
STUB_METHOD(__wakeup)

static const zend_function_entry node_php_jswait_methods[] = {
  PHP_ME(JsWait, __construct, node_php_jswait_construct_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
  PHP_ME(JsWait, __sleep,     nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsWait, __wakeup,    nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  ZEND_FE_END
};

PHP_MINIT_FUNCTION(node_php_jswait_class) {
  TRACE("> PHP_MINIT_FUNCTION");
  zend_class_entry ce;
  /* JsWait class */
  INIT_CLASS_ENTRY(ce, "Js\\Wait", node_php_jswait_methods);
  php_ce_jswait = zend_register_internal_class(&ce TSRMLS_CC);
  php_ce_jswait->ce_flags |= ZEND_ACC_FINAL;
  php_ce_jswait->create_object = node_php_jswait_new;

  /* JsWait handlers */
  memcpy(&node_php_jswait_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  node_php_jswait_handlers.clone_obj = nullptr;
  node_php_jswait_handlers.cast_object = nullptr;
  node_php_jswait_handlers.get_property_ptr_ptr = nullptr;

  TRACE("< PHP_MINIT_FUNCTION");
  return SUCCESS;
}
