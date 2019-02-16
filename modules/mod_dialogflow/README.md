# mod_dialogflow

A Freeswitch module that connects a Freeswitch channel to a [dialogflow agent](https://dialogflow.com/docs/getting-started/first-agent) so that an IVR interaction can be driven completely by dialogflow logic.

## API

### Commands
The freeswitch module exposes the following API commands:

```
dialogflow_start <uuid> <project-id> <lang-code> [<timeout-secs>] [<event>]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `project-id` - the identifier of the dialogflow project to execute
- `lang-code` - a valid dialogflow [language tag](https://dialogflow.com/docs/reference/language) to use for speech recognition
- `timeout-secs` - number of seconds to wait for an intent to be returned; default 30 secs. (Note: timeout behavior is currently not implemented)
- `event` - name of an initial event to send to dialogflow; e.g. to trigger an initial prompt

```
dialogflow_stop <uuid> 
```
Stop dialogflow on the channel.

### Events
* `dialogflow::intent` - a dialogflow [intent](https://dialogflow.com/docs/intents) has been detected.
* `dialogflow::transcription` - a transcription has been returned
* `dialogflow::audio_provided` - an audio prompt has been returned from dialogflow.  Dialogflow will return both an audio clip in linear 16 format, as well as the text of the prompt.  The audio clip will be played out to the caller and the prompt text is returned to the application in this event.
* `dialogflow::end_of_utterance` - dialogflow has detected the end of an utterance
* `dialogflow::error` - dialogflow has returned an error
## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('dialogflow_start', `${ep.uuid} my-project-id en-US 30 welcome`); 
```
## Examples
