# mod_aws_lex

A Freeswitch module that connects to [AWS Lex](https://docs.aws.amazon.com/lex/) using the streaming API.

Once a Freeswitch channel is connected to a Lex bot, media is streamed to Lex, which returns information describing the "intent" that was detected, along with transcriptions and audio prompts and text to play to the caller.  The handling of returned audio by the module is two-fold:
1.  If an audio clip was returned, it is *not* immediately played to the caller, but instead is written to a temporary file on the Freeswitch server.
2.  Next, a Freeswitch custom event is sent to the application containing the details of the dialogflow response as well as the path to the audio file.

This allows the application whether to decide to play the returned audio clip (via the mod_dptools 'play' command), or to use a text-to-speech service to generate audio using the returned prompt text.

## API

### Commands
The freeswitch module exposes the following API commands:

```
aws_lex_start <uuid>  botId aliasId region locale [welcome-intent]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - freeswitch channel uuid
- `bot` - name of Lex bot
- `alias` - alias of Lex bot
- `region` - AWS region name (e.g 'us-east-1')
- `locale` - AWS language to use for speech recognition (e.g. 'en-US')
- `welcome-intent` - name of intent to trigger initially

```
aws_lex_dtmf <uuid> dtmf-entry
```
Notify Lex of a dtmf entry

```
aws_lex_play_done <uuid> 
```
Notify Lex that an audio prompt has completed playing.  The application needs to call this if barge-in is enabled.
```
aws_lex_stop <uuid> 
```
Stop dialogflow on the channel.

### Channel variables
* `ACCESS_KEY_ID` - AWS access key id to use to authenticate; if not provided an environment variable of the same name is used if provided
* `SECRET_ACCESS_KEY` - AWS secret access key to use to authenticate; if not provided an environment variable of the same name is used if provided
* `LEX_WELCOME_MESSAGE` - text for a welcome message to play at audio start
* `x-amz-lex:start-silence-threshold-ms` - no-input timeout in milliseconds (Lex defaults to 4000 if not provided)

### Events
* `lex::intent` - an intent has been detected.
* `lex::transcription` - a transcription has been returned
* `lex::text_response` - a text response has been returned; the telephony application can play this using text-to-speech if desired.
* `lex::audio_provided` - an audio response (.mp3 format) has been returned; the telephony application can play this file if TTS is not being used
* `lex::text_response` - a text response was provided.
* `lex::playback_interruption` - the caller has spoken during prompt playback; the telephony application should kill the current audio prompt
* `lex::error` - dialogflow has returned an error
## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('aws_lex_start', `${ep.uuid} BookTrip Gamma us-east-1`); 
```

# Example application

See [drachtio-lex-gateway](https://github.com/drachtio/drachtio-lex-phone-gateway).