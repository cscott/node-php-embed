// Main entry point: this is the node module declaration, contains
// the PhpRequestWorker which shuttles messages between node and PHP,
// and contains the SAPI hooks to configure PHP to talk to node.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_embed.h"

#include "nan.h"

extern "C" {
#include "sapi/embed/php_embed.h"
#include "Zend/zend_exceptions.h"
#include "ext/standard/info.h"
}

#include "src/asyncmessageworker.h"
#include "src/macros.h"
#include "src/messages.h"
#include "src/node_php_jsobject_class.h"
#include "src/node_php_jsbuffer_class.h"
#include "src/values.h"

using node_php_embed::MapperChannel;
using node_php_embed::OwnershipType;
using node_php_embed::PhpRequestWorker;
using node_php_embed::ZVal;

static void node_php_embed_ensure_init(void);

/* Per-thread storage for the module */
ZEND_BEGIN_MODULE_GLOBALS(node_php_embed)
  PhpRequestWorker *worker;
  MapperChannel *channel;
ZEND_END_MODULE_GLOBALS(node_php_embed)

ZEND_DECLARE_MODULE_GLOBALS(node_php_embed);

#ifdef ZTS
# define NODE_PHP_EMBED_G(v)                    \
  TSRMG(node_php_embed_globals_id, zend_node_php_embed_globals *, v)
#else
# define NODE_PHP_EMBED_G(v)                    \
  (node_php_embed_globals.v)
#endif

class node_php_embed::PhpRequestWorker : public AsyncMessageWorker {
 public:
  PhpRequestWorker(Nan::Callback *callback, v8::Local<v8::Object> stream,
                   v8::Local<v8::Value> context, char *source)
      : AsyncMessageWorker(callback), result_(NULL), stream_(), context_() {
    size_t size = strlen(source) + 1;
    source_ = new char[size];
    memcpy(source_, source, size);
    JsStartupMapper mapper(this);
    stream_.Set(&mapper, stream);
    context_.Set(&mapper, context);
  }
  virtual ~PhpRequestWorker() {
    delete[] source_;
    if (result_) {
      delete[] result_;
    }
  }
  Value &GetStream() { return stream_; }
  Value &GetContext() { return context_; }

