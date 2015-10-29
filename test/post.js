var Promise = require('prfun');
var StringStream = require('../test-stream.js');
var http = require('http');
var querystring = require('querystring');
var should = require('should');
require('should-http');

describe('Feeding POST data from JS to PHP', function() {
  var php = require('../');
  var extend = function(obj1, obj2) {
    Object.keys(obj2).forEach(function(k) { obj1[k] = obj2[k]; });
    return obj1;
  };
  // Create an HTTP server for these tests.
  var makeServer = function(httpOptions, phpOptions, requestFunc) {
    var phpValueResolver = Promise.defer();
    var responseResolver = Promise.defer();
    var outputResolver = Promise.defer();

    var out = new StringStream();
    var server = http.createServer(function(request, response) {
      php.request(extend({
        request: request,
        stream: response,
      }, phpOptions)).
      tap(function() { response.end(); }).
      then(phpValueResolver.resolve, phpValueResolver.reject);
    });
    server.listen(0/* Random port */, function() {
      var address = server.address();
      var req = http.request(extend({
        host: address.address,
        port: address.port,
      }, httpOptions), function(res) {
        res.on('end', function() {
          responseResolver.resolve(res);
          outputResolver.resolve(out.toString());
        });
        // Pipe the output to a string stream
        res.pipe(out);
      });
      req.on('error', responseResolver.reject);
      if (requestFunc) { requestFunc(req); }
      req.end();
    });
    return Promise.all([
      phpValueResolver.promise,
      outputResolver.promise,
      responseResolver.promise,
    ]);
  };
  it('should handle a basic GET request', function() {
    return makeServer({
      path: '/index.html?abc=def&foo=bar+bat',
    }, {
      source: 'var_dump($_GET)',
    }).spread(function(phpvalue, output, response) {
      should(phpvalue).be.null();
      output.should.be.equal([
        'array(2) {',
        '  ["abc"]=>',
        '  string(3) "def"',
        '  ["foo"]=>',
        '  string(7) "bar bat"',
        '}',
        '',
      ].join('\n'));
      response.should.be.html();
      response.should.have.status(200);
      response.should.have.header('x-powered-by');
    });
  });
  it('should handle a basic POST request', function() {
    var postData = querystring.stringify({
      msg: 'Hello World!',
      foo: 'bar bat',
    });
    return makeServer({
      path: '/post',
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': postData.length,
      },
    }, {
      source: 'var_dump($_POST)',
    }, function(request) {
      request.write(postData);
    }).spread(function(phpvalue, output, response) {
      should(phpvalue).be.null();
      output.should.be.equal([
        'array(2) {',
        '  ["msg"]=>',
        '  string(12) "Hello World!"',
        '  ["foo"]=>',
        '  string(7) "bar bat"',
        '}',
        '',
      ].join('\n'));
      response.should.be.html();
      response.should.have.status(200);
      response.should.have.header('x-powered-by');
    });
  });
  it('should handle cookies', function() {
    return makeServer({
      path: '/cookie/test',
      headers: {
        // RFC 6265 says, "When the user agent generates an HTTP request,
        // the user agent MUST NOT attach more than one Cookie header
        // field." (sec 5.4)  So we don't need to worry about duplicate
        // headers sent *to* PHP (except for set-cookie, perhaps, but
        // that would be very unusual being sent *to* PHP).
        Cookie: 'foo=bar; bat=ball',
      },
    }, {
      source: [
		'call_user_func(function() {',
		'  # ensure we handle duplicate headers sent from PHP',
		'  setcookie("a", "b");',
		'  setcookie("c", "d", 0, "/");',
		'  var_dump($_COOKIE);',
		'  return 1;',
		'})',
      ].join('\n'),
    }).spread(function(phpvalue, output, response) {
      should(phpvalue).be.equal(1);
      output.should.be.equal([
        'array(2) {',
        '  ["foo"]=>',
        '  string(3) "bar"',
        '  ["bat"]=>',
        '  string(4) "ball"',
        '}',
        '',
      ].join('\n'));
      response.should.be.html();
      response.should.have.status(200);
      response.should.have.header('x-powered-by');
      response.should.have.header('set-cookie');
      response.headers['set-cookie'].should.eql([
        'a=b',
        'c=d; path=/',
      ]);
    });
  });
});
