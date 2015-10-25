var path = require('path');
var stream = require('readable-stream'); // For node 0.8.x compatability
var util = require('util');

require('should');

var StringStream = function(opts) {
  opts = opts || {};
  StringStream.super_.call(this, opts);
  this._result = '';
  this._encoding = opts.encoding || 'utf8';
};
util.inherits(StringStream, stream.Writable);
StringStream.prototype._write = function(chunk, encoding, callback) {
  this._result += chunk.toString(this._encoding);
  return callback(null);
};
StringStream.prototype.toString = function() {
  return this._result;
};

describe('Hello, world in PHP', function() {
  var php = require('../');
  it('should execute a basic test', function() {
    var out = new StringStream();
    return php.request({
      file: path.join(__dirname, '1-hello.php'),
      stream: out,
    }).then(function(v) {
      out.toString().should.equal('<h1>Hello, world! 3</h1>\n');
    });
  });
});
