FROM debian:stretch

COPY ./modules/mod_audio_fork /mod_google_audio_fork
COPY ./modules/mod_google_tts /mod_google_tts
COPY ./modules/mod_google_transcribe /mod_google_transcribe
COPY ./modules/mod_dialogflow /mod_dialogflow
COPY ./files /files
COPY ./build.sh /build.sh
RUN chmod +x /build.sh

# We need this here so we can git fetch the ansible-roles repo with patches
#  and templates that we symlink from /files. Misc other stuff that the ansible
#  tasks we copied in build.sh depend on.
RUN apt-get update && apt-get -y --quiet --force-yes upgrade \
    && apt-get install -y --quiet --no-install-recommends ca-certificates git wget
RUN git clone https://github.com/davehorton/ansible-role-fsmrf.git

RUN /build.sh

RUN rm -rf /usr/local/src

#ADD conf.tar.gz /usr/local/freeswitch

RUN groupadd -r freeswitch && useradd -r -g freeswitch freeswitch

#ONBUILD ADD dialplan /usr/local/freeswitch/conf/dialplan
#ONBUILD ADD sip_profiles /usr/local/freeswitch/conf/sip_profiles

RUN chown -R freeswitch:freeswitch /usr/local/freeswitch
