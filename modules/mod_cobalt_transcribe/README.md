# mod_cobalt_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using the [streaming transcription API](https://docs-v2.cobaltspeech.com/docs/asr/) from [Cobalt Speech](https://www.cobaltspeech.com/).  Cobalt Speech provides a speech recognition product that can be run on-prem on a Linux server.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_cobalt_get_version <uuid> <hostport>
```
Returns version information about the Cobalt server listening at the specified ip address and port

```
uuid_cobalt_list_models <uuid> <hostport> 
```
Lists the available models for a Cobalt speech server

```
uuid_cobalt_compile_context <uuid> <hostport> <model> <token> <phrases>
```
Compiles a list of hint phrases into a context string that can later be used in a transcribe command.  The context string is returned as a base64-encoded string.  Hints must be compiled within the context of a single model, thus it is required to provide the model name.  Hints must also be associated with a "token"; the default token that you may generally use is "unk:default".  See [here](https://docs-v2.cobaltspeech.com/docs/asr/transcribe/recognition_context/) for more details.

```
uuid_cobalt_transcribe <uuid> hostport start model [interim|full] [stereo|mono] [bug-name]
```
Attaches media bug to channel and performs streaming recognize request.

```
uuid_cobalt_transcribe <uuid> hostport stop model
```
Stop transcription on a channel.


### Channel Variables

| variable | Description |
| --- | ----------- |
| COBALT_ENABLE_CONFUSION_NETWORK | if true, enable [confusion network](https://docs-v2.cobaltspeech.com/docs/asr/transcribe/#confusion-network) |
| COBALT_METADATA | custom metadata to send with a transcribe request  |
| COBALT_COMPILED_CONTEXT_DATA | base64-encoded compiled context hints to include with the transcribe request |


### Events
`cobalt_speech::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result.

`cobalt_speech::version_response` - returns the response to a `uuid_cobalt_get_version` request. The event contains a JSON body describing the version.

`cobalt_speech::model_list_response` - returns the response to a `uuid_cobalt_list_models` request. The event contains a JSON body describing the available models.

`cobalt_speech::compile_context_response` - returns the response to a uuid_cobalt_compile_context request. The event contains a JSON body containing the base64-encoded context.

