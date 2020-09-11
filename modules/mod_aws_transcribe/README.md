# mod_aws_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using AWS streaming transcription API

## API

### Commands
The freeswitch module exposes the following API commands:

```
aws_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid AWS [language code](https://docs.aws.amazon.com/transcribe/latest/dg/what-is-transcribe.html) that is supported for streaming transcription
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
aws_transcribe <uuid> stop
```
Stop transcription on the channel.

### Authentication
The plugin will first look for channel variables, then environment variables.  If neither are found, then the default AWS profile on the server will be used.

The names of the channel variables and environment variables are:

| variable | Description |
| --- | ----------- |
| AWS_ACCESS_KEY_ID | The Aws access key ID |
| AWS_SECRET_ACCESS_KEY | The Aws secret access key |
| AWS_REGION | The Aws region |


### Events
`aws_transcribe::transcription` - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
```js
[
  {
    "is_final": true,
    "alternatives": [{
      "transcript": "Hello. Can you hear me?"
    }]
  }
]
```

## Usage
When using [drachtio-fsrmf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.
```js
ep.api('aws_transcribe', `${ep.uuid} start en-US interim`);  
```

## Building
You will need to build the AWS C++ SDK.  You can use [this ansible role](https://github.com/davehorton/ansible-role-fsmrf), or refer to the specific steps [here](https://github.com/davehorton/ansible-role-fsmrf/blob/a1947cc24e89dee7d6b42053c53295f9198340c1/tasks/grpc.yml#L28).

## Examples
[aws_transcribe.js](../../examples/aws_transcribe.js)