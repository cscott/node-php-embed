// A PHP object, wrapped for access by JavaScript (node/v8).
// Inspired by v8js_object_export in the v8js PHP extension.
#ifndef NODE_PHP_PHPOBJECT_CLASS_H
#define NODE_PHP_PHPOBJECT_CLASS_H

#include <nan.h>
extern "C" {
#include <main/php.h>
#include <Zend/zend.h>
}

#include "values.h" /* for objid_t */

namespace node_php_embed {

class PhpMessageChannel;

struct node_php_phpobject {
    PhpMessageChannel *channel;
    objid_t id;
};

v8::Local<v8::Object> node_php_phpobject_create(PhpMessageChannel *channel, objid_t id);

}

#endif
