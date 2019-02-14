#!/bin/bash
set -x

# install dependencies
apt-get update
apt-get install -y --quiet --no-install-recommends git curl build-essential make autoconf automake gcc g++ cmake libssl-dev libtool libtool-bin libjpeg-dev pkg-config libsqlite3-dev libcurl4-openssl-dev libpcre3-dev libspeex-dev libspeexdsp-dev libldns-dev libedit-dev libssl-dev yasm libopus-dev libsndfile-dev

# check out supporting code
cd /usr/local/src/
git clone https://github.com/davehorton/drachtio-freeswitch-modules.git
git clone -b v1.6 https://freeswitch.org/stash/scm/fs/freeswitch.git
curl -L https://grpc.io/release
git clone -b v1.18.0 https://github.com/grpc/grpc
cd freeswitch/libs
git clone -b v3.1.0 https://github.com/warmcat/libwebsockets.git

# patch freeswitch
cd /usr/local/src/freeswitch
patch -b < /usr/local/src/drachtio-freeswitch-modules/ansible-role-drachtio-freeswitch/files/configure.ac.patch
patch -b < /usr/local/src/drachtio-freeswitch-modules/ansible-role-drachtio-freeswitch/files/Makefile.am.patch
cp /usr/local/src/drachtio-freeswitch-modules/ansible-role-drachtio-freeswitch/files/modules.conf .
cd src/mod/applications
cp -r /usr/local/src/drachtio-freeswitch-modules/modules/mod_* .

# build libwebsockets
cd /usr/local/src/freeswitch/libs
cd libwebsockets/
mkdir build && cd $_
cmake ..
make
make install
cd /usr/local/src/

# build grpc
cd /usr/local/src/
cd grpc
git submodule update --init --recursive
cd third_party/protobuf
./autogen.sh
./configure
make install
cd ../..
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH && make
make install

# now make freeswitch
cd ../freeswitch/libs/
git clone https://github.com/googleapis/googleapis
cd googleapis
LANGUAGE=cpp make
cd /usr/local/src/freeswitch
./bootstrap.sh -j
./configure --with-lws=yes --with-grpc=yes
make
make install
make sounds-install moh-install