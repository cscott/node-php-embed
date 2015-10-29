// This is a wrapper for PHP closures which signals that a given PHP closure
// should be considered a reference to the PHP event loop, so that PHP
// execution is not considered complete until the closure is executed once.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_JSASYNC_CLASS_H_
#define NODE_PHP_EMBED_NODE_PHP_JSASYNC_CLASS_H_

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

namespace node_php_embed {

class MessageToJs;

struct node_php_jsasync {
  zend_object std;
  // No special properties.
};

/* Create a PHP version of a JS Buffer. */
void node_php_jsasync_create(zval *res TSRMLS_DC);

}  // namespace node_php_embed

extern zend_class_entry *php_ce_jsasync;

PHP_MINIT_FUNCTION(node_php_jsasync_class);

#endif  // NODE_PHP_EMBED_NODE_PHP_JSASYNC_CLASS_H_
