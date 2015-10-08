# node-php-embed
[![NPM][NPM1]][NPM2]

[![Build Status][1]][2] [![dependency status][3]][4] [![dev dependency status][5]][6]

The node `php-embed` package binds to PHP's "embed SAPI" in order to
provide interoperability between PHP and JavaScript code.

# USAGE

# API

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
