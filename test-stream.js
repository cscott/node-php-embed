// Helper for testing PHP requests: captures all output in a string.
var stream = require('readable-stream'); // For node 0.8.x compatability
var util = require('util');

var StringStream = module.exports = function(opts) {
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
