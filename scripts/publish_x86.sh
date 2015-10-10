#!/bin/bash
set -e

platform=$(uname -s | sed "y/ABCDEFGHIJKLMNOPQRSTUVWXYZ/abcdefghijklmnopqrstuvwxyz/")

if [[ $(uname -s) != 'Linux' ]]; then
    exit 0
fi

npm run clean
npm install core-js@1.1.4
npm install request@2.64.0

# borrowed from node-sqlite3 scripts/build_against_node.sh


# These steps are taken care on in .travis.yml now.
#sudo apt-get update
#sudo apt-get -y install gcc-5-multilib g++-5-multilib

# node v0.8 and above provide pre-built 32 bit and 64 bit binaries
# so here we use the 32 bit ones to also test 32 bit builds
NVER=`node -v`
# enable 32 bit node
if [[ ${NODE_VERSION:0:4} == 'iojs' ]]; then
    wget https://iojs.org/download/release/${NVER}/iojs-${NVER}-${platform}-x86.tar.gz
    tar xf iojs-${NVER}-${platform}-x86.tar.gz
    # enable 32 bit iojs
    export PATH=$(pwd)/iojs-${NVER}-${platform}-x86/bin:$(pwd)/iojs-${NVER}-${platform}-ia32/bin:$PATH
else
    wget http://nodejs.org/dist/${NVER}/node-${NVER}-${platform}-x86.tar.gz
    tar xf node-${NVER}-${platform}-x86.tar.gz
    # enable 32 bit node
    export PATH=$(pwd)/node-${NVER}-${platform}-x86/bin:$(pwd)/node-${NVER}-${platform}-ia32/bin:$PATH
fi
node -e "console.log(process.arch,process.execPath)"
if [ `node -e "console.log(process.arch)"` != "ia32" ]; then
    echo "Can't built 32-bit binaries."
    exit 1
fi
# install 32 bit compiler toolchain and X11
# test source compile in 32 bit mode
npm install --build-from-source
node-pre-gyp package testpackage
npm test
npm run gh-publish
npm run clean
