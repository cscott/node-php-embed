#!/usr/bin/env node
'use strict';
// Very simple CLI wrapper, to allow php-embed to be used as a replacement
// for the `php` binary.
var php = require('../');

// XXX here we might eventually do some preprocessing of the command line
// like the PHP CLI does.
// For now assume the first argument is the script to run.

var args = process.argv.slice(0);
if (args.length < 3) {
  console.error('PHP filename needed.');
  process.exit(1);
}
args.shift(); // Remove the path to the node interpreter
args.shift(); // Remove the path to this script
var phpfile = args[0];

// XXX an `exit(2)` in the PHP script will cause the request to bailout
// and throw a JS exception, which will then cause a non-zero exit
// code from node as well -- but with a lot of extra noise on the
// console.  We should figure out how to catch this case in PHP,
// wrap the exit code appropriately, then catch the exception here
// and unwrap the exit code.
php.request({
  file: phpfile,
  args: args,
}).done();
