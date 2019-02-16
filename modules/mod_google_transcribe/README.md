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
## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US`);  
```
## Examples
[google_transcribe.js](../../examples/google_transcribe.js)