var binary = require('node-pre-gyp');
var path = require('path');
var binding_path =
  binary.find(path.resolve(path.join(__dirname, '..', 'package.json')));
var bindings = require(binding_path);

Object.keys(bindings).forEach(function(k) {
    exports[k] = bindings[k];
});
