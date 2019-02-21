# mod_audio_fork

A Freeswitch module that attaches a bug to a media server endpoint and streams audio via websockets to a remote server, using the [websocket sub-protocol](https://tools.ietf.org/html/rfc6455#section-1.9) `audiostream.drachtio.org`.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_fork <uuid> start <wss-url> <mix-type> <metadata>
```
Attaches media bug and starts streaming audio stream to the back-end server.  Audio is streamed in linear 16 format (signed 16-bit PCM encoding, 16khz sampling) with either one or two channels depending on the mix-type requested.
- `uuid` - unique identifier of Freeswitch channel
- `wss-url` - websocket url to connect and stream audio to
- `mix-type` - choice of 
  - "mono" - single channel containing caller's audio
  - "mixed" - single channel containing both caller and callee audio
  - "stereo" - two channels with caller audio in one and callee audio in the other.
- `metadata` - JSON metadata to send to the back-end server after initial connection

```
uuid_audio_fork <uuid> stop
```
Closes websocket connection and detaches media bug.

### Events
None.

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
const url = 'https://70f21a76.ngrok.io';
const callerData = {to: '6173333456', from: '2061236666', callid: req.get('Call-Id')};
ep.api('uuid_audio_fork', `${ep.uuid} start ${url} ${JSON.stringify(callerData)}`);
```
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