  // Executed inside the PHP thread.  It is not safe to access V8 or
  // V8 data structures here, so everything we need for input and output
  // should go on `this`.
  virtual void Execute(MapperChannel *channel TSRMLS_DC) {
    TRACE("> PhpRequestWorker");
    if (php_request_startup(TSRMLS_C) == FAILURE) {
      Nan::ThrowError("can't create request");
      return;
    }
    NODE_PHP_EMBED_G(worker) = this;
    NODE_PHP_EMBED_G(channel) = channel;
    {
      ZVal retval{ZEND_FILE_LINE_C};
      zend_first_try {
        char eval_msg[] = { "request" };  // This shows up in error messages.
        if (FAILURE == zend_eval_string_ex(source_, *retval, eval_msg,
                                           true TSRMLS_CC)) {
          if (EG(exception)) {
            zend_clear_exception(TSRMLS_C);
            SetErrorMessage("<threw exception>");
          } else {
            SetErrorMessage("<eval failure>");
          }
        }
        convert_to_string(*retval);
        result_ = new char[Z_STRLEN_P(*retval) + 1];
        memcpy(result_, Z_STRVAL_P(*retval), Z_STRLEN_P(*retval));
        result_[Z_STRLEN_P(*retval)] = 0;
      } zend_catch {
        SetErrorMessage("<bailout>");
      } zend_end_try();
    }  // End of scope releases retval.
    // After we return, the queues will be emptied, which might
    // end up running some additional PHP code.
    // The remainder of cleanup is done in AfterExecute after
    // the queues have been emptied.
    TRACE("< PhpRequestWorker");
  }
  virtual void AfterExecute(TSRMLS_D) {
    TRACE("> PhpRequestWorker");
    NODE_PHP_EMBED_G(worker) = NULL;
    NODE_PHP_EMBED_G(channel) = NULL;
    TRACE("- request shutdown");
    php_request_shutdown(NULL);
    TRACE("< PhpRequestWorker");
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
  Value stream_;
  Value context_;
};

/* PHP extension metadata */
extern zend_module_entry node_php_embed_module_entry;

static int node_php_embed_startup(sapi_module_struct *sapi_module) {
  TRACE(">");
  if (php_module_startup(sapi_module, &node_php_embed_module_entry, 1) ==
      FAILURE) {
    return FAILURE;
  }
  TRACE("<");
  return SUCCESS;
}

static int node_php_embed_ub_write(const char *str,
                                   unsigned int str_length TSRMLS_DC) {
  TRACE(">");
  // Fetch the MapperChannel for this thread.
  PhpRequestWorker *worker = NODE_PHP_EMBED_G(worker);
  MapperChannel *channel = NODE_PHP_EMBED_G(channel);
  ZVal stream{ZEND_FILE_LINE_C}, retval{ZEND_FILE_LINE_C};
  worker->GetStream().ToPhp(channel, stream TSRMLS_CC);
  // Use plain zval to avoid allocating copy of method name.
  zval method; ZVAL_STRINGL(&method, "write", 5, 0);
  // Special buffer type to pass `str` as a node buffer and avoid copying.
  zval buffer, *args[] = { &buffer }; INIT_ZVAL(buffer);
  node_php_embed::node_php_jsbuffer_create(&buffer, str, str_length,
                                           OwnershipType::NOT_OWNED TSRMLS_CC);
  call_user_function(EG(function_table), stream.PtrPtr(), &method,
                     retval.Ptr(), 1, args TSRMLS_CC);
  zval_dtor(&buffer);
  TRACE("<");
  return str_length;
}

static void node_php_embed_flush(void *server_context) {
  // XXX IMPLEMENT ME
  // Do a JsInvokeAsyncMethod of stream.write, which should add a callback
  // and block until it is handled.
}

static void node_php_embed_register_server_variables(
    zval *track_vars_array TSRMLS_DC) {
  TRACE(">");
  PhpRequestWorker *worker = NODE_PHP_EMBED_G(worker);
  MapperChannel *channel = NODE_PHP_EMBED_G(channel);

  php_import_environment_variables(track_vars_array TSRMLS_CC);
  // Set PHP_SELF to "The filename of the currently executing script,
  // relative to the document root."
  // XXX
  // Put PHP-wrapped version of node context object in $_SERVER['CONTEXT'].
  ZVal context{ZEND_FILE_LINE_C};
  worker->GetContext().ToPhp(channel, context TSRMLS_CC);
  char contextName[] = { "CONTEXT" };
  php_register_variable_ex(contextName, context.Transfer(TSRMLS_C),
                           track_vars_array TSRMLS_CC);
  // XXX Call a JS function passing in $_SERVER to allow init?
  TRACE("<");
}

NAN_METHOD(setIniPath) {
  TRACE(">");
  REQUIRE_ARGUMENT_STRING(0, iniPath);
  if (php_embed_module.php_ini_path_override) {
    delete[] php_embed_module.php_ini_path_override;
  }
  php_embed_module.php_ini_path_override = strdup(*iniPath);
  TRACE("<");
}

NAN_METHOD(request) {
  TRACE(">");
  REQUIRE_ARGUMENTS(4);
  REQUIRE_ARGUMENT_STRING(0, source);
  if (!*source) {
    return Nan::ThrowTypeError("bad string");
  }
  if (!info[1]->IsObject()) {
    return Nan::ThrowTypeError("stream expected");
  }
  v8::Local<v8::Object> stream = info[1].As<v8::Object>();
  v8::Local<v8::Value> context = info[2];
  if (!info[3]->IsFunction()) {
    return Nan::ThrowTypeError("callback expected");
  }
  Nan::Callback *callback = new Nan::Callback(info[3].As<v8::Function>());

  node_php_embed_ensure_init();
  Nan::AsyncQueueWorker(new PhpRequestWorker(callback, stream, context,
                                             *source));
  TRACE("<");
}

/** PHP module housekeeping */
PHP_MINFO_FUNCTION(node_php_embed) {
  php_info_print_table_start();
  php_info_print_table_row(2, "Version", NODE_PHP_EMBED_VERSION);
  php_info_print_table_row(2, "Node version", NODE_VERSION_STRING);
  php_info_print_table_row(2, "PHP version", PHP_VERSION);
  php_info_print_table_end();
}

static void node_php_embed_globals_ctor(
    zend_node_php_embed_globals *node_php_embed_globals TSRMLS_DC) {
  node_php_embed_globals->worker = NULL;
  node_php_embed_globals->channel = NULL;
}
static void node_php_embed_globals_dtor(
    zend_node_php_embed_globals *node_php_embed_globals TSRMLS_DC) {
  // No clean up required.
}

PHP_MINIT_FUNCTION(node_php_embed) {
  TRACE("> PHP_MINIT_FUNCTION");
  PHP_MINIT(node_php_jsobject_class)(INIT_FUNC_ARGS_PASSTHRU);
  PHP_MINIT(node_php_jsbuffer_class)(INIT_FUNC_ARGS_PASSTHRU);
  TRACE("< PHP_MINIT_FUNCTION");
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

/** Node module housekeeping. */
static void ModuleShutdown(void *arg);

#ifdef ZTS
static void ***tsrm_ls;
#endif
static bool node_php_embed_inited = false;
static void node_php_embed_ensure_init(void) {
  if (node_php_embed_inited) {
    return;
  }
  TRACE(">");
  node_php_embed_inited = true;
  char *argv[] = { NULL };
  int argc = 0;
  php_embed_init(argc, argv PTSRMLS_CC);
  // Shutdown the initially-created request; we'll create our own request
  // objects inside PhpRequestWorker.
  php_request_shutdown(NULL);
  node::AtExit(ModuleShutdown, NULL);
  TRACE("<");
}

NAN_MODULE_INIT(ModuleInit) {
  TRACE(">");
  php_embed_module.php_ini_path_override = NULL;
  php_embed_module.php_ini_ignore = true;
  php_embed_module.php_ini_ignore_cwd = true;
  php_embed_module.ini_defaults = NULL;
  php_embed_module.startup = node_php_embed_startup;
  php_embed_module.ub_write = node_php_embed_ub_write;
  php_embed_module.flush = node_php_embed_flush;
  php_embed_module.register_server_variables =
    node_php_embed_register_server_variables;
  // Most of init will be done lazily in node_php_embed_ensure_init()

  // Export functions
  NAN_EXPORT(target, setIniPath);
  NAN_EXPORT(target, request);
  TRACE("<");
}

void ModuleShutdown(void *arg) {
  TRACE(">");
  TSRMLS_FETCH();
  // The php_embed_shutdown expects there to be an open request, so
  // create one just for it to shutdown for us.
  php_request_startup(TSRMLS_C);
  php_embed_shutdown(TSRMLS_C);
  if (php_embed_module.php_ini_path_override) {
    delete[] php_embed_module.php_ini_path_override;
  }
  TRACE("<");
}

NODE_MODULE(node_php_embed, ModuleInit)
