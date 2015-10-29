// AsyncMapperChannel is an implementation of MapperChannel used by
// AsyncMessageWorker.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_ASYNCMAPPERCHANNEL_H_
#define NODE_PHP_EMBED_ASYNCMAPPERCHANNEL_H_

#include <unordered_map>
#include <vector>

#include "nan.h"

extern "C" {
#include "main/php.h"
}

#include "src/messages.h"  // for MapperChannel
#include "src/values.h"  // for objid_t

namespace node_php_embed {

class AsyncMessageWorker;
class JsCleanupSyncMsg;

namespace amw {

/* This is the interface exposed to the object proxy classes. */
class AsyncMapperChannel : public MapperChannel {
  friend class node_php_embed::AsyncMessageWorker;
  friend class node_php_embed::JsCleanupSyncMsg;
 public:
  virtual ~AsyncMapperChannel() {
    // zvals should have been freed beforehand from php_obj_list_ because
    // PHP context is shut down so we can't do that now.
    js_obj_to_id_.Reset();
    uv_mutex_destroy(&id_lock_);
  }
  // JsObjectMapper interface
  objid_t IdForJsObj(const v8::Local<v8::Object> o) override;
  v8::Local<v8::Object> JsObjForId(objid_t id) override;
  // PhpObjectMapper interface
  objid_t IdForPhpObj(zval *o) override;
  zval *PhpObjForId(objid_t id TSRMLS_DC) override;
  // ObjectMapper interfaces
  bool IsValid() override;
  // JsMessageChannel interface
  void SendToJs(Message *m, MessageFlags flags TSRMLS_DC) const override;
  // PhpMessageChannel interface
  void SendToPhp(Message *m, MessageFlags flags) const override;

 private:
  // Callable from both threads:
  objid_t NewId();
  // Callable from JS thread:
  void ClearJsId(objid_t id);
  objid_t ClearAllJsIds();
  // Callable from PHP thread:
  void ClearPhpId(objid_t id TSRMLS_DC);
  // Constructor, invoked from JS thread:
  explicit AsyncMapperChannel(AsyncMessageWorker *worker)
      : worker_(worker), js_obj_to_id_(), php_obj_to_id_(), php_obj_list_(),
        // Id #0 is reserved for "invalid object".
        next_id_(1) {
    uv_mutex_init(&id_lock_);
    js_obj_to_id_.Reset(v8::NativeWeakMap::New(v8::Isolate::GetCurrent()));
  }
  NAN_DISALLOW_ASSIGN_COPY_MOVE(AsyncMapperChannel);
  AsyncMessageWorker* worker_;

  // Js Object mapping (along with worker's GetFromPersistent/etc)
  // Read/writable only from Js thread.
  Nan::Persistent<v8::NativeWeakMap> js_obj_to_id_;

  // PHP Object mapping
  // Read/writable only from PHP thread.
  std::unordered_map<zend_object_handle, objid_t> php_obj_to_id_;
  std::vector<zval*> php_obj_list_;

  // Ids are allocated from both threads, so mutex is required.
  uv_mutex_t id_lock_;
  objid_t next_id_;
};

}  // namespace amw

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_ASYNCMAPPERCHANNEL_H_
