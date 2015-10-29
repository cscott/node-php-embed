# php-embed
[![NPM][NPM1]][NPM2]

[![Build Status][1]][2] [![dependency status][3]][4] [![dev dependency status][5]][6]

The node `php-embed` package binds to PHP's "embed SAPI" in order to
provide bidirectional interoperability between PHP and JavaScript code
in a single process.

Node/iojs >= 2.4.0 is currently required, since we use `NativeWeakMap`s
in the implementation.  This could probably be worked around using
v8 hidden properties, but it doesn't seem worth it right now.

# Usage

## Basic

```js
var path = require('path');
var php = require('php-embed');
php.request({
  file: path.join(__dirname, 'hello.php'),
  stream: process.stdout
}).then(function(v) {
  console.log('php is done and stream flushed.');
});
```

## Advanced

```js
var php = require('php-embed');
php.request({
  source: ['call_user_func(function() {',
           '  class Foo {',
           '    var $bar = "bar";',
           '  }',
           ' $c = $_SERVER["CONTEXT"];',
           ' // Invoke an Async JS method',
           ' $result = $c->jsfunc(new Foo, $c->jsvalue, new Js\\Wait);',
           '  // And return the value back to JS.',
          '  return $result;',
          '})'].join('\n'),
  context: {
    jsvalue: 42, // Pass JS values to PHP
    jsfunc: function(foo, value, cb) {
      // Access PHP object from JS
      console.log(foo.bar, value); // Prints "bar 42"
      // Asynchronous completion, doesn't block node event loop
      setTimeout(function() { cb(null, "done") }, 500);
    }
  }
}).then(function(v) {
  console.log(v); // Prints "done" ($result from PHP)
}).done();
```

## Running command-line PHP scripts

The `php-embed` package contains a binary which can be used as a
drop-in replacement for the `php` CLI binary:

```sh
npm install -g php-embed
php-embed some-file.php argument1 argument2....
```

Not every feature of the PHP CLI binary has been implemented; this
is currently mostly a convenient testing tool.

# API

## php.request(options, [callback])
Triggers a PHP "request", and returns a [`Promise`] which will be
resolved when the request completes.  If you prefer to use callbacks,
you can ignore the return value and pass a callback as the second
parameter.
*   `options`: an object containing various parameters for the request.
    Either `source` or `file` is mandatory; the rest are optional.
    - `source`:
        Specifies a source string to evaluate *as an expression* in
        the request context.  (If you want to evaluate a statement,
        you can wrap it in [`call_user_func`]`(function () { ... })`.)
    - `file`:
        Specifies a PHP file to evaluate in the request context.
    - `stream`:
        A node [`stream.Writable`] to accept output from the PHP
        request.  If not specified, defaults to `process.stdout`.
    - `request`:
        If an [`http.IncomingMessage`] is provided here, the PHP
        server variables will be set up with information about
        the request.
    - `args`:
        If an array with at least one element is provided, the
        PHP `$argc` and `$argv` variables will be set up as
        PHP CLI programs expect.  Note that `args[0]` should
        be the "script file name", as in C convention.
    - `context`:
        A JavaScript object which will be made available to the PHP
        request in `$_SERVER['CONTEXT']`.
    - `serverInitFunc`:
        The user can provide a JavaScript function which will
        be passed an object containing values for the PHP
        [`$_SERVER`] variable, such as `REQUEST_URI`, `SERVER_ADMIN`, etc.
        You can add or override values in this function as needed
        to set up your request.
*   `callback` *(optional)*: A standard node callback.  The first argument
    is non-null iff an exception was raised. The second argument is the
    result of the PHP evaluation, converted to a string.

# PHP API

From the PHP side, there are three new classes defined, all in the
`Js` namespace, and one new property defined in the [`$_SERVER`]
superglobal.

## `$_SERVER['CONTEXT']`
This is the primary mechanism for passing data from the node
process to the PHP request.  You can pass over a reference to
a JavaScript object, and populate it with whatever functions
or data you wish to make available to the PHP code.

## class `Js\Object`
This is the class which wraps JavaScript objects visible to PHP code.
You can't create new objects of this type except by invoking
JavaScript functions/methods/constructors.

## class `Js\Buffer`
This class wraps a PHP string to indicate that it should be passed to
JavaScript as a node `Buffer` object, instead of decoded to UTF-8 and
converted to a JavaScript String.  Assuming that a node-style
Writable stream is made available to PHP as `$stream`, compare:

