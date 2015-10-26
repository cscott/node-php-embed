// This is an opaque object, which can be created in PHP code, which
// can be passed to JavaScript functions in the slot normally occupied
// by the callback.  It is converted into a callback which blocks the
// PHP thread until it is resolved, and the value passed to the callback
// is substituted for the value returned by the function.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_JSWAIT_CLASS_H_
#define NODE_PHP_EMBED_NODE_PHP_JSWAIT_CLASS_H_

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

namespace node_php_embed {

struct node_php_jswait {
  zend_object std;
  // No special properties.
};

/* Create a PHP version of a JS Buffer. */
void node_php_jswait_create(zval *res TSRMLS_DC);

}  // namespace node_php_embed

extern zend_class_entry *php_ce_jswait;

PHP_MINIT_FUNCTION(node_php_jswait_class);

#endif  // NODE_PHP_EMBED_NODE_PHP_JSWAIT_CLASS_H_
