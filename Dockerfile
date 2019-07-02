FROM debian:stretch

COPY ./modules/mod_audio_fork /mod_google_audio_fork
COPY ./modules/mod_google_tts /mod_google_tts
COPY ./modules/mod_google_transcribe /mod_google_transcribe
COPY ./modules/mod_dialogflow /mod_dialogflow
COPY ./files /files
COPY ./build.sh /build.sh
RUN chmod +x /build.sh

RUN apt-get update && apt-get -y --quiet --force-yes upgrade \
    && apt-get install -y --quiet --no-install-recommends wget git ca-certificates

# We need this here so that the symlinks in /files point to patches
#  and templates in this repo. There is almost certainly a better way
#  to do this.
RUN git clone https://github.com/davehorton/ansible-role-fsmrf.git
RUN mkdir -p /usr/local/src/freeswitch /usr/local/freeswitch

RUN /build.sh

RUN rm -rf /usr/local/src

#ADD conf.tar.gz /usr/local/freeswitch

RUN groupadd -r freeswitch && useradd -r -g freeswitch freeswitch

#ONBUILD ADD dialplan /usr/local/freeswitch/conf/dialplan
#ONBUILD ADD sip_profiles /usr/local/freeswitch/conf/sip_profiles

RUN chown -R freeswitch:freeswitch /usr/local/freeswitch
