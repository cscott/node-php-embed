// A JavaScript (node/v8) object, wrapped for access by PHP code.
// Inspired by v8js_v8object_class in the v8js PHP extension.
#ifndef NODE_PHP_JSOBJECT_CLASS_H
#define NODE_PHP_JSOBJECT_CLASS_H

#include <nan.h>
extern "C" {
#include <main/php.h>
#include <Zend/zend.h>
}

#include "values.h" /* for objid_t */

namespace node_php_embed {

class JsMessageChannel;

struct node_php_jsobject {
    zend_object std;
    JsMessageChannel *channel;
    objid_t id;
};

/* Create a PHP proxy for a JS object.  res should be allocated & inited,
 * and it is owned by the caller. */
void node_php_jsobject_create(zval *res, JsMessageChannel *channel, objid_t id TSRMLS_DC);

}

extern zend_class_entry *php_ce_jsobject;

PHP_MINIT_FUNCTION(node_php_jsobject_class);

#endif
