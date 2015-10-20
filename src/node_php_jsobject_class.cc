// Inspired by v8js_v8object_class in the v8js extension.

#include <nan.h>
extern "C" {
#include "php.h"
#include "Zend/zend.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_types.h"
}

#include "node_php_jsobject_class.h"
#include "messages.h"
#include "values.h"
#include "macros.h"

#define USE_MAGIC_ISSET 0

using namespace node_php_embed;

/* Class Entries */
zend_class_entry *php_ce_jsobject;

/* Object Handlers */
static zend_object_handlers node_php_jsobject_handlers;


/* JsObject handlers */
class JsHasPropertyMsg : public MessageToJs {
    Value object_;
    Value member_;
    int has_set_exists_;
public:
    JsHasPropertyMsg(ObjectMapper *m, objid_t objId, zval *member, int has_set_exists)
        : MessageToJs(m), object_(), member_(m, member),
          has_set_exists_(has_set_exists) {
        object_.SetJsObject(objId);
    }
protected:
    virtual void InJs(ObjectMapper *m) {
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
                if (has_set_exists_ == 0 ) {
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
    }
};

#if USE_MAGIC_ISSET

PHP_METHOD(JsObject, __isset) {
    zval *member;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z/", &member) == FAILURE) {
        zend_throw_exception(
            zend_exception_get_default(TSRMLS_C),
            "bad property name for __isset", 0 TSRMLS_CC);
        return;
    }
    convert_to_string(member);
    node_php_jsobject *obj = (node_php_jsobject *)
        zend_object_store_get_object(this_ptr TSRMLS_CC);
    JsHasPropertyMsg msg(obj->channel, obj->id, member, 0);
    obj->channel->Send(&msg);
    msg.WaitForResponse();
    // ok, result is in msg.retval_ or msg.exception_
    if (msg.HasException()) {
         zend_throw_exception_ex(
            zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
            "JS exception thrown during __isset of \"%*s\"",
            Z_STRLEN_P(member), Z_STRVAL_P(member));
        return;
    }
    RETURN_BOOL(msg.retval_.AsBool());
}

#else
// By overriding has_property we can implement property_exists correctly,
// and also handle empty arrays.
static int node_php_jsobject_has_property(zval *object, zval *member, int has_set_exists ZEND_HASH_KEY_DC TSRMLS_DC) {
    /* param has_set_exists:
     * 0 (has) whether property exists and is not NULL  - isset()
     * 1 (set) whether property exists and is true-ish  - empty()
     * 2 (exists) whether property exists               - property_exists()
     */
    if (Z_TYPE_P(member) != IS_STRING) {
        return false;
    }
    node_php_jsobject *obj = (node_php_jsobject *)
        zend_object_store_get_object(object TSRMLS_CC);
    JsHasPropertyMsg msg(obj->channel, obj->id, member, has_set_exists);
    obj->channel->Send(&msg);
    msg.WaitForResponse();
    // ok, result is in msg.retval_ or msg.exception_
    if (msg.HasException()) { return false; /* sigh */ }
    return msg.retval_.AsBool();
}
#endif /* USE_MAGIC_ISSET */

class JsReadPropertyMsg : public MessageToJs {
    Value object_;
    Value member_;
    int type_;
public:
    JsReadPropertyMsg(ObjectMapper* m, objid_t objId, zval *member, int type)
        : MessageToJs(m), object_(), member_(m, member), type_(type) {
        object_.SetJsObject(objId);
    }
protected:
    virtual void InJs(ObjectMapper *m) {
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
    }
};


PHP_METHOD(JsObject, __get) {
    zval *member;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z/", &member) == FAILURE) {
        zend_throw_exception(zend_exception_get_default(TSRMLS_C), "bad property name", 0 TSRMLS_CC);
        return;
    }
    convert_to_string(member);
    node_php_jsobject *obj = (node_php_jsobject *)
        zend_object_store_get_object(this_ptr TSRMLS_CC);
    JsReadPropertyMsg msg(obj->channel, obj->id, member, 0);
    obj->channel->Send(&msg);
    msg.WaitForResponse();
    // ok, result is in msg.retval_ or msg.exception_
    if (msg.HasException()) {
         zend_throw_exception_ex(
            zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
            "JS exception thrown during __get of \"%*s\"",
            Z_STRLEN_P(member), Z_STRVAL_P(member));
        return;
    }
    msg.retval_.ToPhp(obj->channel, return_value, return_value_ptr TSRMLS_CC);
}

/* Use (slightly thunked) versions of the has/read/write property handlers
 * for dimensions as well, so that $obj['foo'] acts like $obj->foo. */
