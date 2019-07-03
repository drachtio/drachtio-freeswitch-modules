#!/bin/bash
# Derived from an Ansible task deploy, do not hand edit unless desperate
cd /usr/local/src/freeswitch/
make
apt-get update -q
apt-get install -q -y curl gnupg2 wget git cmake automake autoconf libtool build-essential pkg-config ca-certificates libssl-dev libz-dev libjpeg-dev libsqlite3-dev libcurl4-openssl-dev libpcre3-dev libspeex-dev libspeexdsp-dev libedit-dev libtiff-dev yasm libopus-dev libsndfile-dev libshout3-dev libmpg123-dev libmp3lame-dev
apt-get install -q -y libtool-bin
wget  --no-check-certificate  -O - https://files.freeswitch.org/repo/deb/freeswitch-1.8/fsstretch-archive-keyring.asc | apt-key add -
git config --global pull.rebase true
( echo "deb http://files.freeswitch.org/repo/deb/freeswitch-1.8/ stretch main" > /etc/apt/sources.list.d/freeswitch.list
echo "deb-src http://files.freeswitch.org/repo/deb/freeswitch-1.8/ stretch main" >> /etc/apt/sources.list.d/freeswitch.list
exit 0
 )
git clone https://freeswitch.org/stash/scm/fs/freeswitch.git /usr/local/src/freeswitch/ --branch v1.8.5
git clone https://github.com/warmcat/libwebsockets.git /usr/local/src/freeswitch//libs/libwebsockets --branch v3.1.0
git clone https://github.com/davehorton/drachtio-freeswitch-modules.git /usr/local/src/drachtio-freeswitch-modules
patch /usr/local/src/freeswitch/configure.ac /files/configure.ac.patch
cp -r /usr/local/src/drachtio-freeswitch-modules/modules/mod_audio_fork /usr/local/src/freeswitch//src/mod/applications/mod_audio_fork
patch /usr/local/src/freeswitch/Makefile.am /files/Makefile.am.patch
patch /usr/local/src/freeswitch/build/modules.conf.in /files/modules.conf.in.patch
cp /files/modules.conf.vanilla.xml.lws /usr/local/src/freeswitch//conf/vanilla/autoload_configs/modules.conf.xml
test -f /usr/local/src/freeswitch//conf/vanilla/autoload_configs/modules.conf.xml || exit 1
cd /usr/local/src/freeswitch//libs/libwebsockets
mkdir -p build && cd build && cmake .. && make && make install
cp -r -n "/usr/local/src/drachtio-freeswitch-modules/modules/mod_google_tts/" "/usr/local/src/freeswitch//src/mod/applications/"
cp -r -n "/usr/local/src/drachtio-freeswitch-modules/modules/mod_google_transcribe/" "/usr/local/src/freeswitch//src/mod/applications/"
cp -r -n "/usr/local/src/drachtio-freeswitch-modules/modules/mod_dialogflow/" "/usr/local/src/freeswitch//src/mod/applications/"
patch /usr/local/src/freeswitch/Makefile.am /files/Makefile.am.grpc.patch
patch /usr/local/src/freeswitch/configure.ac /files/configure.ac.grpc.patch
patch /usr/local/src/freeswitch/build/modules.conf.in /files/modules.conf.in.grpc.patch
cp /files/modules.conf.vanilla.xml.grpc /usr/local/src/freeswitch//conf/vanilla/autoload_configs/modules.conf.xml
test -f /usr/local/src/freeswitch//conf/vanilla/autoload_configs/modules.conf.xml || exit 1
git clone https://github.com/grpc/grpc /usr/local/src/grpc --branch v1.20.0
cd /usr/local/src/grpc
git submodule update --init --recursive
cd /usr/local/src/grpc/third_party/protobuf
./autogen.sh && ./configure && make install
cd /usr/local/src/grpc
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH && make && make install
git clone https://github.com/googleapis/googleapis /usr/local/src/freeswitch//libs/googleapis --branch adsgen
cd /usr/local/src/freeswitch//libs/googleapis
LANGUAGE=cpp make
cd /usr/local/src/freeswitch/
./bootstrap.sh -j
cd /usr/local/src/freeswitch/
./configure CFLAGS="-D__alloca=alloca" --with-lws=yes --with-grpc=yes
cd /usr/local/src/freeswitch/
make
cd /usr/local/src/freeswitch/
make install
cd /usr/local/src/freeswitch/ && make cd-sounds-install cd-moh-install
cp /files/acl.conf.xml.j2 /usr/local/freeswitch/conf/autoload_configs/acl.conf.xml
test -f /usr/local/freeswitch/conf/autoload_configs/acl.conf.xml || exit 1
cp /files/event_socket.conf.xml.j2 /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml
test -f /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml || exit 1
rm -rf /usr/local/freeswitch/conf/sip_profiles/external
rm -rf /usr/local/freeswitch/conf/sip_profiles/external.xml
rm -rf /usr/local/freeswitch/conf/sip_profiles/external-ipv6.xml
rm -rf /usr/local/freeswitch/conf/sip_profiles/internal.xml
rm -rf /usr/local/freeswitch/conf/sip_profiles/internal-ipv6.xml
rm -rf /usr/local/freeswitch/conf/sip_profiles/external-ipv6
rm -rf /usr/local/freeswitch/conf/dialplan/default
rm -rf /usr/local/freeswitch/conf/dialplan/default.xml
rm -rf /usr/local/freeswitch/conf/dialplan/public.xml
rm -rf /usr/local/freeswitch/conf/dialplan/features.xml
rm -rf /usr/local/freeswitch/conf/dialplan/skinny-patterns.xml
rm -rf /usr/local/freeswitch/conf/dialplan/public
rm -rf /usr/local/freeswitch/conf/dialplan/skinny-patterns
cp /files/mrf_dialplan.xml.j2 /usr/local/freeswitch/conf/dialplan/mrf.xml
test -f /usr/local/freeswitch/conf/dialplan/mrf.xml || exit 1
sed -i.bak 's/^.*rtp-start-port.*$/<param name="rtp-start-port" value="25000"\/>/' /usr/local/freeswitch/conf/autoload_configs/switch.conf.xml
cp /files/mrf_sip_profile.xml.j2 /usr/local/freeswitch/conf/sip_profiles/mrf.xml
test -f /usr/local/freeswitch/conf/sip_profiles/mrf.xml || exit 1
sed -i.bak 's/^.*rtp-end-port.*$/<param name="rtp-end-port" value="39000"\/>/' /usr/local/freeswitch/conf/autoload_configs/switch.conf.xml
cp /files/freeswitch_log_rotation.j2 /etc/cron.daily/freeswitch_log_rotation
test -f /etc/cron.daily/freeswitch_log_rotation || exit 1
chmod a+x /etc/cron.daily/freeswitch_log_rotation
