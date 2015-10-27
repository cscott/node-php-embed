// Main entry point: this is the node module declaration, contains
// the PhpRequestWorker which shuttles messages between node and PHP,
// and contains the SAPI hooks to configure PHP to talk to node.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_embed.h"

#include "nan.h"

extern "C" {
#include "sapi/embed/php_embed.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "ext/standard/head.h"
#include "ext/standard/info.h"
}

#include "src/asyncmessageworker.h"
#include "src/macros.h"
#include "src/messages.h"
#include "src/node_php_jsbuffer_class.h"
#include "src/node_php_jsobject_class.h"
#include "src/node_php_jsserver_class.h"
#include "src/node_php_jswait_class.h"
#include "src/values.h"

using node_php_embed::MapperChannel;
using node_php_embed::OwnershipType;
using node_php_embed::PhpRequestWorker;
using node_php_embed::Value;
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
  PhpRequestWorker(Nan::Callback *callback, v8::Local<v8::String> source,
                   v8::Local<v8::Object> stream, v8::Local<v8::Array> args,
                   v8::Local<v8::Value> init_func)
      : AsyncMessageWorker(callback), result_(), stream_(), init_func_(),
        argc_(args->Length()), argv_(new char*[args->Length()]) {
    JsStartupMapper mapper(this);
    source_.Set(&mapper, source);
    stream_.Set(&mapper, stream);
    init_func_.Set(&mapper, init_func);
    // Turn the JS array into a char** suitable for PHP.
    for (uint32_t i = 0; i < argc_; i++) {
      Nan::Utf8String s(
        Nan::Get(args, i)
        .FromMaybe(static_cast< v8::Local<v8::Value> >(Nan::EmptyString())));
      argv_[i] = strdup(*s ? *s : "");
    }
  }
  virtual ~PhpRequestWorker() {
    for (uint32_t i = 0; i < argc_; i++) {
      free(argv_[i]);
    }
    delete[] argv_;
  }
  const Value &GetStream() { return stream_; }
  const Value &GetInitFunc() { return init_func_; }

  // Executed inside the PHP thread.  It is not safe to access V8 or
  // V8 data structures here, so everything we need for input and output
  // should go on `this`.
  void Execute(MapperChannel *channel TSRMLS_DC) override {
    TRACE("> PhpRequestWorker");
    SG(request_info).argc = argc_;
    SG(request_info).argv = argv_;
    if (php_request_startup(TSRMLS_C) == FAILURE) {
      Nan::ThrowError("can't create request");
      return;
    }
    NODE_PHP_EMBED_G(worker) = this;
    NODE_PHP_EMBED_G(channel) = channel;
    {
      ZVal source{ZEND_FILE_LINE_C}, result{ZEND_FILE_LINE_C};
      zend_first_try {
        char eval_msg[] = { "request" };  // This shows up in error messages.
        source_.ToPhp(channel, source TSRMLS_CC);
        assert(Z_TYPE_P(*source) == IS_STRING);
        CHECK_ZVAL_STRING(*source);
        zend_eval_stringl_ex(Z_STRVAL_P(*source), Z_STRLEN_P(*source), *result,
                            eval_msg, false TSRMLS_CC);
        if (EG(exception)) {
          // Can't call zend_clear_exception because there isn't a current
          // execution stack (ie, `EG(current_execute_data)`)
          zval *e = EG(exception);
          EG(exception) = nullptr;
          convert_to_string(e);
          SetErrorMessage(Z_STRVAL_P(e));
          zval_ptr_dtor(&e);
        }
        result_.Set(channel, *result TSRMLS_CC);
        result_.TakeOwnership();  // Since this will outlive scope of `result`.
      } zend_catch {
        SetErrorMessage("<bailout>");
      } zend_end_try();
    }  // End of scope releases source and retval.
    // After we return, async tasks will be run to completion and then the
    // queues will be emptied, which may well end up running some additional
    // PHP code in the request context.
    // The remainder of cleanup is done in AfterExecute after that's all done.
    TRACE("< PhpRequestWorker");
  }
  void AfterAsyncLoop(TSRMLS_D) override {
    TRACE("> PhpRequestWorker");
    // Flush the buffers, send the headers.
    // (If we wait until AfterExecute, the queues are already shut down.)
    php_header(TSRMLS_C);
    php_output_flush_all(TSRMLS_C);
    TRACE("< PhpRequestWorker");
  }
  void AfterExecute(TSRMLS_D) override {
    TRACE("> PhpRequestWorker");
    NODE_PHP_EMBED_G(worker) = nullptr;
    NODE_PHP_EMBED_G(channel) = nullptr;
    TRACE("- request shutdown");
    php_request_shutdown(nullptr);
    SG(request_info).argc = 0;
    SG(request_info).argv = nullptr;
    TRACE("< PhpRequestWorker");
  }
  // Executed when the async work is complete.
  // This function will be run inside the main event loop
  // so it is safe to use V8 again.
  void HandleOKCallback(JsObjectMapper *m) override {
    Nan::HandleScope scope;
    v8::Local<v8::Value> argv[] = {
      Nan::Null(),
      // Note that if this returns a wrapped PHP object, it won't be
      // usable for very long!
      result_.ToJs(m)
    };
    callback->Call(2, argv);
  }

 private:
  Value source_;
  Value result_;
  Value stream_;
  Value init_func_;
  uint32_t argc_;
  char **argv_;
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
  if (!worker) { return str_length; /* in module shutdown */ }
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
  if (EG(exception)) {
    NPE_ERROR("- exception caught (ignoring)");
    zend_clear_exception(TSRMLS_C);
  }
  zval_dtor(&buffer);
  TRACE("<");
  return str_length;
}

