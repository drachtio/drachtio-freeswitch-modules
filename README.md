# drachtio-freeswitch-modules
An open-source collection of freeswitch modules, primarily intended for use with [drachtio](https://drachtio.org) applications utilizing [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf).  

Review the module listing below for more details on each module.

# Installation

## Overview
These modules require a custom version of freeswitch to be built with support for [grpc](https://github.com/grpc/grpc) compiled in, in order to talk to google cloud.  

This project includes the tools to build a 1.6 version of Freeswitch that includes grpc support.  Options are provided for building a native Freeswitch (Debian 8) using ansible, or a docker image.

## Building a native freeswitch
Please see the ansible role provided.

## Building a docker image
The [Dockerfile](./Dockerfile) in the top-level directory will build a Freeswitch 1.6 image that has support for GRPC compiled in.  GRPC is needed for the freeswitch modules that interact with google for speech, tts, or dialogflow.  Other than adding in GRPC support, this is a fairly simple and stripped-down version of Freeswitch designed primarily for applications that use only dialplan or event socket.  No lua, javascript or other scripting languages are commpiled into this image, and many of the less frequently-used modules are also not provided.

This is intended to be a base image that other Dockerfiles will reference via ONBBUILD directives to bring in their own dialplans and sip profiles to customize the install.

# module listing

## [mod_google_tts](modules/mod_google_tts/README.md)
Text-to-speech module using google cloud services.

## [mod_google_speech](modules/mod_google_tts/README.md)
Speech recognition / transcription using google cloud services.

## [mod_google_dialogflow](modules/mod_google_dialogflow/README.md)
Google dialogflow