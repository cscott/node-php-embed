// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.

#include <nan.h>
extern "C" {
#include <main/php.h>
#include <Zend/zend.h>
}
#include <cassert>

#include "node_php_phpobject_class.h"

namespace node_php_embed {

v8::Local<v8::Object> node_php_phpobject_create(PhpMessageChannel *channel, objid_t id) {
    Nan::EscapableHandleScope scope;
    // XXX IMPLEMENT ME!
    assert(false);
    return scope.Escape(Nan::New<v8::Object>());
}

}
