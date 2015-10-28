// Main entry point: this is the node module declaration, contains
// the PhpRequestWorker which shuttles messages between node and PHP,
// and contains the SAPI hooks to configure PHP to talk to node.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_EMBED_H_
#define NODE_PHP_EMBED_NODE_PHP_EMBED_H_

// This should match the version declared in package.json.
#define NODE_PHP_EMBED_VERSION "0.5.0-git"

namespace node_php_embed {

class PhpRequestWorker;

}

#endif  // NODE_PHP_EMBED_NODE_PHP_EMBED_H_
