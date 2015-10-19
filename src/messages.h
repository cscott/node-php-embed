#ifndef NODE_PHP_MESSAGES_H
#define NODE_PHP_MESSAGES_H
#include <iostream>
#include "values.h"

namespace node_php_embed {

class Message {
 public:
    ObjectMapper *mapper_;
    Value retval_, exception_;
    explicit Message(ObjectMapper *mapper)
        : mapper_(mapper), retval_(), exception_() { }
    virtual ~Message() { }
    bool HasException() {
        return !exception_.IsEmpty();
    }
};

class MessageToPhp : public Message {
 protected:
    virtual void InPhp(ObjectMapper *m TSRMLS_DC) = 0;
 public:
    MessageToPhp(ObjectMapper *m) : Message(m) { }
    void ExecutePhp(TSRMLS_D) {
        InPhp(mapper_ TSRMLS_CC);
    }
};

class MessageToJs : public Message {
    uv_barrier_t *finish_;
 protected:
    // in JS context
    virtual void InJs(ObjectMapper *m) = 0;
 public:
 MessageToJs(ObjectMapper *m) : Message(m), finish_(new uv_barrier_t) {
        uv_barrier_init(finish_, 2);
        // We dynamically allocate the barrier, and deallocate it after
        // uv_barrier_wait, *not* during ~MessageToJs(), to avoid a race
        // on OSX where the PHP thread destroys the message before the JS
        // thread has been released.
    }
    // in PHP context
    void WaitForResponse() {
        // XXX invoke a recursive message loop, so JS
        // can call back into PHP while PHP is blocked.
        uv_barrier_t *b = finish_;
        if (uv_barrier_wait(b) > 0) {
            uv_barrier_destroy(b);
            delete b;
        }
    }
    // in JS context
    void ExecuteJs() {
        Nan::HandleScope scope;
        Nan::TryCatch tryCatch;
        InJs(mapper_);
        if (tryCatch.HasCaught()) {
            // If an exception was thrown, set exception_
            exception_.Set(mapper_, tryCatch.Exception());
            tryCatch.Reset();
        } else if (retval_.IsEmpty() && exception_.IsEmpty()) {
            // if no result, throw an exception
            exception_.Set(mapper_, Nan::TypeError("no return value"));
        }
        // signal completion.
        // cache the uv_barrier_t * because it's not safe to touch `this`
        // after uv_barrier_wait() returns (PHP side may have deleted object).
        uv_barrier_t *b = finish_;
        if (uv_barrier_wait(b) > 0) {
            uv_barrier_destroy(b);
            delete b;
        }
    }
};

class JsMessageChannel : public ObjectMapper {
 public:
    virtual void Send(MessageToJs *m) const = 0;
};

class JsGetPropertyMsg : public MessageToJs {
    Value obj_;
    Value name_;
 public:
    JsGetPropertyMsg(ObjectMapper *m, zval *obj, zval *name)
        : MessageToJs(m), obj_(m, obj), name_(m, name) { }
 protected:
    virtual void InJs(ObjectMapper *m) {
        Nan::MaybeLocal<v8::Object> o = Nan::To<v8::Object>(obj_.ToJs(m));
        if (o.IsEmpty()) {
            return Nan::ThrowTypeError("not an object");
        }
        Nan::MaybeLocal<v8::Value> r =
            Nan::Get(o.ToLocalChecked(), name_.ToJs(m));
        if (!r.IsEmpty()) {
            retval_.Set(m, r.ToLocalChecked());
        }
    }
};
class JsInvokeMethodMsg : public MessageToJs {
    Value obj_;
    Value name_;
    int argc_;
    Value *argv_;
 public:
    JsInvokeMethodMsg(ObjectMapper *m, zval *obj, zval *name, int argc, zval **argv)
        : MessageToJs(m), obj_(m, obj), name_(m, name), argc_(argc), argv_(Value::NewArray(m, argc, argv)) { }
    JsInvokeMethodMsg(ObjectMapper *m, zval *obj, const char *name, int argc, zval **argv)
        : MessageToJs(m), obj_(m, obj), name_(), argc_(argc), argv_(Value::NewArray(m, argc, argv)) {
        name_.SetOwnedString(name, strlen(name));
    }
    virtual ~JsInvokeMethodMsg() { delete[] argv_; }
 protected:
    virtual void InJs(ObjectMapper *m) {
        Nan::MaybeLocal<v8::Object> o = Nan::To<v8::Object>(obj_.ToJs(m));
        if (o.IsEmpty()) {
            return Nan::ThrowTypeError("not an object");
        }
        Nan::MaybeLocal<v8::Object> method = Nan::To<v8::Object>(
            Nan::Get(o.ToLocalChecked(), name_.ToJs(m))
            .FromMaybe<v8::Value>(Nan::Undefined())
        );
        if (method.IsEmpty()) {
            return Nan::ThrowTypeError("method is not an object");
        }
        v8::Local<v8::Value> *argv =
            static_cast<v8::Local<v8::Value>*>
            (alloca(sizeof(v8::Local<v8::Value>) * argc_));
        for (int i=0; i<argc_; i++) {
            new(&argv[i]) v8::Local<v8::Value>;
            argv[i] = argv_[i].ToJs(m);
        }
        Nan::MaybeLocal<v8::Value> result =
            Nan::CallAsFunction(method.ToLocalChecked(), o.ToLocalChecked(),
                                argc_, argv);
        if (!result.IsEmpty()) {
            retval_.Set(m, result.ToLocalChecked());
        }
    }
};

class PhpGetPropertyMsg : public MessageToPhp {
    Value obj_;
    Value name_;
 public:
    PhpGetPropertyMsg(ObjectMapper *m, v8::Local<v8::Value> obj, v8::Local<v8::Value> name)
        : MessageToPhp(m), obj_(m, obj), name_(m, name) { }
 protected:
    virtual void InPhp(ObjectMapper *m TSRMLS_DC) {
        ZVal obj(ZEND_FILE_LINE_C), name(ZEND_FILE_LINE_C);
        zval *r;
        zend_class_entry *ce;
        zend_property_info *property_info;

        obj_.ToPhp(m, obj TSRMLS_CC); name_.ToPhp(m, name TSRMLS_CC);
        if (!(obj.IsObject() && name.IsString())) {
            retval_.SetNull();
            return;
        }
        ce = Z_OBJCE_P(*obj);
        property_info = zend_get_property_info(ce, *name, 1 TSRMLS_CC);
        if (property_info && property_info->flags & ZEND_ACC_PUBLIC) {
            r = zend_read_property(NULL, *obj, Z_STRVAL_P(*name), Z_STRLEN_P(*name), true TSRMLS_CC);
            // special case uninitialized_zval_ptr and return an empty value
            // (indicating that we don't intercept this property) if the
            // property doesn't exist.
            if (r == EG(uninitialized_zval_ptr)) {
                retval_.SetEmpty();
                return;
            } else {
                retval_.Set(m, r);
                /* We don't own the reference to php_value... unless the
                 * returned refcount was 0, in which case the below code
                 * will free it. */
                zval_add_ref(&r);
                zval_ptr_dtor(&r);
                return;
            }
        }
        // XXX fallback to __get method
        retval_.SetNull();
    }
};

} /* namespace */

#endif
