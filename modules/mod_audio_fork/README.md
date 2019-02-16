# mod_audio_fork

A Freeswitch module that attaches a bug to a media server endpoint and streams audio via websockets to a remote server, using the [websocket sub-protocol](https://tools.ietf.org/html/rfc6455#section-1.9) `audiostream.drachtio.org`.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_fork <uuid> start <wss-url> <metadata>
```
Attaches media bug and starts streaming audio stream to the back-end server.
- `uuid` - unique identifier of Freeswitch channel
- `wss-url` - websocket url to connect and stream audio to
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

To run this app, you can run [the simple websocket server provided](../../examples/ws_server.js) in a separate terminal.  It will listen on port 3001.  

So in the first terminal window run:
```
node ws_server.js
```
And in the second window run:
```
node audio_fork.js http://localhost:3001
```
The app uses text-to-speech to play prompts, so you will need mod_audio_tts loaded as well, and configured to use your GCS cloud credentials to access Google Cloud Text-to-Speech.


