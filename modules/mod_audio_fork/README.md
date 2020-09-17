# mod_audio_fork

A Freeswitch module that attaches a bug to a media server endpoint and streams L16 audio via websockets to a remote server.  This module also supports receiving media from the server to play back to the caller, enabling the creation of full-fledged IVR or dialog-type applications.

#### Environment variables
- MOD_AUDIO_FORK_SUBPROTOCOL_NAME - optional, name of the [websocket sub-protocol](https://tools.ietf.org/html/rfc6455#section-1.9) to advertise; defaults to "audio.drachtio.org"
- MOD_AUDIO_FORK_SERVICE_THREADS - optional, number of libwebsocket service threads to create; these threads handling sending all messages for all sessions.  Defaults to 1, but can be set to as many as 5.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_fork <uuid> start <wss-url> <mix-type> <sampling-rate> <metadata>
```
Attaches media bug and starts streaming audio stream to the back-end server.  Audio is streamed in linear 16 format (16-bit PCM encoding) with either one or two channels depending on the mix-type requested.
- `uuid` - unique identifier of Freeswitch channel
- `wss-url` - websocket url to connect and stream audio to
- `mix-type` - choice of 
  - "mono" - single channel containing caller's audio
  - "mixed" - single channel containing both caller and callee audio
  - "stereo" - two channels with caller audio in one and callee audio in the other.
- `sampling-rate` - choice of
  - "8k" = 8000 Hz sample rate will be generated
  - "16k" = 16000 Hz sample rate will be generated
- `metadata` - a text frame of arbitrary data to send to the back-end server immediately upon connecting.  Once this text frame has been sent, the incoming audio will be sent in binary frames to the server.

```
uuid_audio_fork <uuid> send_text <metadata>
```
Send a text frame of arbitrary data to the remote server (e.g. this can be used to notify of DTMF events).

```
uuid_audio_fork <uuid> stop <metadata>
```
Closes websocket connection and detaches media bug, optionally sending a final text frame over the websocket connection before closing.

### Events
An optional feature of this module is that it can receive JSON text frames from the server and generate associated events to an application.  The format of the JSON text frames and the associated events are described below.

#### audio
##### server JSON message
The server can provide audio content to be played back to the caller by sending a JSON text frame like this:
```json
{
	"type": "playAudio",
	"data": {
		"audioContentType": "raw",
		"sampleRate": 8000,
		"audioContent": "base64 encoded raw audio..",
		"textContent": "Hi there!  How can we help?"
	}
}
```
The `audioContentType` value can be either `wave` or `raw`.  If the latter, then `sampleRate` must be specified.  The audio content itself is supplied as a base64 encoded string.  The `textContent` attribute can optionally contain the text of the prompt.  This allows an application to choose whether to play the raw audio or to use its own text-to-speech to play the text prompt.

Note that the module does _not_ directly play out the raw audio.  Instead, it writes it to a temporary file and provides the path to the file in the event generated.  It is left to the application to play out this file if it wishes to do so.
##### Freeswitch event generated
**Name**: mod_audio_fork::play_audio
**Body**: JSON string
```
{
  "audioContentType": "raw",
  "sampleRate": 8000,
  "textContent": "Hi there!  How can we help?",
  "file": "/tmp/7dd5e34e-5db4-4edb-a166-757e5d29b941_2.tmp.r8"
}
```
Note the audioContent attribute has been replaced with the path to the file containing the audio.  This temporary file will be removed when the Freeswitch session ends.
#### killAudio
##### server JSON message
The server can provide a request to kill the current audio playback:
```json
{
	"type": "killAudio",
}
```
Any current audio being played to the caller will be immediately stopped.  The event sent to the application is for information purposes only.

##### Freeswitch event generated
**Name**: mod_audio_fork::kill_audio
**Body**: JSON string - the data attribute from the server message


#### transcription
##### server JSON message
The server can optionally provide transcriptions to the application in real-time:
```json
{
	"type": "transcription",
	"data": {
    
	}
}
```
The transcription data can be any JSON object; for instance, a server may choose to return a transcript and an associated confidence level.  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::transcription
**Body**: JSON string - the data attribute from the server message

#### transfer
##### server JSON message
The server can optionally provide a request to transfer the call:
```json
{
	"type": "transfer",
	"data": {
    
	}
}
```
The transfer data can be any JSON object and is left for the application to determine how to handle it and accomplish the call transfer.  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::transfer
**Body**: JSON string - the data attribute from the server message

#### disconnect
##### server JSON message
The server can optionally request to disconnect the caller:
```json
{
	"type": "disconnect"
}
```
Note that the module _does not_ close the Freeswitch channel when a disconnect request is received.  It is left for the application to determine whether to tear down the call.

##### Freeswitch event generated
**Name**: mod_audio_fork::disconnect
**Body**: none

#### error
##### server JSON message
The server can optionally report an error of some kind.  
```json
{
	"type": "error",
	"data": {
    
	}
}
```
The error data can be any JSON object and is left for the application to the application to determine what, if any, action should be taken in response to an error..  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::error
**Body**: JSON string - the data attribute from the server message

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
const url = 'https://70f21a76.ngrok.io';
const callerData = {to: '6173333456', from: '2061236666', callid: req.get('Call-Id')};
ep.api('uuid_audio_fork', `${ep.uuid} start ${url} mono 8k ${JSON.stringify(callerData)}`);
```
or, from version 1.4.1 on, by using the Endpoint convenience methods:
```js
await ep.forkAudioStart({
  wsUrl,
  mixType: 'stereo',
  sampling: '16k',
  metadata
});
..
ep.forkAudioSendText(moremetadata);
..
ep.forkAudioStop(evenmoremetadata);
```
Each of the methods above returns a promise that resolves when the api command has been executed, or throws an error.
## Examples
[audio_fork.js](../../examples/audio_fork.js) provides an example of an application that connects an incoming call to Freeswitch and then forks the audio to a remote websocket server.

To run this app, you can run [the simple websocket server provided](../../examples/ws_server.js) in a separate terminal.  It will listen on port 3001 and will simply write the incoming raw audio to `/tmp/audio.raw` in linear16 format with no header or file container.

So in the first terminal window run:
```
node ws_server.js
```
And in the second window run:
```
node audio_fork.js http://localhost:3001
```
The app uses text-to-speech to play prompts, so you will need mod_google_tts loaded as well, and configured to use your GCS cloud credentials to access Google Cloud Text-to-Speech.  (If you don't want to run mod_google_tts you can of course simply modify the application remove the prompt, just be aware that you will hear silence when you connect, and should simply begin speaking after the call connects).


