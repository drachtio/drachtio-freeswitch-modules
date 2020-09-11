const Srf = require('drachtio-srf');
const srf = new Srf();
const Mrf = require('drachtio-fsmrf');
const mrf = new Mrf(srf);
const config = require('config');
const text = 'Hi there!  Let\'s play a game of telephone: you say something and I will repeat back what I heard.  Go ahead..';

srf.connect(config.get('drachtio'))
  .on('connect', (err, hp) => console.log(`connected to sip on ${hp}`))
  .on('error', (err) => console.log(err, 'Error connecting'));

mrf.connect(config.get('freeswitch'))
  .then((ms) => run(ms));

function run(ms) {
  srf.invite((req, res) => {
    ms.connectCaller(req, res)
      .then(({endpoint, dialog}) => {
        dialog.on('destroy', () => endpoint.destroy());
        endpoint.addCustomEventListener('aws_transcribe::transcription', onTranscription.bind(null, endpoint));
        doTts(dialog, endpoint);
      })
      .catch((err) => {
        console.log(err, 'Error connecting call to freeswitch');
      });
  });
}

async function doTts(dlg, ep) {
  try {
    await ep.play('silence_stream://1000');
    await ep.speak({
      ttsEngine: 'google_tts',
      voice: 'en-GB-Wavenet-A',
      text
    });
    ep.api('aws_transcribe', [ep.uuid, 'start', 'en-US', 'interim']);  
  }
  catch (err) {
    console.log(err, 'Error starting transcription');
  }
}

function onTranscription(ep, evt) {
  console.log(`received transcription: ${JSON.stringify(evt)}`);
  if (evt[0].is_final) {
    const text = `OK, I heard you say: ${evt[0].alternatives[0].transcript}`;
    ep.speak({
      ttsEngine: 'google_tts',
      voice: 'en-GB-Wavenet-A',
      text
    });  
  }
}
