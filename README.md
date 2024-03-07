**Please note: This repo is no longer being actively maintained.**

**For those interested in similar modules, please refer to [this repo](https://github.com/jambonz/freeswitch-modules)  which is offered under a different (dual-licensing) scheme:**

- **AGLP-3 for general usage.**
- **MIT for use with [jambonz](https://jambonz.org) installs <ins>only</ins>.**

# drachtio-freeswitch-modules

An open-source collection of freeswitch modules, primarily built for for use with [drachtio](https://drachtio.org) applications utilizing [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), but generally usable and useful with generic freeswitch applications.  These modules have beeen tested with Freeswitch version 1.8.

#### [mod_audio_fork](modules/mod_audio_fork/README.md)
A Freeswitch module that attaches a bug to a media server endpoint and streams L16 audio via websockets to a remote server. The audio is never stored to disk locally on the media server, making it ideal for "no data at rest" type of applications.  This module also supports receiving media from the server to play back to the caller, enabling the creation of full-fledged IVR or dialog-type applications.

#### [mod_google_tts](modules/mod_google_tts/README.md)
A tts provider module that integrates with Google Cloud Text-to-Speech API and integrates into freeswitch's TTS framework (i.e., usable with the mod_dptools 'speak' application)

#### [mod_google_transcribe](modules/mod_google_transcribe/README.md)
Adds a Freeswitch API call to start (or stop) real-time transcription on a Freeswitch channel using Google Cloud Speech-to-Text API.

#### [mod_dialogflow](modules/mod_dialogflow/README.md)
Adds a Freeswitch API to start a Google Dialogflow agent on a Freeswitch channel.

#### [mod_aws_lex](modules/mod_aws_lex/README.md)
Adds Freeswitch APIs call to integrate with aws lex v2 apis.

#### [mod_aws_transcribe](modules/mod_aws_transcribe/README.md)
Adds a Freeswitch API call to start (or stop) real-time transcription on a Freeswitch channel using AWS streaming transcription (HTTP/2 based).


# Installation

These modules have dependencies that require a custom version of freeswitch to be built that has support for [grpc](https://github.com/grpc/grpc) (if any of the google modules are built) and [libwebsockets](https://libwebsockets.org). Specifically, mod_google_tts, mod_google_transcribe and mod_dialogflow require grpc, and mod_audio_fork requires libwebsockets.

#### Building from source
[This ansible role](https://github.com/davehorton/ansible-role-fsmrf) can be used to build a freeswitch 1.8 with support for these modules.  Even if you don't want to use ansible for some reason, the [task files](https://github.com/davehorton/ansible-role-fsmrf/tree/master/tasks), and the [patchfiles](https://github.com/davehorton/ansible-role-fsmrf/tree/master/files) should let you work out how to build it yourself manually or through your preferred automation (but why not just use ansible!)

> Note: that ansible role assumes you are building on Debian 9 (stretch).

#### Using docker

`docker pull drachtio/drachtio-freeswitch-mrf:v1.10.1-full` to get a docker image containing all of the above modules with the exception of mod_aws_transcribe.

## Configuring

The three modules that access google services (mod_google_tts, mod_google_transcribe, and mod_dialogflow) require a JSON service key file to be installed on the Freeswitch server, and the environment variable named "GOOGLE_APPLICATION_CREDENTIALS" must point to that file location.