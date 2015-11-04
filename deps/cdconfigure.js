#!/usr/bin/env node
var child_process = require('child_process');
var path = require('path');
var os = require('os');

process.argv.shift(); // take off the 'node'
process.argv.shift(); // take off 'cdconfigure.js'
var confdir = process.argv.shift();
var rebuild_configure = (confdir === '--rebuild');
if (rebuild_configure) { confdir = process.argv.shift(); }
console.log("Changing to", confdir);

var rest = process.argv.slice(0);

if (rebuild_configure) {
  child_process.spawnSync(
    path.resolve(confdir, './buildconf'),
    ['--force'], {
      cwd: confdir,
      stdio: 'inherit'
    });
}

//console.log(os.arch());
//console.log(process.version, process.arch, process.config.variables.target_arch);

// HACK!  Can't figure out how to get libphp5.gyp to apply these options
// "properly".
switch (process.env.BUILDTYPE || 'Release') {
case 'Debug':
  rest.push('--enable-debug');
  break;
default:
case 'Release':
  rest.push('--enable-release');
  break;
}
// Fixup CFLAGS
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
c.on('close', function(code) {
	if (code === 0) { return; }

	// In order to diagnose the failure, dump the config log.
	console.log('\n\n\nConfig log:');
	var fs = require('fs');
	console.log(fs.readFileSync(path.join(confdir, 'config.log'), 'utf8'));
	process.exit(code);
});
