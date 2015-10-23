// This is a PHP class which wraps a node Buffer object.
// This helps paper over a difference between PHP and JS: PHP strings
// are bytestrings (like node Buffers); JavaScript strings are unicode
// strings (nominally encoded in UTF-8 when converted to bytestrings).
// In order to avoid incorrect decoding/encoding, we sometimes must
// pass strings from PHP to Node as Buffer objects, not String objects.
// This class provides a PHP-accessible wrapper to allow doing so.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_JSBUFFER_CLASS_H_
#define NODE_PHP_EMBED_NODE_PHP_JSBUFFER_CLASS_H_

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

namespace node_php_embed {

// The contents of NOT_OWNED objects should not be freed.
// The contents of PHP_OWNED objects should be freed with efree().
// The contents of CPP_OWNED objects should be freed with delete;
enum OwnershipType { NOT_OWNED, PHP_OWNED, CPP_OWNED };

struct node_php_jsbuffer {
  zend_object std;
  const char *data;
  ulong length;
  OwnershipType owner;
  zval *z; /* can hold a reference to a PHP string */
};

/* Create a PHP version of a JS Buffer. */
void node_php_jsbuffer_create(zval *res, const char *data, ulong length,
                              OwnershipType owner TSRMLS_DC);

}  // namespace node_php_embed

extern zend_class_entry *php_ce_jsbuffer;

PHP_MINIT_FUNCTION(node_php_jsbuffer_class);

#endif  // NODE_PHP_EMBED_NODE_PHP_JSBUFFER_CLASS_H_
