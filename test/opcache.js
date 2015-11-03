require('should');

describe('Opcache extension', function() {
  var php = require('../');
  it('should be loaded', function() {
    return php.request({ source: 'opcache_get_status() !== false' })
      .then(function(v) {
        v.should.equal(true);
      });
  });
});
