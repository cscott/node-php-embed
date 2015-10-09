#include <nan.h>
#include <sapi/embed/php_embed.h>
#include <Zend/zend_exceptions.h>
#include "macros.h"

static void ModuleShutdown(void *arg);

class PhpRequestWorker : public Nan::AsyncWorker {
public:
    PhpRequestWorker(Nan::Callback *callback, v8::Isolate *isolate, char *source)
        : AsyncWorker(callback), result_(NULL) {
        size_t size = strlen(source) + 1;
        source_ = new char[size];
        memcpy(source_, source, size);
        isolate_ = isolate;
    }
    ~PhpRequestWorker() {
        delete[] source_;
        if (result_) {
            delete[] result_;
        }
    }

    // Executed inside the PHP thread.  It is not safe to access V8 or
    // V8 data structures here, so everything we need for input and output
    // should go on `this`.
    void Execute() {
        TSRMLS_FETCH();
        if (php_request_startup(TSRMLS_C) == FAILURE) {
            Nan::ThrowError("can't create request");
            return;
        }
        zval *retval;
        ALLOC_INIT_ZVAL(retval);
        zend_first_try {
            if (FAILURE == zend_eval_string_ex(source_, retval, "request", true)) {
                if (EG(exception)) {
                    zend_clear_exception(TSRMLS_C);
                    SetErrorMessage("<threw exception");
                } else {
                    SetErrorMessage("<eval failure>");
                }
            }
            convert_to_string(retval);
            result_ = new char[Z_STRLEN_P(retval) + 1];
            memcpy(result_, Z_STRVAL_P(retval), Z_STRLEN_P(retval));
            result_[Z_STRLEN_P(retval)] = 0;
        } zend_catch {
            SetErrorMessage("<bailout>");
        } zend_end_try();
        zval_dtor(retval);
        php_request_shutdown(NULL);
    }
    // Executed when the async work is complete.
    // This function will be run inside the main event loop
    // so it is safe to use V8 again.
    void HandleOKCallback() {
        Nan::HandleScope scope;
        v8::Local<v8::Value> argv[] = {
            Nan::Null(),
            Nan::New<v8::String>(result_ ? result_ : "").ToLocalChecked()
        };
        callback->Call(2, argv);
    }
private:
    char *source_;
    char *result_;
    v8::Isolate *isolate_;
};

NAN_METHOD(request) {

    REQUIRE_ARGUMENTS(4);
    REQUIRE_ARGUMENT_STRING(0, iniPath);//XXX too late?
    REQUIRE_ARGUMENT_STRING(1, source);
    if (!*source) {
        return Nan::ThrowTypeError("bad string");
    }
    if (!info[2]->IsObject()) {
        return Nan::ThrowTypeError("stream expected");
    }
    if (!info[3]->IsFunction()) {
        return Nan::ThrowTypeError("callback expected");
    }
    Nan::Callback *callback = new Nan::Callback(info[3].As<v8::Function>());

    Nan::AsyncQueueWorker(new PhpRequestWorker(callback, v8::Isolate::GetCurrent(), *source));
}

NAN_MODULE_INIT(ModuleInit) {
    TSRMLS_FETCH();
    char *argv[] = { };
    int argc = 0;
    php_embed_module.php_ini_ignore = true;
    php_embed_init(argc, argv PTSRMLS_CC);
    // shutdown the initially-created request
    php_request_shutdown(NULL);
    node::AtExit(ModuleShutdown, NULL);
    Nan::Set(target, NEW_STR("request"),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(request)).ToLocalChecked());
}

void ModuleShutdown(void *arg) {
    TSRMLS_FETCH();
    php_request_startup(TSRMLS_C);
    php_embed_shutdown(TSRMLS_CC);
}


NODE_MODULE(php_embed, ModuleInit)
