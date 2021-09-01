# mod_dialogflow

A Freeswitch module that connects a Freeswitch channel to a [dialogflow agent](https://dialogflow.com/docs/getting-started/first-agent) so that an IVR interaction can be driven completely by dialogflow logic.

Once a Freeswitch channel is connected to a dialogflow agent, media is streamed to the dialogflow service, which returns information describing the "intent" that was detected, along with transcriptions and audio prompts and text to play to the caller.  The handling of returned audio by the module is two-fold:
1.  If an audio clip was returned, it is *not* immediately played to the caller, but instead is written to a temporary wave file on the Freeswitch server.
2.  Next, a Freeswitch custom event is sent to the application containing the details of the dialogflow response as well as the path to the wave file.

This allows the application whether to decide to play the returned audio clip (via the mod_dptools 'play' command), or to use a text-to-speech service to generate audio using the returned prompt text.

## API

### Commands
The freeswitch module exposes the following API commands:

#### dialogflow_start
```
dialogflow_start <uuid> <project-id> <lang-code> [<event>]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `project-id` - the identifier of the dialogflow project to execute, which may optionally include a dialogflow environment and a region (see below).
- `lang-code` - a valid dialogflow [language tag](https://dialogflow.com/docs/reference/language) to use for speech recognition
- `event` - name of an initial event to send to dialogflow; e.g. to trigger an initial prompt

When executing a dialogflow project, the environment and region will default to 'draft' and 'us', respectively.  

To specify both an environment and a region, provide a value for project-id in the dialogflow_start command as follows:
```
dialogflow-project-id:environment:region, i.e myproject:production:eu-west1
```
To specify environment and default to the global region:
```
dialogflow-project-id:environment, i.e myproject:production
```
To specify a region and default environment:
```
dialogflow-project-id::region, i.e myproject::eu-west1
```
To simply use the defaults for both environment and region:
```
dialogflow-project-id, i.e myproject
```

#### dialogflow_stop
```
dialogflow_stop <uuid> 
```
Stops dialogflow on the channel.

### Events
* `dialogflow::intent` - a dialogflow [intent](https://dialogflow.com/docs/intents) has been detected.
* `dialogflow::transcription` - a transcription has been returned
* `dialogflow::audio_provided` - an audio prompt has been returned from dialogflow.  Dialogflow will return both an audio clip in linear 16 format, as well as the text of the prompt.  The audio clip will be played out to the caller and the prompt text is returned to the application in this event.
* `dialogflow::end_of_utterance` - dialogflow has detected the end of an utterance
* `dialogflow::error` - dialogflow has returned an error
## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('dialogflow_start', `${ep.uuid} my-agent-uuxr:production en-US welcome`); 
```
## Examples
[drachtio-dialogflow-phone-gateway](https://github.com/davehorton/drachtio-dialogflow-phone-gateway)
