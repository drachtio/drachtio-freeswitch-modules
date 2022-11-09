# mod_deepgram_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using Deepgram's streaming transcription API

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_deepgram_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid AWS [language code](https://docs.deepgram.amazon.com/transcribe/latest/dg/what-is-transcribe.html) that is supported for streaming transcription
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
uuid_deepgram_transcribe <uuid> stop
```
Stop transcription on the channel.

### Channel Variables

| variable | Description |
| --- | ----------- |
| DEEPGRAM_API_KEY | Deepgram API key used to authenticate |
| DEEPGRAM_SPEECH_TIER | https://developers.deepgram.com/documentation/features/tier/ |
| DEEPGRAM_SPEECH_CUSTOM_MODEL | custom model id |
| DEEPGRAM_SPEECH_MODEL | https://developers.deepgram.com/documentation/features/model/ |
| DEEPGRAM_SPEECH_MODEL_VERSION | https://developers.deepgram.com/documentation/features/version/ |
| DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION | https://developers.deepgram.com/documentation/features/punctuate/ |
| DEEPGRAM_SPEECH_PROFANITY_FILTER | https://developers.deepgram.com/documentation/features/profanity-filter/ |
| DEEPGRAM_SPEECH_REDACT | https://developers.deepgram.com/documentation/features/redact/ |
| DEEPGRAM_SPEECH_DIARIZE | https://developers.deepgram.com/documentation/features/diarize/  |
| DEEPGRAM_SPEECH_DIARIZE_VERSION |  https://developers.deepgram.com/documentation/features/diarize/  |
| DEEPGRAM_SPEECH_NER | https://developers.deepgram.com/documentation/features/named-entity-recognition/ |
| DEEPGRAM_SPEECH_ALTERNATIVES | number of alternative hypotheses to return (default: 1) |
| DEEPGRAM_SPEECH_NUMERALS | https://developers.deepgram.com/documentation/features/numerals/ |
| DEEPGRAM_SPEECH_SEARCH | https://developers.deepgram.com/documentation/features/search/ |
| DEEPGRAM_SPEECH_KEYWORDS | https://developers.deepgram.com/documentation/features/keywords/ |
| DEEPGRAM_SPEECH_REPLACE | https://developers.deepgram.com/documentation/features/replace/  |
| DEEPGRAM_SPEECH_TAG | https://developers.deepgram.com/documentation/features/tag/ |
| DEEPGRAM_SPEECH_ENDPOINTING  | https://developers.deepgram.com/documentation/features/endpointing/ |
| DEEPGRAM_SPEECH_VAD_TURNOFF | https://developers.deepgram.com/documentation/features/voice-activity-detection/ |


### Events
`deepgram_transcribe::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
```js
{
	"channel_index": [0, 1],
	"duration": 4.59,
	"start": 0.0,
	"is_final": true,
	"speech_final": true,
	"channel": {
		"alternatives": [{
			"transcript": "hello hello hello",
			"confidence": 0.98583984,
			"words": [{
				"word": "hello",
				"start": 3.0865219,
				"end": 3.206,
				"confidence": 0.99902344
			}, {
				"word": "hello",
				"start": 3.5644348,
				"end": 3.644087,
				"confidence": 0.9741211
			}, {
				"word": "hello",
				"start": 4.042348,
				"end": 4.3609567,
				"confidence": 0.98583984
			}]
		}]
	},
	"metadata": {
		"request_id": "37835678-5d3b-4c77-910e-f8914c882cec",
		"model_info": {
			"name": "conversationalai",
			"version": "2021-11-10.1",
			"tier": "base"
		},
		"model_uuid": "6b28e919-8427-4f32-9847-492e2efd7daf"
	}
}
```

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim`);  
```

