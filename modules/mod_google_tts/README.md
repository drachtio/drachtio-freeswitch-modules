# mod_google_tts

A Freeswitch module that allows Google Text-to-Speech API to be used as a tts provider.

## API

### Commands
This freeswitch module does not add any new commands, per se.  Rather, it integrates into the Freeswitch TTS interface such that it is invoked when an application uses the mod_dptools `speak` command with a tts engine of `google_tts` and a voice equal to the language code associated to one of the [supported Wavenet voices](https://cloud.google.com/text-to-speech/docs/voices)

### Events
None.

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this functionality via the speak method on the 'endpoint' object.
```js
ep.speak({
    ttsEngine: 'google_tts',
    voice: 'en-GB-Wavenet-A',
    text: 'This aggression will not stand'
  });
```
## Examples
[google_tts.js](../../examples/google_tts.js)