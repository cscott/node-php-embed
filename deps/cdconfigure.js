#!/usr/bin/env node
var child_process = require('child_process');
var path = require('path');
var os = require('os');

process.argv.shift(); // take off the 'node'
process.argv.shift(); // take off 'cdconfigure.js'
var confdir = process.argv.shift();
console.log("Changing to", confdir);

var rest = process.argv.slice(0);

//console.log(os.arch());
//console.log(process.version, process.arch, process.config.variables.target_arch);

if (process.config.variables.target_arch !== 'ia32') {
    // needed for x86_64
    rest.push('CFLAGS=-fPIC');
    rest.push('CXXFLAGS=-fPIC');
} else {
    // needed for multilib builds on x86_64 host
    rest.push('CFLAGS=-m32');
    rest.push('CXXFLAGS=-m32');
}

console.log(path.resolve(confdir, './configure'), rest.join(' '));
var c = child_process.spawn(path.resolve(confdir, './configure'), rest, {
    cwd: confdir,
    stdio: 'inherit'
});
