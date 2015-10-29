// AsyncMapperChannel is an implementation of MapperChannel used by
// AsyncMessageWorker.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/asyncmapperchannel.h"

#include <cassert>
#include <unordered_map>
#include <vector>

#include "nan.h"

extern "C" {
#include "main/php.h"
}

#include "src/asyncmessageworker.h"
#include "src/values.h"  // for objid_t

namespace node_php_embed {

namespace amw {

// AsyncMapperChannel implementation.

// JsObjectMapper interface -----------------------
// Callable only from the JavaScript side.

// Map Js object to an index.
objid_t AsyncMapperChannel::IdForJsObj(const v8::Local<v8::Object> o) {
  // Have we already mapped this?
  Nan::HandleScope scope;
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  if (jsObjToId->Has(o)) {
    return Nan::To<objid_t>(jsObjToId->Get(o)).FromJust();
  }

  // XXX If o is a Promise, then call PrFunPromise.resolve(o),
  // and set both of them in the jsObjToId map. This would ensure
  // that Promise#nodify is available from PHP.

  objid_t id = NewId();
  jsObjToId->Set(o, Nan::New(id));
  worker_->SaveToPersistent(id, o);
  return id;
}

// Map index to JS object (or create it if necessary).
v8::Local<v8::Object> AsyncMapperChannel::JsObjForId(objid_t id) {
  Nan::EscapableHandleScope scope;
  v8::Local<v8::Value> v = worker_->GetFromPersistent(id);
  if (v->IsObject()) {
    return scope.Escape(Nan::To<v8::Object>(v).ToLocalChecked());
  }
  if (!IsValid()) {
    // This happens when we return an object at the tail of the request.
    return scope.Escape(PhpObject::Create(nullptr, 0));
  }
  // Make a wrapper!
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  v8::Local<v8::Object> o = PhpObject::Create(this, id);
  jsObjToId->Set(o, Nan::New(id));
  worker_->SaveToPersistent(id, o);
  return scope.Escape(o);
}

  // Free JS references associated with an id.
void AsyncMapperChannel::ClearJsId(objid_t id) {
  Nan::HandleScope scope;
  v8::Local<v8::Value> v = worker_->GetFromPersistent(id);
  if (!v->IsObject()) { return; }
  v8::Local<v8::Object> o = Nan::To<v8::Object>(v).ToLocalChecked();
  // There might be other live references to this object; set its
  // id to 0 to neuter it.
  PhpObject::MaybeNeuter(this, o);
  // Remove it from our maps (and release our persistent reference).
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  jsObjToId->Delete(o);
  worker_->DeleteFromPersistent(id);
}

objid_t AsyncMapperChannel::ClearAllJsIds() {
  uv_mutex_lock(&id_lock_);
  objid_t last = next_id_;
  next_id_ = 0;  // Don't allocate any more ids.
  uv_mutex_unlock(&id_lock_);

  for (objid_t id = 1; id < last; id++) {
    ClearJsId(id);
  }
  return last;
}

// PhpObjectMapper interface -----------------------
// Callable only from the PHP side.

// Map PHP object to an index.
objid_t AsyncMapperChannel::IdForPhpObj(zval *z) {
  assert(Z_TYPE_P(z) == IS_OBJECT);
  zend_object_handle handle = Z_OBJ_HANDLE_P(z);
  if (php_obj_to_id_.count(handle)) {
    return php_obj_to_id_.at(handle);
  }

  objid_t id = NewId();
  if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
  // XXX Should we clone/separate z?
  Z_ADDREF_P(z);
  php_obj_list_[id] = z;
  php_obj_to_id_[handle] = id;
  return id;
}

// Returned value is owned by the object mapper, caller should not release it.
zval *AsyncMapperChannel::PhpObjForId(objid_t id TSRMLS_DC) {
  if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
  ZVal z(php_obj_list_[id] ZEND_FILE_LINE_CC);
  if (z.IsNull()) {
    node_php_jsobject_create(z.Ptr(), this, id TSRMLS_CC);
    php_obj_list_[id] = z.Ptr();
    php_obj_to_id_[Z_OBJ_HANDLE_P(z.Ptr())] = id;
    // One excess reference, owned by objectmapper.
    return z.Escape();
  }
  // Don't increment reference.
  return z.Ptr();
}

// Free PHP references associated with an id.
void AsyncMapperChannel::ClearPhpId(objid_t id TSRMLS_DC) {
  zval *z = (id < php_obj_list_.size()) ? php_obj_list_[id] : nullptr;
  if (z) {
    node_php_jsobject_maybe_neuter(z TSRMLS_CC);
    php_obj_list_[id] = nullptr;
    php_obj_to_id_.erase(Z_OBJ_HANDLE_P(z));
    zval_ptr_dtor(&z);
  }
}

// ObjectMapper interface -----------------------
// Callable from both threads.

objid_t AsyncMapperChannel::NewId() {
  uv_mutex_lock(&id_lock_);
  // next_id_ is 0 if we're shutting down.
  objid_t id = (next_id_ == 0) ? 0 : (next_id_++);
  uv_mutex_unlock(&id_lock_);
  return id;
}

bool AsyncMapperChannel::IsValid() {
  uv_mutex_lock(&id_lock_);
  bool valid = (next_id_ != 0);
  uv_mutex_unlock(&id_lock_);
  return valid;
}

// JsMessageChannel interface -----------------------
// Callable only from the PHP side.
void AsyncMapperChannel::SendToJs(Message *m, MessageFlags flags
                                       TSRMLS_DC) const {
  worker_->SendToJs(m, flags TSRMLS_CC);
}
// PhpMessageChannel interface -----------------------
// Callable only from the JS side.
void AsyncMapperChannel::SendToPhp(Message *m, MessageFlags flags) const {
  worker_->SendToPhp(m, flags);
}

}  // namespace amw

}  // namespace node_php_embed
