#include <nan.h>
#include "asyncstreamworker.h"

#include <sapi/embed/php_embed.h>
#include <Zend/zend_exceptions.h>
#include <ext/standard/info.h>

#include "node_php_embed.h"
#include "node_php_jsobject_class.h"
#include "macros.h"

using namespace node_php_embed;
static void node_php_embed_ensure_init(void);

/* Per-thread storage for the module */
ZEND_BEGIN_MODULE_GLOBALS(node_php_embed)
    const AsyncStreamWorker::ExecutionStream *stream;
ZEND_END_MODULE_GLOBALS(node_php_embed)

ZEND_DECLARE_MODULE_GLOBALS(node_php_embed);

#ifdef ZTS
# define NODE_PHP_EMBED_G(v) \
    TSRMG(node_php_embed_globals_id, zend_node_php_embed_globals *, v)
#else
# define NODE_PHP_EMBED_G(v) (node_php_embed_globals.v)
#endif

// XXX async progress worker doesn't guarantee receipt of messages;
// we need to rewrite it to use guaranteed delivery.
class node_php_embed::PhpRequestWorker : public AsyncStreamWorker {
public:
    PhpRequestWorker(Nan::Callback *callback, v8::Local<v8::Object> stream, v8::Isolate *isolate, char *source)
        : AsyncStreamWorker(callback), result_(NULL) {
        size_t size = strlen(source) + 1;
        source_ = new char[size];
        memcpy(source_, source, size);
        isolate_ = isolate;
        SaveToPersistent("stream", stream);
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
    void Execute(const ExecutionStream &stream) {
        zval *retval;
        TSRMLS_FETCH();
        if (php_request_startup(TSRMLS_C) == FAILURE) {
            Nan::ThrowError("can't create request");
            return;
        }
        NODE_PHP_EMBED_G(stream) = &stream;
        ALLOC_INIT_ZVAL(retval);
        zend_first_try {
            if (FAILURE == zend_eval_string_ex(source_, retval, "request", true TSRMLS_CC)) {
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
        NODE_PHP_EMBED_G(stream) = NULL;
        php_request_shutdown(NULL);
    }
    // This is again handled in the main loop.
    void HandleStreamCallback(const char *data, size_t size) {
        Nan::HandleScope scope;
        v8::Local<v8::Object> stream = GetFromPersistent("stream")
            .As<v8::Object>();
        v8::Local<v8::Value> write = GET_PROPERTY(stream, "write");
        if (!write->IsFunction()) { return; /* silent failure */ }
        v8::Local<v8::Value> argv[] = {
            Nan::CopyBuffer(data, size).ToLocalChecked()
        };
        Nan::CallAsFunction(write.As<v8::Object>(), stream, 1, argv);
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

/* PHP extension metadata */

static int node_php_embed_ub_write(const char *str, unsigned int str_length TSRMLS_DC) {
    // Fetch the ExecutionStream object for this thread.
    const AsyncStreamWorker::ExecutionStream *stream = NODE_PHP_EMBED_G(stream);
    stream->Send(str, str_length);
    return str_length;
}

NAN_METHOD(setIniPath) {
    REQUIRE_ARGUMENT_STRING(0, iniPath);
    if (php_embed_module.php_ini_path_override) {
        delete[] php_embed_module.php_ini_path_override;
    }
    php_embed_module.php_ini_path_override = strdup(*iniPath);
}

NAN_METHOD(request) {
    REQUIRE_ARGUMENTS(3);
    REQUIRE_ARGUMENT_STRING(0, source);
    if (!*source) {
        return Nan::ThrowTypeError("bad string");
    }
    if (!info[1]->IsObject()) {
        return Nan::ThrowTypeError("stream expected");
    }
    v8::Local<v8::Object> stream = info[1].As<v8::Object>();
    if (!info[2]->IsFunction()) {
        return Nan::ThrowTypeError("callback expected");
    }
    Nan::Callback *callback = new Nan::Callback(info[2].As<v8::Function>());

    node_php_embed_ensure_init();
    Nan::AsyncQueueWorker(new PhpRequestWorker(callback, stream, v8::Isolate::GetCurrent(), *source));
}

/** PHP module housekeeping */
PHP_MINFO_FUNCTION(node_php_embed) {
    php_info_print_table_start();
    php_info_print_table_row(2, "Version", NODE_PHP_EMBED_VERSION);
    php_info_print_table_row(2, "Node version", NODE_VERSION_STRING);
    php_info_print_table_row(2, "PHP version", PHP_VERSION);
    php_info_print_table_end();
}

static void node_php_embed_globals_ctor(zend_node_php_embed_globals *node_php_embed_globals TSRMLS_DC) {
    node_php_embed_globals->stream = NULL;
}
static void node_php_embed_globals_dtor(zend_node_php_embed_globals *node_php_embed_globals TSRMLS_DC) {
    // no clean up required
}

PHP_MINIT_FUNCTION(node_php_embed) {
    PHP_MINIT(node_php_jsobject_class)(INIT_FUNC_ARGS_PASSTHRU);
    return SUCCESS;
}

zend_module_entry node_php_embed_module_entry = {
    STANDARD_MODULE_HEADER,
    "node-php-embed", /* extension name */
    NULL, /* function entries */
    PHP_MINIT(node_php_embed), /* MINIT */
    NULL, /* MSHUTDOWN */
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    PHP_MINFO(node_php_embed), /* MINFO */
    NODE_PHP_EMBED_VERSION,
    ZEND_MODULE_GLOBALS(node_php_embed),
    (void(*)(void* TSRMLS_DC))node_php_embed_globals_ctor,
    (void(*)(void* TSRMLS_DC))node_php_embed_globals_dtor,
    NULL, /* post deactivate func */
    STANDARD_MODULE_PROPERTIES_EX
};

/** Node module housekeeping */
static void ModuleShutdown(void *arg);

static bool node_php_embed_inited = false;
static void node_php_embed_ensure_init(void) {
    if (node_php_embed_inited) {
        return;
    }
    node_php_embed_inited = true;
    TSRMLS_FETCH();
    char *argv[] = { };
    int argc = 0;
    php_embed_init(argc, argv PTSRMLS_CC);
    // shutdown the initially-created request
    php_request_shutdown(NULL);
    zend_startup_module(&node_php_embed_module_entry);
    node::AtExit(ModuleShutdown, NULL);
}

NAN_MODULE_INIT(ModuleInit) {
    php_embed_module.php_ini_path_override = NULL;
    php_embed_module.php_ini_ignore = true;
    php_embed_module.php_ini_ignore_cwd = true;
    php_embed_module.ini_defaults = NULL;
    php_embed_module.ub_write = node_php_embed_ub_write;
    // Most of init will be lazily in node_php_embed_ensure_init()

    // Export functions
    NAN_EXPORT(target, setIniPath);
    NAN_EXPORT(target, request);
}

void ModuleShutdown(void *arg) {
    TSRMLS_FETCH();
    php_request_startup(TSRMLS_C);
    php_embed_shutdown(TSRMLS_C);
    if (php_embed_module.php_ini_path_override) {
        delete[] php_embed_module.php_ini_path_override;
    }
}

NODE_MODULE(node_php_embed, ModuleInit)
