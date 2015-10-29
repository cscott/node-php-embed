// PhpRequestWorker is a subclass of AsyncMessageWorker which contains
// all the PHP-specific code to initialize and tear down request contexts.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/phprequestworker.h"

#include "nan.h"

extern "C" {
#include "main/php.h"
#include "main/php_main.h"
#include "main/SAPI.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "ext/standard/head.h"
#include "ext/standard/info.h"
}

#include "src/asyncmessageworker.h"
#include "src/macros.h"
#include "src/node_php_embed.h"  // for NODE_PHP_EMBED_G

namespace node_php_embed {

PhpRequestWorker::PhpRequestWorker(Nan::Callback *callback,
                                   v8::Local<v8::String> source,
                                   v8::Local<v8::Object> stream,
                                   v8::Local<v8::Array> args,
                                   v8::Local<v8::Object> server_vars,
                                   v8::Local<v8::Value> init_func)
    : AsyncMessageWorker(callback), result_(), stream_(), init_func_(),
      argc_(args->Length()), argv_(new char*[args->Length()]),
      server_vars_() {
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
  // Turn the server_vars object into a c++ string->string map.
  v8::Local<v8::Array> names =  Nan::GetPropertyNames(server_vars)
    .FromMaybe(Nan::New<v8::Array>(0));
  for (uint32_t i = 0; i < names->Length(); i++) {
    v8::Local<v8::Value> key = Nan::Get(names, i).ToLocalChecked();
    v8::Local<v8::Value> value = Nan::Get(server_vars, key)
      .FromMaybe(static_cast<v8::Local<v8::Value> >(Nan::Undefined()));
    if (!value->IsString()) { continue; }
    Nan::Utf8String k(key), v(value);
    if (!(*k && *v)) { continue; }
    server_vars_.emplace(*k, *v);
  }
}

PhpRequestWorker::~PhpRequestWorker() {
  for (uint32_t i = 0; i < argc_; i++) {
    free(argv_[i]);
  }
  delete[] argv_;
}

// Executed inside the PHP thread.  It is not safe to access V8 or
// V8 data structures here, so everything we need for input and output
// should go on `this`.
void PhpRequestWorker::Execute(MapperChannel *channel TSRMLS_DC) {
  TRACE("> PhpRequestWorker");
  // Certain fields in request_info need to be set up before
  // php_request_startup is invoked.
  SG(request_info).argc = argc_;
  SG(request_info).argv = argv_;
#define SET_REQUEST_INFO(envvar, requestvar)                            \
  if (server_vars_.count(envvar)) {                                     \
    SG(request_info).requestvar = estrdup((server_vars_[envvar]).c_str()); \
  } else {                                                              \
    SG(request_info).requestvar = nullptr;                              \
  }
#define FREE_REQUEST_INFO(requestvar) /* for later */           \
  if (SG(request_info).requestvar) {                            \
    efree(const_cast<char*>(SG(request_info).requestvar));      \
    SG(request_info).requestvar = nullptr;                      \
  }
#define CHECK_REQUEST_INFO(requestvar)                                  \
  if (SG(request_info).requestvar) {                                    \
    NPE_ERROR("OOPS! " #requestvar " is set!");                         \
    SG(request_info).requestvar = nullptr;                              \
  }
  if (argc_ == 0) {
    SG(sapi_headers).http_response_code = 200;
  }
  SET_REQUEST_INFO("REQUEST_METHOD", request_method);
  SET_REQUEST_INFO("QUERY_STRING", query_string);
  SET_REQUEST_INFO("PATH_TRANSLATED", path_translated);
  SET_REQUEST_INFO("REQUEST_URI", request_uri);
  SET_REQUEST_INFO("HTTP_COOKIE", cookie_data);
  SET_REQUEST_INFO("HTTP_CONTENT_TYPE", content_type);
  SG(request_info).content_length =
    server_vars_.count("HTTP_CONTENT_LENGTH") ?
    atol(server_vars_["HTTP_CONTENT_LENGTH"].c_str()) : 0;
  // Unlike the other settings, proto_num needs to be set *after* we
  // activate the new request.  Go figure.
  int proto_num = 1000;
  if (server_vars_.count("SERVER_PROTOCOL")) {
    const char *sline = server_vars_["SERVER_PROTOCOL"].c_str();
    if (strlen(sline) > 7 && strncmp(sline, "HTTP/1.", 7) == 0) {
      proto_num = 1000 + (sline[7] - '0');
    }
  }
  server_vars_.clear();  // We don't need to keep these around any more.
  // SG(server_context) needs to be non-zero.  Believe it or not,
  // this is what the php-cgi binary does:
  SG(server_context) = reinterpret_cast<void*>(1);  // Sigh.
  // The read_post callback gets executed by php_request_startup, and it
  // will need access to the worker and channel, so set them up now.
  NODE_PHP_EMBED_G(worker) = this;
  NODE_PHP_EMBED_G(channel) = channel;
  // Ok, *now* we can startup the request.
  if (php_request_startup(TSRMLS_C) == FAILURE) {
    Nan::ThrowError("can't create request");
    return;
  }
  SG(request_info).proto_num = proto_num;
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

void PhpRequestWorker::AfterAsyncLoop(TSRMLS_D) {
  TRACE("> PhpRequestWorker");
  // Flush the buffers, send the headers.
  // (If we wait until AfterExecute, the queues are already shut down.)
  php_header(TSRMLS_C);
  php_output_flush_all(TSRMLS_C);
  TRACE("< PhpRequestWorker");
}

void PhpRequestWorker::AfterExecute(TSRMLS_D) {
  TRACE("> PhpRequestWorker");
  NODE_PHP_EMBED_G(worker) = nullptr;
  NODE_PHP_EMBED_G(channel) = nullptr;
  TRACE("- request shutdown");
  SG(request_info).argc = 0;
  SG(request_info).argv = nullptr;
  FREE_REQUEST_INFO(request_method);
  FREE_REQUEST_INFO(query_string);
  FREE_REQUEST_INFO(path_translated);
  FREE_REQUEST_INFO(request_uri);
  FREE_REQUEST_INFO(cookie_data);
  FREE_REQUEST_INFO(content_type);
  php_request_shutdown(nullptr);
  TRACE("< PhpRequestWorker");
}
void PhpRequestWorker::CheckRequestInfo(TSRMLS_D) {
  CHECK_REQUEST_INFO(request_method);
  CHECK_REQUEST_INFO(query_string);
  CHECK_REQUEST_INFO(path_translated);
  CHECK_REQUEST_INFO(request_uri);
  CHECK_REQUEST_INFO(cookie_data);
  CHECK_REQUEST_INFO(content_type);
}

// Executed when the async work is complete.
// This function will be run inside the main event loop
// so it is safe to use V8 again.
void PhpRequestWorker::HandleOKCallback(JsObjectMapper *m) {
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {
    Nan::Null(),
    // Note that if this returns a wrapped PHP object, it won't be
    // usable for very long!
    result_.ToJs(m)
  };
  callback->Call(2, argv);
}

}  // namespace node_php_embed
