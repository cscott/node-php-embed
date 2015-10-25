var path = require('path');

var StringStream = require('../test-stream.js');
require('should');

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
