// This is a thin wrapper object to allow javascript to call
// php_register_variable_* inside an initialization callback.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_jsserver_class.h"

extern "C" {
#include "main/php.h"
#include "main/php_variables.h"
#include "Zend/zend.h"
#include "Zend/zend_exceptions.h"
}

#include "src/macros.h"
#include "src/values.h"

using node_php_embed::node_php_jsserver;

/* Class entries */
zend_class_entry *php_ce_jsserver;

/*Object handlers */
static zend_object_handlers node_php_jsserver_handlers;

/* Constructors and destructors */
static void node_php_jsserver_free_storage(
    void *object,
    zend_object_handle handle TSRMLS_DC) {
  TRACE(">");
  node_php_jsserver *c = reinterpret_cast<node_php_jsserver *>(object);

  zend_object_std_dtor(&c->std TSRMLS_CC);
  zval_ptr_dtor(&(c->track_vars_array));
  efree(object);
  TRACE("<");
}

static zend_object_value node_php_jsserver_new(zend_class_entry *ce TSRMLS_DC) {
  TRACE(">");
  zend_object_value retval;
  node_php_jsserver *c;

  c = reinterpret_cast<node_php_jsserver *>(ecalloc(1, sizeof(*c)));

  zend_object_std_init(&c->std, ce TSRMLS_CC);

  retval.handle = zend_objects_store_put(
      c, nullptr,
      (zend_objects_free_object_storage_t) node_php_jsserver_free_storage,
      nullptr TSRMLS_CC);
  retval.handlers = &node_php_jsserver_handlers;

  TRACE("<");
  return retval;
}

void node_php_embed::node_php_jsserver_create(zval *res, zval *track_vars_array
                                              TSRMLS_DC) {
  TRACE(">");

  object_init_ex(res, php_ce_jsserver);

  node_php_jsserver *c = reinterpret_cast<node_php_jsserver *>
    (zend_object_store_get_object(res TSRMLS_CC));

  c->track_vars_array = track_vars_array;
  Z_ADDREF_P(c->track_vars_array);

  TRACE("<");
}

/* Methods */
#define FETCH_OBJ_ELSE(method, this_ptr, defaultValue)                  \
  node_php_jsserver *obj = reinterpret_cast<node_php_jsserver *>        \
    (zend_object_store_get_object(this_ptr TSRMLS_CC))
#define FETCH_OBJ(method, this_ptr) FETCH_OBJ_ELSE(method, this_ptr, )

#define PARSE_PARAMS(method, ...)                                       \
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, __VA_ARGS__) ==  \
      FAILURE) {                                                        \
    zend_throw_exception(zend_exception_get_default(TSRMLS_C),          \
                         "bad args to " #method, 0 TSRMLS_CC);          \
    return;                                                             \
  }                                                                     \
  FETCH_OBJ(method, this_ptr)

ZEND_BEGIN_ARG_INFO_EX(node_php_jsserver_get_args, 0, 1/*return by ref*/, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()

PHP_METHOD(JsServer, __get) {
  TRACE(">");
#if 0  // XXX disabled, since it seems to be corrupting memory.
  zval *member;
  PARSE_PARAMS(__get, "z/", &member);
  convert_to_string(member);

  zval_ptr_dtor(&return_value);
  zval **retval;
  if (FAILURE ==
      zend_symtable_find(Z_ARRVAL_P(obj->track_vars_array),
                         Z_STRVAL_P(member), Z_STRLEN_P(member) + 1,
                         reinterpret_cast<void**>(&retval))) {
    *return_value_ptr = EG(uninitialized_zval_ptr);
  } else {
      *return_value_ptr = *retval;
  }
#else
  ZVAL_BOOL(return_value, true);
#endif
  TRACE("<");
}

ZEND_BEGIN_ARG_INFO_EX(node_php_jsserver_set_args, 0, 0, 2)
    ZEND_ARG_INFO(0, member)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

PHP_METHOD(JsServer, __set) {
  zval *member; zval *zv;
  TRACE(">");
  PARSE_PARAMS(__set, "z/z/", &member, &zv);
  node_php_embed::ZVal value(zv ZEND_FILE_LINE_CC);
  convert_to_string(member);
  // This is the whole point of this class!
  php_register_variable_ex(Z_STRVAL_P(member), value.Transfer(TSRMLS_C),
                           obj->track_vars_array TSRMLS_CC);
  ZVAL_BOOL(return_value, true);
  TRACE("<");
}

#define STUB_METHOD(name)                                               \
  PHP_METHOD(JsServer, name) {                                          \
    TRACE(">");                                                         \
    zend_throw_exception(                                               \
        zend_exception_get_default(TSRMLS_C),                           \
        "Can't directly construct, serialize or unserialize JsServer.", \
        0 TSRMLS_CC);                                                   \
    TRACE("<");                                                         \
    RETURN_FALSE;                                                       \
  }

/* NOTE: We could also override node_php_jsserver_handlers.get_constructor
 * to throw an exception when invoked, but doing so causes the
 * half-constructed object to leak -- this seems to be a PHP bug.  So
 * we'll define magic __construct methods instead. */
STUB_METHOD(__construct)
STUB_METHOD(__sleep)
STUB_METHOD(__wakeup)

static const zend_function_entry node_php_jsserver_methods[] = {
  PHP_ME(JsServer, __construct,     nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsServer, __sleep,     nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsServer, __wakeup,    nullptr,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsServer, __get, node_php_jsserver_get_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  PHP_ME(JsServer, __set, node_php_jsserver_set_args,
         ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
  ZEND_FE_END
};

PHP_MINIT_FUNCTION(node_php_jsserver_class) {
  TRACE("> PHP_MINIT_FUNCTION");
  zend_class_entry ce;
  /* JsServer class */
  INIT_CLASS_ENTRY(ce, "Js\\Server", node_php_jsserver_methods);
  php_ce_jsserver = zend_register_internal_class(&ce TSRMLS_CC);
  php_ce_jsserver->ce_flags |= ZEND_ACC_FINAL;
  php_ce_jsserver->create_object = node_php_jsserver_new;

  /* JsServer handlers */
  memcpy(&node_php_jsserver_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  node_php_jsserver_handlers.clone_obj = nullptr;
  node_php_jsserver_handlers.cast_object = nullptr;
  node_php_jsserver_handlers.get_property_ptr_ptr = nullptr;

  TRACE("< PHP_MINIT_FUNCTION");
  return SUCCESS;
}
