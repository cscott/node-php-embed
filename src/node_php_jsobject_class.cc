// Inspired by v8js_v8object_class in the v8js extension.

#include <nan.h>
extern "C" {
#include "php.h"
#include "Zend/zend_exceptions.h"
}

#include "node_php_jsobject_class.h"

/* Class Entries */
zend_class_entry *php_ce_jsobject;

/* Object Handlers */
static zend_object_handlers node_php_jsobject_handlers;

/* JsObject handlers */

static void node_php_jsobject_free_storage(void *object, zend_object_handle handle TSRMLS_DC) {
    node_php_jsobject *c = (node_php_jsobject *) object;

#if 0
    if (c->properties) {
        zend_hash_destroy(c->properties);
        FREE_HASHTABLE(c->properties);
        c->properties = NULL;
    }
#endif

    zend_object_std_dtor(&c->std TSRMLS_CC);

#if 0
    if (c->ctx) {
        c->v8obj.Reset();
        c->ctx->node_php_jsobjects.remove(c);
    }
#endif

    efree(object);
}

static zend_object_value node_php_jsobject_new(zend_class_entry *ce TSRMLS_DC) {
    zend_object_value retval;
    node_php_jsobject *c;

    c = (node_php_jsobject *) ecalloc(1, sizeof(*c));

    zend_object_std_init(&c->std, ce TSRMLS_CC);
    new(&c->v8obj) v8::Persistent<v8::Value>();

    retval.handle = zend_objects_store_put(c, NULL, (zend_objects_free_object_storage_t) node_php_jsobject_free_storage, NULL TSRMLS_CC);
    retval.handlers = &node_php_jsobject_handlers;

    return retval;
}

void node_php_jsobject_create(zval *res, v8::Handle<v8::Value> value, int flags, v8::Isolate *isolate TSRMLS_DC) {
#if 0
    node_php_ctx *ctx = (node_php_ctx *) isolate->GetData(0);
#endif
    node_php_jsobject *c;

    object_init_ex(res, php_ce_jsobject);

    c = (node_php_jsobject *) zend_object_store_get_object(res TSRMLS_CC);

    c->v8obj.Reset(isolate, value);
#if 0
    c->flags = flags;
    c->ctx = ctx;
    c->properties = NULL;

    ctx->node_php_jsobjects.push_front(c);
#endif
}

#define STUB_METHOD(name)                                                \
PHP_METHOD(JsObject, name) {                                             \
    zend_throw_exception(                                                \
        zend_exception_get_default(),                                    \
        "Can't directly construct, serialize, or unserialize JsObject.", \
        0 TSRMLS_CC                                                      \
    );                                                                   \
    RETURN_FALSE;                                                        \
}

/* NOTE: We could also override v8js_v8object_handlers.get_constructor
 * to throw an exception when invoked, but doing so causes the
 * half-constructed object to leak -- this seems to be a PHP bug.  So
 * we'll define magic __construct methods instead. */
STUB_METHOD(__construct)
STUB_METHOD(__sleep)
STUB_METHOD(__wakeup)

static const zend_function_entry node_php_jsobject_methods[] = {
    PHP_ME(JsObject, __construct, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
    PHP_ME(JsObject, __sleep,     NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
    PHP_ME(JsObject, __wakeup,    NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
    {NULL, NULL, NULL}
};


PHP_MINIT_FUNCTION(node_php_jsobject_class) {
    zend_class_entry ce;
    /* JsObject Class */
    INIT_CLASS_ENTRY(ce, "JsObject", node_php_jsobject_methods);
    php_ce_jsobject = zend_register_internal_class(&ce TSRMLS_CC);
    php_ce_jsobject->ce_flags |= ZEND_ACC_FINAL;
    php_ce_jsobject->create_object = node_php_jsobject_new;

    /* JsObject handlers */
    memcpy(&node_php_jsobject_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    node_php_jsobject_handlers.clone_obj = NULL;
    node_php_jsobject_handlers.cast_object = NULL;
    node_php_jsobject_handlers.get_property_ptr_ptr = NULL;
    /*
    node_php_jsobject_handlers.has_property = node_php_jsobject_has_property;
    node_php_jsobject_handlers.read_property = node_php_jsobject_read_property;
    node_php_jsobject_handlers.write_property = node_php_jsobject_write_property;
    node_php_jsobject_handlers.unset_property = node_php_jsobject_unset_property;
    node_php_jsobject_handlers.get_properties = node_php_jsobject_get_properties;
    node_php_jsobject_handlers.get_method = node_php_jsobject_get_method;
    node_php_jsobject_handlers.call_method = node_php_jsobject_call_method;
    node_php_jsobject_handlers.get_debug_info = node_php_jsobject_get_debug_info;
    node_php_jsobject_handlers.get_closure = node_php_jsobject_get_closure;
    */

    return SUCCESS;
}
