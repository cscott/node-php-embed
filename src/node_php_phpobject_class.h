// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_PHPOBJECT_CLASS_H_
#define NODE_PHP_EMBED_NODE_PHP_PHPOBJECT_CLASS_H_

#include "nan.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

#include "src/values.h" /* for objid_t */

namespace node_php_embed {

class MapperChannel;

class PhpObject : public Nan::ObjectWrap {
 public:
  // Register this class with Node.
  static NAN_MODULE_INIT(Init);
  // Create a new V8 wrapper corresponding to a particular PHP object id.
  static v8::Local<v8::Object> Create(MapperChannel *channel, objid_t id);
  // If the given object is an instance of PhpObject from this channel,
  // set the id field to 0 to indicate an invalid reference to a closed
  // PHP context.
  static void MaybeNeuter(MapperChannel *channel, v8::Local<v8::Object> obj);

 private:
  explicit PhpObject(MapperChannel *channel, objid_t id)
    : channel_(channel), id_(id) { }
  ~PhpObject() override;

  static NAN_METHOD(New);

  // Stash away the constructor's template for later use.
  static inline Nan::Persistent<v8::FunctionTemplate> & cons_template() {
    static Nan::Persistent<v8::FunctionTemplate> my_template;
    return my_template;
  }
  static inline v8::Local<v8::Function> constructor() {
    Nan::EscapableHandleScope scope;
    v8::Local<v8::FunctionTemplate> t = Nan::New(cons_template());
    return scope.Escape(Nan::GetFunction(t).ToLocalChecked());
  }

  // Members
  MapperChannel *channel_;
  objid_t id_;
};

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_NODE_PHP_PHPOBJECT_CLASS_H_