static void node_php_embed_flush(void *server_context) {
  // Invoke stream.write with a PHP "JsWait" callback, which causes PHP
  // to block until the callback is handled.
  TRACE(">");
  TSRMLS_FETCH();
  // Fetch the MapperChannel for this thread.
  PhpRequestWorker *worker = NODE_PHP_EMBED_G(worker);
  MapperChannel *channel = NODE_PHP_EMBED_G(channel);
  if (!worker) { return; /* we're in module shutdown, no request any more */ }
  ZVal stream{ZEND_FILE_LINE_C}, retval{ZEND_FILE_LINE_C};
  worker->GetStream().ToPhp(channel, stream TSRMLS_CC);
  // Use plain zval to avoid allocating copy of method name.
  zval method; ZVAL_STRINGL(&method, "write", 5, 0);
  // Special buffer type to pass `str` as a node buffer and avoid copying.
  zval buffer; INIT_ZVAL(buffer);
  node_php_embed::node_php_jsbuffer_create(&buffer, "", 0,
                                           OwnershipType::NOT_OWNED TSRMLS_CC);
  // Create the special JsWait object.
  zval wait; INIT_ZVAL(wait);
  node_php_embed::node_php_jswait_create(&wait TSRMLS_CC);
  zval *args[] = { &buffer, &wait };
  call_user_function(EG(function_table), stream.PtrPtr(), &method,
                     retval.Ptr(), 2, args TSRMLS_CC);
  if (EG(exception)) {
    // This exception is often the "ASYNC inside SYNC" TypeError, which
    // is harmless in this context, so don't be noisy about it.
    TRACE("- exception caught (ignoring)");
    zend_clear_exception(TSRMLS_C);
  }
  zval_dtor(&buffer);
  zval_dtor(&wait);
  TRACE("<");
}

static void node_php_embed_send_header(sapi_header_struct *sapi_header,
                                       void *server_context TSRMLS_DC) {
  TRACE(">");
  // Fetch the MapperChannel for this thread.
  PhpRequestWorker *worker = NODE_PHP_EMBED_G(worker);
  MapperChannel *channel = NODE_PHP_EMBED_G(channel);
  if (!worker) { return; /* we're in module shutdown, no headers any more */ }
  ZVal stream{ZEND_FILE_LINE_C}, retval{ZEND_FILE_LINE_C};
  worker->GetStream().ToPhp(channel, stream TSRMLS_CC);
  // Use plain zval to avoid allocating copy of method name.
  // The "sendHeader" method is a special JS-side method to translate
  // headers into node.js format.
  zval method; ZVAL_STRINGL(&method, "sendHeader", 10, 0);
  // Special buffer type to pass `str` as a node buffer and avoid copying.
  zval buffer, *args[] = { &buffer }; INIT_ZVAL(buffer);
  if (sapi_header) {  // NULL is passed to indicate "last call"
    node_php_embed::node_php_jsbuffer_create(
        &buffer, sapi_header->header, sapi_header->header_len,
        OwnershipType::NOT_OWNED TSRMLS_CC);
  }
  call_user_function(EG(function_table), stream.PtrPtr(), &method,
                     retval.Ptr(), 1, args TSRMLS_CC);
  if (EG(exception)) {
    NPE_ERROR("- exception caught (ignoring)");
    zend_clear_exception(TSRMLS_C);
  }
  zval_dtor(&buffer);
  TRACE("<");
}

static void node_php_embed_register_server_variables(
    zval *track_vars_array TSRMLS_DC) {
  TRACE(">");
  PhpRequestWorker *worker = NODE_PHP_EMBED_G(worker);
  MapperChannel *channel = NODE_PHP_EMBED_G(channel);

  // Invoke the init_func in order to set up the $_SERVER variables.
  ZVal init_func{ZEND_FILE_LINE_C};
  ZVal server{ZEND_FILE_LINE_C};
  ZVal wait{ZEND_FILE_LINE_C};
  worker->GetInitFunc().ToPhp(channel, init_func TSRMLS_CC);
  assert(init_func.Type() == IS_OBJECT);
  // Create a wrapper that will allow the JS function to set $_SERVER.
  node_php_embed::node_php_jsserver_create(server.Ptr(), track_vars_array
                                           TSRMLS_CC);
  // Allow the JS function to be asynchronous.
  node_php_embed::node_php_jswait_create(wait.Ptr() TSRMLS_CC);
  // Now invoke the JS function, passing in the wrapper
  zval *r = nullptr;
  zend_call_method_with_2_params(init_func.PtrPtr(),
                                 Z_OBJCE_P(init_func.Ptr()), nullptr,
                                 "__invoke", &r, server.Ptr(), wait.Ptr());
  if (EG(exception)) {
    NPE_ERROR("Exception in server init function");
    zend_clear_exception(TSRMLS_C);
  }
  if (r) { zval_ptr_dtor(&r); }
  TRACE("<");
}

