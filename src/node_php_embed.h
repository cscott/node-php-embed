// Main entry point: this is the node module declaration, contains
// the PhpRequestWorker which shuttles messages between node and PHP,
// and contains the SAPI hooks to configure PHP to talk to node.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_EMBED_H_
#define NODE_PHP_EMBED_NODE_PHP_EMBED_H_

// This should match the version declared in package.json.
#define NODE_PHP_EMBED_VERSION "0.5.1-git"

extern "C" {
#include "TSRM/TSRM.h"
#include "Zend/zend_API.h"
}

namespace node_php_embed {
class PhpRequestWorker;
class MapperChannel;
}

/* Per-thread storage for the module */
ZEND_BEGIN_MODULE_GLOBALS(node_php_embed)
  node_php_embed::PhpRequestWorker *worker;
  node_php_embed::MapperChannel *channel;
ZEND_END_MODULE_GLOBALS(node_php_embed)

ZEND_EXTERN_MODULE_GLOBALS(node_php_embed);

#ifdef ZTS
# define NODE_PHP_EMBED_G(v)                    \
  TSRMG(node_php_embed_globals_id, zend_node_php_embed_globals *, v)
#else
# define NODE_PHP_EMBED_G(v)                    \
  (node_php_embed_globals.v)
#endif

#endif  // NODE_PHP_EMBED_NODE_PHP_EMBED_H_
