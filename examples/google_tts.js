const Srf = require('drachtio-srf');
const srf = new Srf();
const Mrf = require('drachtio-fsmrf');
const mrf = new Mrf(srf);
const config = require('config');
const text = 'Hi there!  This is an admittedly quite simple example of using google text to speech.  Try it out for yourself some time, and do raise issues on github if you have any problems or suggestions.  Ta for now!';

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
        doTts(dialog, endpoint);  
      })
      .catch((err) => {
        console.log(err, 'Error connecting call to freeswitch');
      });
  });
}

async function doTts(dlg, ep) {
  await ep.play('silence_stream://1000');
  await ep.speak({
    ttsEngine: 'google_tts',
    voice: 'en-GB-Wavenet-A',
    text
  });
  dlg.destroy();
  ep.destroy();
}
