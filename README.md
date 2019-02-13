# drachtio-freeswitch-modules
An open-source collection of freeswitch modules, primarily intended for use with [drachtio](https://drachtio.org) applications utilizing [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf).  

Review the module listing below for more details on each module.

# module listing

## [mod_audio_fork](modules/mod_audio_fork/README.md)
Forks an audio stream and sends the raw audio in linear16 format over a websocket to a remote server in real-time. An initial text frame of JSON metadata can also be sent to the back-end to describe arbitrary information elements about the call or media stream.  The audio is never stored to disk locally on the media server, making it ideal for "no data at rest" type of applications.

## [mod_google_tts](modules/mod_google_tts/README.md)
Text-to-speech module using google cloud services.

## [mod_google_speech](modules/mod_google_tts/README.md)
Speech recognition / transcription using google cloud services.

## [mod_google_dialogflow](modules/mod_google_dialogflow/README.md)
Google dialogflow

# Installation

## Overview
These modules require a custom version of freeswitch to be built, because they each require support for either [grpc](https://github.com/grpc/grpc) or [libwebsockets](libwebsockets.org).  The google modules require grpc support, and mod_audio_fork requires libwebsockets.

This project includes the tools to build a 1.6 version of Freeswitch that includes both libwebsockets and grpc support.  Options are provided for building a native Freeswitch (Debian 8) using ansible, or a docker image.  See below for further details,

## Building a native freeswitch
Please see the [ansible role](./ansible-role-drachtio-freeswitch/README.md) provided.

If you don't want to or can't use ansible for some reason and want to build everything by hand, have a look at the [build.sh](./build.sh) script, which has the commands to build freeswitch with the necessary support along with the modules on a debian 8 server.

## Configuring
Please note that although the modules are built if you follow the instructions above, they are not enabled by default.  You will need to edit the `/usr/local/freeswitch/conf/autoload_configs/modules.conf.xml` file to add them in.

Also, please note that the google modules each require a config file to be placed into ``/usr/local/freeswitch/conf/autoload_configs`.  Copy the template config file from the module source "/conf" directory and edit appropriately.  You will need to download a google service key with permissions to execute the APIs that each module exercises, place it on the server somewhere, and reference it from the config file.

## Building a docker image
> This section in progress.
The [Dockerfile](./Dockerfile) in the top-level directory will build a Freeswitch 1.6 image that has support for GRPC compiled in.  GRPC is needed for the freeswitch modules that interact with google for speech, tts, or dialogflow.  Other than adding in GRPC support, this is a fairly simple and stripped-down version of Freeswitch designed primarily for applications that use only dialplan or event socket.  No lua, javascript or other scripting languages are commpiled into this image, and many of the less frequently-used modules are also not provided.

This is intended to be a base image that other Dockerfiles will reference via ONBBUILD directives to bring in their own dialplans and sip profiles to customize the install.

# module listing

## [mod_google_tts](modules/mod_google_tts/README.md)
Text-to-speech module using google cloud services.

## [mod_google_transcribe](modules/mod_google_transcribe)
Speech recognition / transcription using google cloud services.

## [mod_dialogflow](modules/mod_dialogflow/README.md)
Google dialogflow integration.
