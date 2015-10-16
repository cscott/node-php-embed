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
    JsHasPropertyMsg(ObjectMapper *m, uint32_t objId, zval *member, int has_set_exists)
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

class JsReadPropertyMsg : public MessageToJs {
    Value object_;
    Value member_;
    int type_;
public:
    JsReadPropertyMsg(ObjectMapper* m, uint32_t objId, zval *member, int type)
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

static zval *node_php_jsobject_read_property(zval *object, zval *member, int type ZEND_HASH_KEY_DC TSRMLS_DC) {
    ZVal retval;
    if (Z_TYPE_P(member) != IS_STRING) {
        return retval.Escape();
    }
    node_php_jsobject *obj = (node_php_jsobject *)
        zend_object_store_get_object(object TSRMLS_CC);
    JsReadPropertyMsg msg(obj->channel, obj->id, member, type);
    obj->channel->Send(&msg);
    msg.WaitForResponse();
    // ok, result is in msg.retval_ or msg.exception_
    if (msg.HasException()) { msg.retval_.SetNull(); /* sigh */ }
    msg.retval_.ToPhp(obj->channel, *retval TSRMLS_CC);
    return retval.Escape();
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

void node_php_embed::node_php_jsobject_create(zval *res, ObjectMapper *mapper, uint32_t id TSRMLS_DC) {
    node_php_jsobject *c;

    object_init_ex(res, php_ce_jsobject);

    c = (node_php_jsobject *) zend_object_store_get_object(res TSRMLS_CC);

    c->channel = static_cast<JsMessageChannel *>(mapper);
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
    node_php_jsobject_handlers.has_property = node_php_jsobject_has_property;
    node_php_jsobject_handlers.read_property = node_php_jsobject_read_property;
    /*
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
