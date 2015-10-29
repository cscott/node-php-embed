// Main entry point: this is the node module declaration, contains
// the PhpRequestWorker which shuttles messages between node and PHP,
// and contains the SAPI hooks to configure PHP to talk to node.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_embed.h"

#include <string>
#include <unordered_map>

#include "nan.h"

extern "C" {
#include "sapi/embed/php_embed.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "ext/standard/head.h"
#include "ext/standard/info.h"
}

#include "src/macros.h"
#include "src/node_php_jsbuffer_class.h"
#include "src/node_php_jsobject_class.h"
#include "src/node_php_jsserver_class.h"
#include "src/node_php_jswait_class.h"
#include "src/phprequestworker.h"
#include "src/values.h"

using node_php_embed::MapperChannel;
using node_php_embed::OwnershipType;
using node_php_embed::PhpRequestWorker;
using node_php_embed::Value;
using node_php_embed::ZVal;

static void node_php_embed_ensure_init(void);

ZEND_DECLARE_MODULE_GLOBALS(node_php_embed);

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

static char * node_php_embed_read_cookies(TSRMLS_D) {
  // This is a hack to prevent the SAPI from overwriting the
  // cookie data we set up in the PhpRequestWorker constructor.
  return SG(request_info).cookie_data;
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
  if (!info[3]->IsObject()) {
    return Nan::ThrowTypeError("server vars object expected");
  }
  if (!info[4]->IsFunction()) {
    return Nan::ThrowTypeError("init function expected");
  }
  if (!info[5]->IsFunction()) {
    return Nan::ThrowTypeError("callback expected");
  }
  v8::Local<v8::String> source = info[0].As<v8::String>();
  v8::Local<v8::Object> stream = info[1].As<v8::Object>();
  v8::Local<v8::Array> args = info[2].As<v8::Array>();
  v8::Local<v8::Object> server_vars = info[3].As<v8::Object>();
  v8::Local<v8::Value> init_func = info[4];
  Nan::Callback *callback = new Nan::Callback(info[5].As<v8::Function>());

  node_php_embed_ensure_init();
  Nan::AsyncQueueWorker(new PhpRequestWorker(callback, source, stream,
                                             args, server_vars, init_func));
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
  PhpRequestWorker::CheckRequestInfo(TSRMLS_C);
  node::AtExit(ModuleShutdown, nullptr);
  TRACE("<");
}

NAN_MODULE_INIT(ModuleInit) {
  TRACE(">");
  php_embed_module.php_ini_path_override = nullptr;
  php_embed_module.php_ini_ignore = true;
  php_embed_module.php_ini_ignore_cwd = true;
  php_embed_module.ini_defaults = nullptr;
  // The following initialization statements are kept in the same
  // order as the fields in `struct _sapi_module_struct` (SAPI.h)
  php_embed_module.startup = node_php_embed_startup;
  php_embed_module.ub_write = node_php_embed_ub_write;
  php_embed_module.flush = node_php_embed_flush;
  php_embed_module.send_header = node_php_embed_send_header;
  php_embed_module.read_cookies = node_php_embed_read_cookies;
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
