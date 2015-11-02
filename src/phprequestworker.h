// PhpRequestWorker is a subclass of AsyncMessageWorker which contains
// all the PHP-specific code to initialize and tear down request contexts.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_PHPREQUESTWORKER_H_
#define NODE_PHP_EMBED_PHPREQUESTWORKER_H_

#include <string>
#include <unordered_map>

#include "nan.h"

extern "C" {
#include "main/php.h"
}

#include "src/asyncmessageworker.h"
#include "src/values.h"

namespace node_php_embed {

class PhpRequestWorker : public AsyncMessageWorker {
 public:
  PhpRequestWorker(Nan::Callback *callback, v8::Local<v8::String> source,
                   v8::Local<v8::Object> stream, v8::Local<v8::Array> args,
                   v8::Local<v8::Object> server_vars,
                   v8::Local<v8::Value> init_func,
                   const char *startup_file);
  virtual ~PhpRequestWorker();
  const inline Value &GetStream() { return stream_; }
  const inline Value &GetInitFunc() { return init_func_; }

  // Executed inside the PHP thread.  It is not safe to access V8 or
  // V8 data structures here, so everything we need for input and output
  // should go on `this`.
  void Execute(MapperChannel *channel TSRMLS_DC) override;
  void AfterAsyncLoop(TSRMLS_D) override;
  void AfterExecute(TSRMLS_D) override;

  // Executed in the JS thread.
  void HandleOKCallback(JsObjectMapper *m) override;

  // Used during module startup to check SG(request_info)
  static void CheckRequestInfo(TSRMLS_D);

 private:
  Value source_;
  Value result_;
  Value stream_;
  Value init_func_;
  uint32_t argc_;
  char **argv_;
  std::unordered_map<std::string, std::string> server_vars_;
  const char *startup_file_;
};

}  // namespace node_php_embed

#endif  //  NODE_PHP_EMBED_PHPREQUESTWORKER_H_
