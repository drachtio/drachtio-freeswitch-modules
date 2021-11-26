# mod_azure_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using the Microsoft streaming transcription API

## API

### Commands
The freeswitch module exposes the following API commands:

```
azure_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid AWS [language code](https://docs.aws.amazon.com/transcribe/latest/dg/what-is-transcribe.html) that is supported for streaming transcription
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
azure_transcribe <uuid> stop
```
Stop transcription on the channel.

### Authentication
The plugin will first look for channel variables, then environment variables.  If neither are found, then the default AWS profile on the server will be used.

The names of the channel variables and environment variables are:

| variable | Description |
| --- | ----------- |
| AZURE_SUBSCRIPTION_KEY | The Azure subscription key |
| AZURE_REGION | The Azure region |

### Channel variables
The following channel variables can be set to configure the Azure speech to text service

| variable | Description | Default |
| --- | ----------- |  ---|
| AZURE_PROFANITY_OPTION | "masked", "removed", "raw" | raw|
| AZURE_REQUEST_SNR | if set to 1 or true, enables signal to noise ratio reporting | off |
| AZURE_INITIAL_SPEECH_TIMEOUT_MS | initial time to wait for speech before returning no match | none |
| AZURE_SPEECH_HINTS | comma-separated list of phrases or words to expect | none |
| AZURE_USE_OUTPUT_FORMAT_DETAILED | if set to true or 1, provide n-best and confidence levels | off |


### Events
`azure_transcribe::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result; if the body contains a property with "RecognitionStatus": "Success" it is a final transcript, otherwise it is an interim transcript.
```json
{
  "Id": "1708f0bffc2d4d66b8347280447e9dde",
  "RecognitionStatus": "Success",
  "DisplayText": "This is a test.",
  "Offset": 14400000,
  "Duration": 12200000
}
```

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('azure_transcribe', `${ep.uuid} start en-US interim`);  
```
