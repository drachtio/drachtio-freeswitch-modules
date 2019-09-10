# mod_google_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using Google's Speech-to-Text API.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_google_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid Google [language code](https://cloud.google.com/speech-to-text/docs/languages) to use for speech recognition
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
uuid_google_transcribe <uuid> stop
```
Stop transcription on the channel.

### Channel Variables
Additional google speech options can be set through freeswitch channel variables listed below.

| variable | Description |
| --- | ----------- |
| GOOGLE_SPEECH_SINGLE_UTTERANCE | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.StreamingRecognitionConfig.FIELDS.bool.google.cloud.speech.v1.StreamingRecognitionConfig.single_utterance) |
| GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_separate_recognition_per_channel) |
| GOOGLE_SPEECH_MAX_ALTERNATIVES | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.int32.google.cloud.speech.v1.RecognitionConfig.max_alternatives) |
| GOOGLE_SPEECH_PROFANITY_FILTER | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.profanity_filter) |
| GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_word_time_offsets) |
| GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_automatic_punctuation) |
| GOOGLE_SPEECH_MODEL | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.string.google.cloud.speech.v1.RecognitionConfig.model) |
| GOOGLE_SPEECH_USE_ENHANCED | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.use_enhanced) |
| GOOGLE_SPEECH_HINTS | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.SpeechContext.FIELDS.repeated.string.google.cloud.speech.v1.SpeechContext.phrases) |



### Events
`google_transcribe::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
```js
{
	"stability": 0,
	"is_final": true,
	"alternatives": [{
		"confidence": 0.96471,
		"transcript": "Donny was a good bowler, and a good man"
	}]
}
```

`google_transcribe::end_of_utterance` - returns an indication that an utterance has been detected.  This may be returned prior to a final transcription.  This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

`google_transcribe::end_of_transcript` - returned when a transcription operation has completed. If a final transcription has not been returned by now, it won't be. This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

`google_transcribe::no_audio_detected` - returned when google has returned an error indicating that no audio was received for a lengthy period of time.

`google_transcribe::max_duration_exceeded` - returned when google has returned an an indication that a long-running transcription has been stopped due to a max duration limit (305 seconds) on their side.  It is the applications responsibility to respond by starting a new transcription session, if desired.

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US`);  
```
## Examples
[google_transcribe.js](../../examples/google_transcribe.js)