static int node_php_jsobject_has_dimension(zval *obj, zval *idx, int chk_type TSRMLS_DC) {
    // thunk!
    if (chk_type == 0) { chk_type = 2; }
    // use standard has_property method with new chk_type
    return node_php_jsobject_handlers.has_property(obj, idx, chk_type ZEND_HASH_KEY_NULL TSRMLS_CC);
}
static zval *node_php_jsobject_read_dimension(zval *obj, zval *off, int type TSRMLS_DC) {
    // use standard read_property method
    return node_php_jsobject_handlers.read_property(obj, off, type ZEND_HASH_KEY_NULL TSRMLS_CC);
}
static void node_php_jsobject_write_dimension(zval *obj, zval *off, zval *val TSRMLS_DC) {
    // use standard write_property method
    node_php_jsobject_handlers.write_property(obj, off, val ZEND_HASH_KEY_NULL TSRMLS_CC);
}
static void node_php_jsobject_unset_dimension(zval *obj, zval *off TSRMLS_DC) {
    // use standard unset_property method
    node_php_jsobject_handlers.unset_property(obj, off ZEND_HASH_KEY_NULL TSRMLS_CC);
}

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

    // XXX after we ensure that only one php wrapper for a given js id
    // is created, then we could deregister the id on _free().
    // XXX actually, we need to send a message to the JS side first
    // to prevent a race, since the same JS object might get used in
    // another JS->PHP call first, which would revive the PHP-side wrapper.

    efree(object);
}

static zend_object_value node_php_jsobject_new(zend_class_entry *ce TSRMLS_DC) {
    zend_object_value retval;
    node_php_jsobject *c;

    c = (node_php_jsobject *) ecalloc(1, sizeof(*c));

    zend_object_std_init(&c->std, ce TSRMLS_CC);

    retval.handle = zend_objects_store_put(c, NULL, (zend_objects_free_object_storage_t) node_php_jsobject_free_storage, NULL TSRMLS_CC);
    retval.handlers = &node_php_jsobject_handlers;

    return retval;
}

void node_php_embed::node_php_jsobject_create(zval *res, JsMessageChannel *channel, objid_t id TSRMLS_DC) {
    node_php_jsobject *c;

    object_init_ex(res, php_ce_jsobject);

    c = (node_php_jsobject *) zend_object_store_get_object(res TSRMLS_CC);

    c->channel = channel;
    c->id = id;
#if 0
    c->flags = flags;
    c->properties = NULL;

    ctx->node_php_jsobjects.push_front(c);
#endif
}

#define STUB_METHOD(name)                                                \
PHP_METHOD(JsObject, name) {                                             \
    zend_throw_exception(                                                \
        zend_exception_get_default(TSRMLS_C),                            \
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

ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_one_arg, 0, 0, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(node_php_jsobject_one_arg_retref, 0, 1, 1)
    ZEND_ARG_INFO(0, member)
ZEND_END_ARG_INFO()

static const zend_function_entry node_php_jsobject_methods[] = {
    PHP_ME(JsObject, __construct, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
    PHP_ME(JsObject, __sleep,     NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
    PHP_ME(JsObject, __wakeup,    NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
#if USE_MAGIC_ISSET
    PHP_ME(JsObject, __isset, node_php_jsobject_one_arg, ZEND_ACC_PUBLIC)
#endif
    PHP_ME(JsObject, __get, node_php_jsobject_one_arg_retref, ZEND_ACC_PUBLIC)
    ZEND_FE_END
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
#if !USE_MAGIC_ISSET
    node_php_jsobject_handlers.has_property = node_php_jsobject_has_property;
#endif
    //node_php_jsobject_handlers.read_property = node_php_jsobject_read_property;
    /*
    node_php_jsobject_handlers.write_property = node_php_jsobject_write_property;
    node_php_jsobject_handlers.unset_property = node_php_jsobject_unset_property;
    node_php_jsobject_handlers.get_properties = node_php_jsobject_get_properties;
    node_php_jsobject_handlers.get_method = node_php_jsobject_get_method;
    node_php_jsobject_handlers.call_method = node_php_jsobject_call_method;
    node_php_jsobject_handlers.get_debug_info = node_php_jsobject_get_debug_info;
    node_php_jsobject_handlers.get_closure = node_php_jsobject_get_closure;
    */

    /* Array access handlers: slightly thunked versions of property handlers. */
    node_php_jsobject_handlers.read_dimension = node_php_jsobject_read_dimension;
    node_php_jsobject_handlers.write_dimension = node_php_jsobject_write_dimension;
    node_php_jsobject_handlers.has_dimension = node_php_jsobject_has_dimension;
    node_php_jsobject_handlers.unset_dimension = node_php_jsobject_unset_dimension;

    return SUCCESS;
}
