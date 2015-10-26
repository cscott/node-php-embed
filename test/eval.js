require('should');

describe('Return values from PHP request', function() {
  var php = require('../');
  it('should return boolean values', function() {
    return php.request({ source: '(1 == 2)' }).then(function(v) {
      v.should.equal(false);
    });
  });
  it('should return integer values', function() {
    return php.request({ source: '1+2' }).then(function(v) {
      v.should.equal(3);
    });
  });
  it('should return double values', function() {
    return php.request({ source: '1/2' }).then(function(v) {
      v.should.equal(0.5);
    });
  });
  it('should return string values', function() {
    return php.request({ source: 'addslashes("abc")' }).then(function(v) {
      v.should.equal('abc');
    });
  });
  it.skip('should return wrapped PHP arrays', function() {
    return php.request({ source: 'array("abc"=>"def")' }).then(function(v) {
      v.should.equal({abc: 'def'});
    });
  });
  it('should return wrapped PHP objects', function() {
    return php.request({ source: 'new stdClass()' }).then(function(v) {
      (v instanceof php.PhpObject).should.be.true();
    });
  });
});