```php
# The PHP string "abc" is decoded as UTF8 to form a JavaScript string,
# which is then re-encoded as UTF8 and written to the stream:
$stream.write("abc", "utf8");
# The PHP string "abc" is treated as a byte-stream and not de/encoded.
$stream.write(new Js\Buffer("abc"));
# Write to the stream synchronously (see description of next class)
$stream.write(new Js\Buffer("abc"), new Js\Wait());
```

## class `Js\Wait`
This class allows you to invoke asynchronous JavaScript functions from
PHP code as if they were synchronous.  You create a new instance of
`Js\Wait` and pass that to the function where it would expect a
standard node-style callback.  For example, if the JavaScript
`setTimeout` function were made available to PHP as `$setTimeout`, then:
```php
$setTimeout(new Js\Wait, 5000);
```
would halt the PHP thread for 5 seconds.  More usefully, if you were
to make the node [`fs`] module available to PHP as `$fs`, then:
```php
$contents = $fs.readFile('path/to/file', 'utf8', new Js\Wait);
```
would invoke the [`fs.readFile`] method asynchronously in the node context,
but block the PHP thread until its callback was invoked.  The result
returned in the callback would then be used as the return value for
the function invocation, resulting in `$contents` getting the result
of reading the file.

Note that calls using `Js\Wait` block the PHP thread but do not
block the node thread.

# Javascript API

The JavaScript `in` operator, when applied to a wrapped PHP object,
works the same as the PHP [`isset()`] function.  Similarly, when applied
to a wrapped PHP object, JavaScript `delete` works like PHP [`unset()`].

```js
var php = require('php-embed');
php.request({
  source: 'call_user_func(function() {' +
          '  class Foo { var $bar = null; var $bat = 42; } ' +
          '  $_SERVER["CONTEXT"](new Foo()); ' +
          '})',
  context: function(foo) {
    console.log("bar" in foo ? "yes" : "no"); // This prints "no"
    console.log("bat" in foo ? "yes" : "no"); // This prints "yes"
  }
}).done();
```

PHP has separate namespaces for properties and methods, while JavaScript
has just one.  Usually this isn't an issue, but if you need to you can use
a leading `$` to specify a property, or `__call` to specifically invoke a
method.

```js
var php = require('php-embed');
php.request({
  source: ['call_user_func(function() {',
           '  class Foo {',
           '    var $bar = "bar";',
           '    function bar($what) { echo "I am a ", $what, "!\n"; }',
           '  }',
           '  $foo = new Foo;',
           '  // This prints "bar"',
           '  echo $foo->bar, "\n";',
           '  // This prints "I am a function!"',
           '  $foo->bar("function");',
           '  // Now try it in JavaScript',
          '  $_SERVER["CONTEXT"]($foo);',
          '})'].join('\n'),
  context: function(foo) {
    // This prints "bar"
    console.log(foo.$bar);
    // This prints "I am a function"
    foo.__call("bar", "function");
  }
}).done();
```

At the moment, all property accesses and method invocations from
JavaScript to PHP are done synchronously; that is, they block the
JavaScript event loop.  The mechanisms are in place for asynchronous
access; I just haven't quite figured out what the syntax for that
should look like.

# Installing

