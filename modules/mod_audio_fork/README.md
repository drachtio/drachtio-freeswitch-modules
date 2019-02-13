# mod_drachtio_fork

A Freeswitch module that attaches a bug to a media server endpoint and streams audio via websockets to a remote server.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_fork <uuid> start <wss-url> <metadata>
```
Attaches media bug and starts streaming audio stream to the back-end server.
- `uuid` - unique identifier of Freeswitch channel
- `wss-url` - url to connect and stream audio to
- `metadata` - JSON metadata to send to the back-end server after initial connection

```
uuid_audio_fork <uuid> stop
```
Closes websocket connection and detaches media bug.
