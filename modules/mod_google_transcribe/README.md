# mod_google_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using Google's Speech-to-Text API.

Optionally, the connection to the google cloud recognizer can be delayed until voice activity has been detected.  This can be useful in cases where it is desired to minimize the costs of streaming audio for transcription.  This setting is governed by the channel variables starting with 1RECOGNIZER_VAD`, as described below.

## API

### Commands
The freeswitch module exposes two versions of an API command to transcribe speech:
#### version 1
```bash
uuid_google_transcribe <uuid> start <lang-code> [interim]
```
When using this command, additional speech processing options can be provided through Freeswitch channel variables, described [below](#command-variables).

####version 2
```bash
uuid_google_transcribe2 <uuid> start <lang-code> [interim] (bool) \
[single-utterance](bool) [separate-recognition](bool) [max-alternatives](int) \
[profanity-filter](bool) [word-time](bool) [punctuation](bool) \
[model](string) [enhanced](bool) [hints](word seperated by , and no spaces) \
[play-file] (play file path)
```
This command allows speech processing options to be provided on the command line, and has the ability to optionally play an audio file as a prompt.

Example:
```bash
bgapi uuid_google_transcribe2 312033b6-4b2a-48d8-be0c-5f161aec2b3e start en-US \
true true true 5 true true true command_and_search true \
yes,no,hello https://www2.cs.uic.edu/~i101/SoundFiles/CantinaBand60.wav
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid Google [language code](https://cloud.google.com/speech-to-text/docs/languages) to use for speech recognition
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
uuid_google_transcribe <uuid> stop
```
Stop transcription on the channel.

### Command Variables
Additional google speech options can be set through freeswitch channel variables for `uuid_google_transcribe` (some can alternatively be set in the command line for `uuid_google_transcribe2`).

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
| GOOGLE_SPEECH_HINTS | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.PhraseSet) |
| GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES | a comma-separated list of language codes, [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionConfig.FIELDS.repeated.string.google.cloud.speech.v1p1beta1.RecognitionConfig.alternative_language_codes) |
| GOOGLE_SPEECH_SPEAKER_DIARIZATION | set to 1 to enable [speaker diarization](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
|  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
|  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
| GOOGLE_SPEECH_METADATA_INTERACTION_TYPE | set to 'discussion', 'presentation', 'phone_call', 'voicemail', 'professionally_produced', 'voice_search', 'voice_command', or 'dictation' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.InteractionType) |
| GOOGLE_SPEECH_METADATA_INDUSTRY_NAICS_CODE | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata) |
| GOOGLE_SPEECH_METADATA_MICROPHONE_DISTANCE | set to 'nearfield', 'midfield', or 'farfield' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.MicrophoneDistance) |
| GOOGLE_SPEECH_METADATA_ORIGINAL_MEDIA_TYPE | set to 'audio', or 'video' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.OriginalMediaType) |
| GOOGLE_SPEECH_METADATA_RECORDING_DEVICE_TYPE | set to 'smartphone', 'pc', 'phone_line', 'vehicle', 'other_outdoor_device', or 'other_indoor_device' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.RecordingDeviceType)|
| START_RECOGNIZING_ON_VAD | if set to 1 or true, do not begin streaming audio to google cloud until voice activity is detected.|
| RECOGNIZER_VAD_MODE | An integer value 0-3 from less to more aggressive vad detection (default: 2).|
| RECOGNIZER_VAD_VOICE_MS | The number of milliseconds of voice activity that is required to trigger the connection to google cloud, when START_RECOGNIZING_ON_VAD is set (default: 250).|
| RECOGNIZER_VAD_DEBUG | if >0 vad debug logs will be generated (default: 0).|


### Events
**google_transcribe::transcription** - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
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

**google_transcribe::end_of_utterance** - returns an indication that an utterance has been detected.  This may be returned prior to a final transcription.  This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

**google_transcribe::end_of_transcript** - returned when a transcription operation has completed. If a final transcription has not been returned by now, it won't be. This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

**google_transcribe::no_audio_detected** - returned when google has returned an error indicating that no audio was received for a lengthy period of time.

**google_transcribe::max_duration_exceeded** - returned when google has returned an an indication that a long-running transcription has been stopped due to a max duration limit (305 seconds) on their side.  It is the applications responsibility to respond by starting a new transcription session, if desired.

**google_transcribe::no_audio_detected** - returned when google has not received any audio for some reason.

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US`);  
```
## Examples
[google_transcribe.js](../../examples/google_transcribe.js)