You can use [`npm`](https://github.com/isaacs/npm) to download and install:

* The latest `php-embed` package: `npm install php-embed`

* GitHub's `master` branch: `npm install https://github.com/cscott/node-php-embed/tarball/master`

In both cases the module is automatically built with npm's internal
version of `node-gyp`, and thus your system must meet
[node-gyp's requirements](https://github.com/TooTallNate/node-gyp#installation).

The prebuilt binaries are built using g++-5 on Linux, and so you will
need to have the appropriate versions of the C++ standard library
available.  Something like `apt-get install g++-5` should suffice on
Debian/Ubuntu.

It is also possible to make your own build of `php-embed` from its
source instead of its npm package ([see below](#building-from-the-source)).

# Building from source

Unless building via `npm install` you will need `node-pre-gyp`
installed globally:

    npm install -g node-pre-gyp

The `php-embed` module depends on the PHP embedding API.
However, by default, an internal/bundled copy of `libphp5` will be built and
statically linked, so an externally installed `libphp5` is not required.

If you wish to install against an external `libphp5` then you need to
pass the `--libphp5` argument to `node-pre-gyp` or `npm install`.

     node-pre-gyp --libphp5=external rebuild

Or, using `npm`:

     npm install --libphp5=external

If building against an external `libphp5` make sure to have the
development headers available.  If you don't have them installed,
install the `-dev` package with your package manager, e.g.
`apt-get install libphp5-embed php5-dev` for Debian/Ubuntu.
Your external `libphp5` should have been built with thread-safety
enabled (`ZTS` turned on).

You will also need a C++11 compiler.  We perform builds using
clang-3.5 and g++-5; both of these are known to work.  (Use
`apt-get install g++-5` to install g++-5 if `g++ --version`
reveals that you have an older version of `g++`.)  To ensure
that `npm`/`node-pre-gyp` use your preferred compiler, you may
need to do something like:

```sh
export CXX="g++-5"
export CC="gcc-5"
```
On Mac OSX, you need to limit support to OS X 10.7 and above in order
to get C++11 support.  Something like the following should work:

```sh
export MACOSX_DEPLOYMENT_TARGET=10.7 ;
```

Developers hacking on the code will probably want to use:

    node-pre-gyp --debug build

Passing the `--debug` flag to `node-pre-gyp` enables memory checking, and
the `build` command (instead of `rebuild`) avoids rebuilding `libphp5`
from scratch after every change.  (You can also use `npm run
debug-build` if you find that easier to remember.)

# Testing

To run the test suite, use:

    npm test

This will run the JavaScript and C++ linters, as well as a test suite
using [mocha](https://github.com/visionmedia/mocha).

During development, `npm run jscs-fix` will automatically correct most
JavaScript code style issues, and `npm run valgrind` will detect a
large number of potential memory issues.  Note that node itself will
leak a small amount of memory from `node::CreateEnvironment`,
`node::cares_wrap::Initialize`, and `node::Start`; these can safely be
ignored in the `valgrind` report.

# Contributors

* [C. Scott Ananian](https://github.com/cscott)

# Related projects

* [`mediawiki-express`](https://github.com/cscott/node-mediawiki-express)
  is an npm package which uses `php-embed` to run mediawiki inside a
  node.js [`express`](http://expressjs.com) server.
* [`v8js`](https://github.com/preillyme/v8js) is a "mirror image"
  project: it embeds the v8 JavaScript engine inside of PHP, whereas
  `php-embed` embeds PHP inside node/v8.  The author of `php-embed`
  is a contributor to `v8js` and they share bits of code.  The
  JavaScript API to access PHP objects is deliberately similar
  to that used by `v8js`.
* [`dnode-php`](https://github.com/bergie/dnode-php) is an
  RPC protocol implementation for Node and PHP, allowing calls
  between Node and PHP code running on separate servers.  See
  also [`require-php`](https://www.npmjs.com/package/require-php),
  which creates the PHP server on the fly to provide a "single server"
  experience similar to that of `php-embed`.
* [`exec-php`](https://www.npmjs.com/package/exec-php) is another
  clever embedding which uses the ability of the PHP CLI binary
  to execute a single function in order to first export the
  set of functions defined in a PHP file (using the
  `_exec_php_get_user_functions` built-in) and then to implement
  function invocation.

# License
Copyright (c) 2015 C. Scott Ananian.

`php-embed` is licensed using the same
[license](http://www.php.net/license/3_01.txt) as PHP itself.

[`Promise`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise
[`call_user_func`]: http://php.net/manual/en/function.call-user-func.php
[`stream.Writable`]: https://nodejs.org/api/stream.html#stream_class_stream_writable
[`http.IncomingMessage`]: https://nodejs.org/api/http.html#http_http_incomingmessage
[`$_SERVER`]: http://php.net/manual/en/reserved.variables.server.php
[`fs`]: https://nodejs.org/api/fs.html
[`fs.readFile`]: https://nodejs.org/api/fs.html#fs_fs_readfile_filename_options_callback
[`isset()`]: http://php.net/manual/en/function.isset.php
[`unset()`]: http://php.net/manual/en/function.unset.php

[NPM1]: https://nodei.co/npm/php-embed.png
[NPM2]: https://nodei.co/npm/php-embed/

[1]: https://travis-ci.org/cscott/node-php-embed.png
[2]: https://travis-ci.org/cscott/node-php-embed
[3]: https://david-dm.org/cscott/node-php-embed.png
[4]: https://david-dm.org/cscott/node-php-embed
[5]: https://david-dm.org/cscott/node-php-embed/dev-status.png
[6]: https://david-dm.org/cscott/node-php-embed#info=devDependencies