NAN_METHOD(setIniPath) {
  TRACE(">");
  REQUIRE_ARGUMENT_STRING(0, iniPath);
  if (php_embed_module.php_ini_path_override) {
    free(php_embed_module.php_ini_path_override);
  }
  php_embed_module.php_ini_path_override =
    (*iniPath) ? strdup(*iniPath) : nullptr;
  TRACE("<");
}

NAN_METHOD(request) {
  TRACE(">");
  REQUIRE_ARGUMENTS(4);
  REQUIRE_ARGUMENT_STRING_NOCONV(0);
  if (!info[1]->IsObject()) {
    return Nan::ThrowTypeError("stream expected");
  }
  if (!info[2]->IsArray()) {
    return Nan::ThrowTypeError("argument array expected");
  }
  if (!info[3]->IsFunction()) {
    return Nan::ThrowTypeError("init function expected");
  }
  if (!info[4]->IsFunction()) {
    return Nan::ThrowTypeError("callback expected");
  }
  v8::Local<v8::String> source = info[0].As<v8::String>();
  v8::Local<v8::Object> stream = info[1].As<v8::Object>();
  v8::Local<v8::Array> args = info[2].As<v8::Array>();
  v8::Local<v8::Value> init_func = info[3];
  Nan::Callback *callback = new Nan::Callback(info[4].As<v8::Function>());

  node_php_embed_ensure_init();
  Nan::AsyncQueueWorker(new PhpRequestWorker(callback, source, stream,
                                             args, init_func));
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
  node_php_embed_globals->worker = nullptr;
  node_php_embed_globals->channel = nullptr;
}
static void node_php_embed_globals_dtor(
    zend_node_php_embed_globals *node_php_embed_globals TSRMLS_DC) {
  // No clean up required.
}

PHP_MINIT_FUNCTION(node_php_embed) {
  TRACE("> PHP_MINIT_FUNCTION");
  PHP_MINIT(node_php_jsbuffer_class)(INIT_FUNC_ARGS_PASSTHRU);
  PHP_MINIT(node_php_jsobject_class)(INIT_FUNC_ARGS_PASSTHRU);
  PHP_MINIT(node_php_jsserver_class)(INIT_FUNC_ARGS_PASSTHRU);
  PHP_MINIT(node_php_jswait_class)(INIT_FUNC_ARGS_PASSTHRU);
  TRACE("< PHP_MINIT_FUNCTION");
  return SUCCESS;
}

zend_module_entry node_php_embed_module_entry = {
  STANDARD_MODULE_HEADER,
  "node-php-embed", /* extension name */
  nullptr, /* function entries */
  PHP_MINIT(node_php_embed), /* MINIT */
  nullptr, /* MSHUTDOWN */
  nullptr, /* RINIT */
  nullptr, /* RSHUTDOWN */
  PHP_MINFO(node_php_embed), /* MINFO */
  NODE_PHP_EMBED_VERSION,
  ZEND_MODULE_GLOBALS(node_php_embed),
  (void(*)(void* TSRMLS_DC))node_php_embed_globals_ctor,
  (void(*)(void* TSRMLS_DC))node_php_embed_globals_dtor,
  nullptr, /* post deactivate func */
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
  php_embed_init(0, nullptr PTSRMLS_CC);
  // Shutdown the initially-created request; we'll create our own request
  // objects inside PhpRequestWorker.
  php_request_shutdown(nullptr);
  node::AtExit(ModuleShutdown, nullptr);
  TRACE("<");
}

NAN_MODULE_INIT(ModuleInit) {
  TRACE(">");
  php_embed_module.php_ini_path_override = nullptr;
  php_embed_module.php_ini_ignore = true;
  php_embed_module.php_ini_ignore_cwd = true;
  php_embed_module.ini_defaults = nullptr;
  php_embed_module.startup = node_php_embed_startup;
  php_embed_module.send_header = node_php_embed_send_header;
  php_embed_module.ub_write = node_php_embed_ub_write;
  php_embed_module.flush = node_php_embed_flush;
  php_embed_module.register_server_variables =
    node_php_embed_register_server_variables;
  // Most of init will be done lazily in node_php_embed_ensure_init()

  // Initialize object type allowing access to PHP objects from JS
  node_php_embed::PhpObject::Init(target);

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
    free(php_embed_module.php_ini_path_override);
  }
  TRACE("<");
}

NODE_MODULE(node_php_embed, ModuleInit)
