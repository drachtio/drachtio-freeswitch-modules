# mod_ibm_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using IBM Watson

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_ibm_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid IBM [language code](https://cloud.ibm.com/docs/speech-to-text?topic=speech-to-text-models-ng#models-ng-supported) that is supported for streaming transcription
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
uuid_ibm_transcribe <uuid> stop
```
Stop transcription on the channel.

### Channel Variables

| variable | Description |
| --- | ----------- |
| IBM_ACCESS_TOKEN | IBM access token used to authenticate |
| IBM_SPEECH_INSTANCE_ID |IBM instance id |
| IBM_SPEECH_MODEL | IBM speech model (https://cloud.ibm.com/docs/speech-to-text?topic=speech-to-text-websockets) |
| IBM_SPEECH_LANGUAGE_CUSTOMIZATION_ID |IBM speech language customization id |
| IBM_SPEECH_ACOUSTIC_CUSTOMIZATION_ID | IBM accoustic customization id|
| IBM_SPEECH_BASE_MODEL_VERSION | IBM base model version |
| IBM_SPEECH_WATSON_METADATA | customer metadata to pass to IBM watson |
| IBM_SPEECH_WATSON_LEARNING_OPT_OUT | 1 means opt out |


### Events
`ibm_transcribe::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
```json
{
	"result_index": 0,
	"results": [{
		"final": true,
		"alternatives": [{
			"transcript": "what kind of dog is that",
			"confidence": 0.83
		}]
	}]
}
```

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('uuid_ibm_transcribe', `${ep.uuid} start en-US interim`);  
```

