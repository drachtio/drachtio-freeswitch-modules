# drachtio-freeswitch-modules
An open-source collection of freeswitch modules, primarily built for for use with [drachtio](https://drachtio.org) applications utilizing [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), but generally usable and useful with generic freeswitch applications. 

#### [mod_audio_fork](modules/mod_audio_fork/README.md)
Forks an audio stream and sends the raw audio in linear16 format over a websocket to a remote server in real-time. An initial text frame of JSON metadata can also be sent to the back-end to describe arbitrary information elements about the call or media stream.  The audio is never stored to disk locally on the media server, making it ideal for "no data at rest" type of applications.

#### [mod_google_tts](modules/mod_google_tts/README.md)
A tts provider module that integrates with Google Cloud Text-to-Speech API and integrates into freeswitch's TTS framework (i.e., usable with the mod_dptools 'speak' application)

#### [mod_google_transcribe](modules/mod_google_transcribe/README.md)
Adds a Freeswitch API call to start (or stop) real-time transcription on a Freeswitch channel using Google Cloud Speech-to-Text API.

#### [mod_dialogflow](modules/mod_dialogflow/README.md)
Adds a Freeswitch API to start a Google Dialogflow agent on a Freeswitch channel.

# Installation

These modules have dependencies that require a custom version of freeswitch to be built that has support for [grpc](https://github.com/grpc/grpc) and [libwebsockets](libwebsockets.org). Specifically, mod_google_tts, mod_google_transcribe and mod_dialogflow require grpc, and mod_audio_fork requires libwebsockets.

This project includes scripts to build a 1.6 version of Freeswitch that includes both libwebsockets and grpc support.  

Please see the [ansible role](./ansible-role-drachtio-freeswitch/README.md) provided.  This has been tested on Debian 8, and for those who prefer (or are willing) to use ansible, it is the simplest way to build up a Freeswitch server from source with the necessary patches and libraries to use these modules.

If you don't want to or can't use ansible for some reason and want to build everything by hand, have a look at the [build.sh](./build.sh) script, which has the commands to build freeswitch with the necessary support.  Again, to date please note that this has only been tested on Debian 8, since that is the reference platform for Freeswitch.

## Configuring

The three modules that access google services (mod_google_tts, mod_google_transcribe, and mod_dialogflow) require a JSON service key file to be installed on the Freeswitch server, and the filename of that file to be configured in the module config files.  By default, the config files look for the key in `/tmp/gcs_service_account_key.json` but you can change this by editing the config file.