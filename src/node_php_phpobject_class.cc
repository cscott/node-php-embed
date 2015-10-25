// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/node_php_phpobject_class.h"

#include <cassert>

#include "nan.h"

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

#include "src/macros.h"

namespace node_php_embed {

v8::Local<v8::Object> PhpObject::Create(MapperChannel *channel,
                                        objid_t id) {
  Nan::EscapableHandleScope scope;
  PhpObject *obj = new PhpObject(channel, id);
  v8::Local<v8::Value> argv[] = { Nan::New<v8::External>(obj) };
  return scope.Escape(constructor()->NewInstance(1, argv));
}

void PhpObject::MaybeNeuter(MapperChannel *channel, v8::Local<v8::Object> obj) {
  v8::Local<v8::FunctionTemplate> t = Nan::New(cons_template());
  if (!t->HasInstance(obj)) {
    // Nope, not a PhpObject.
    return;
  }
  PhpObject *p = Unwrap<PhpObject>(obj);
  if (p->channel_ != channel) {
    // Already neutered, or else not from this request.
    return;
  }
  p->channel_ = NULL;
  p->id_ = 0;
}

NAN_MODULE_INIT(PhpObject::Init) {
  v8::Local<v8::String> class_name = NEW_STR("PhpObject");
  v8::Local<v8::FunctionTemplate> tpl =
    Nan::New<v8::FunctionTemplate>(PhpObject::New);
  tpl->SetClassName(class_name);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  cons_template().Reset(tpl);
  Nan::Set(target, class_name, constructor());
}

PhpObject::~PhpObject() {
  // XXX remove from mapping table, notify PHP.
  TRACE("JS deallocate");
}

NAN_METHOD(PhpObject::New) {
  if (!info.IsConstructCall()) {
    return Nan::ThrowTypeError("You must use `new` with this constructor.");
  }
  if (!info[0]->IsExternal()) {
    return Nan::ThrowTypeError("This constructor is for internal use only.");
  }
  // This object was made by PhpObject::Create()
  PhpObject *obj = reinterpret_cast<PhpObject*>
    (v8::Local<v8::External>::Cast(info[0])->Value());
  obj->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
}

}  // namespace node_php_embed
