require('should');

var StringStream = require('../test-stream.js');

describe('Passing command-line arguments to PHP request', function() {
  var php = require('../');
  it('$argc should not be present normally', function() {
    var out = new StringStream();
    return php.request({
      source: 'var_dump($argc)',
      stream: out,
    }).then(function(v, o) {
      out.toString().should.equal('NULL\n');
    });
  });
  it('$argv should not be present normally', function() {
    var out = new StringStream();
    return php.request({
      source: 'var_dump($argv)',
      stream: out,
    }).then(function(v, o) {
      out.toString().should.equal('NULL\n');
    });
  });
  it('$argc should be present when passed', function() {
    var out = new StringStream();
    return php.request({
      source: 'var_dump($argc)',
      stream: out,
      args: [ 1, 'abc' ],
    }).then(function(v, o) {
      out.toString().should.equal('int(2)\n');
    });
  });
  it('$argv should be present when passed', function() {
    var out = new StringStream();
    return php.request({
      source: 'var_dump($argv)',
      stream: out,
      args: [ 1, 'abc' ],
    }).then(function(v, o) {
      out.toString().should.equal([
        'array(2) {',
        '  [0]=>',
        '  string(1) "1"',
        '  [1]=>',
        '  string(3) "abc"',
        '}',
        '',
      ].join('\n'));
    });
  });
});
