# node-php-embed
[![NPM][NPM1]][NPM2]

[![Build Status][1]][2] [![dependency status][3]][4] [![dev dependency status][5]][6]

The node `php-embed` package binds to PHP's "embed SAPI" in order to
provide interoperability between PHP and JavaScript code.

Node/iojs >= 2.4.0 is currently required, since we use `NativeWeakMap`s
in the implementation.  This could probably be worked around using
v8 hidden properties, but it doesn't seem worth it right now.

# USAGE

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

# API

## php.request(options, [callback])
Triggers a PHP "request", and returns a `Promise` which will be
resolved when the request completes.  If you prefer to use callbacks,
you can ignore the return value and pass a callback as the second
parameter.
*   `options`: a hash containing various parameters for the request.
    Either `source` or `file` is mandatory; the rest are optional.
    - `source`:
        Specifies a source string to evaluate in the request context.
    - `file`:
        Specifies a PHP file to evaluate in the request context.
    - `stream`:
        A node `stream.Writable` to accept output from the PHP request.
        If not specified, defaults to `process.stdout`.
    - `context`:
        A JavaScript object which will be made available to the PHP
        request in `$_SERVER['CONTEXT']`.
*   `callback` *(optional)*: A standard node callback.  The first argument
    is non-null if an exception was raised. The second argument is the
    result of the PHP evaluation, converted to a string.

# PHP API

From the PHP side, there are three new classes defined, all in the
`Js` namespace, and one new property defined in the `$_SERVER`
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
$setTimeout(new Js\Wait(), 5000);
```
would halt the PHP thread for 5 seconds.  More usefully, if you were
to make the node `fs` module available to PHP as `$fs`, then:
```php
$contents = $fs.readFile('path/to/file', 'utf8', new Js\Wait());
```
would invoke the `readFile` method asynchronously in the node context,
but block the PHP thread until its callback was invoked.  The result
returned in the callback would then be used as the return value for
the function invocation, resulting in `$contents` getting the result
of reading the file.

Note that calls using `Js\Wait` block the PHP thread but do not
block the node thread.

# INSTALLING

You can use [`npm`](https://github.com/isaacs/npm) to download and install:

* The latest `php-embed` package: `npm install php-embed`

* GitHub's `master` branch: `npm install https://github.com/cscott/node-php-embed/tarball/master`

In both cases the module is automatically built with npm's internal
version of `node-gyp`, and thus your system must meet
[node-gyp's requirements](https://github.com/TooTallNate/node-gyp#installation).

It is also possible to make your own build of `php-embed` from its
source instead of its npm package ([see below](#building-from-the-source)).

# BUILDING FROM THE SOURCE

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
`apt-get install libphp5-embed php5-dev` for
Debian/Ubuntu.

Developers hacking on the code will probably want to use:

    node-pre-gyp --debug build

Passing the `--debug` flag to `node-pre-gyp` enables memory checking, and
the `build` command (instead of `rebuild`) avoids rebuilding `libphp5`
from scratch after every change.

# TESTING

[mocha](https://github.com/visionmedia/mocha) is required to run unit tests.

    npm install mocha
    npm test


# CONTRIBUTORS

* [C. Scott Ananian](https://github.com/cscott)

# RELATED PROJECTS

# LICENSE
Copyright (c) 2015 C. Scott Ananian.

`node-php-embed` is licensed using the same
[license](http://www.php.net/license/3_01.txt) as PHP itself.

[NPM1]: https://nodei.co/npm/php-embed.png
[NPM2]: https://nodei.co/npm/php-embed/

[1]: https://travis-ci.org/cscott/node-php-embed.png
[2]: https://travis-ci.org/cscott/node-php-embed
[3]: https://david-dm.org/cscott/node-php-embed.png
[4]: https://david-dm.org/cscott/node-php-embed
[5]: https://david-dm.org/cscott/node-php-embed/dev-status.png
[6]: https://david-dm.org/cscott/node-php-embed#info=devDependencies
