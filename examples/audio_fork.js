const Srf = require('drachtio-srf');
const srf = new Srf();
const Mrf = require('drachtio-fsmrf');
const mrf = new Mrf(srf);
const argv = require('minimist')(process.argv.slice(2));
const wsUrl = argv._[0];
const config = require('config');
const text = 'Hi there.  Please go ahead and make a recording and then hangup';
const EVENT_TRANSCRIPT = 'mod_audio_fork::transcription';
const EVENT_TRANSFER = 'mod_audio_fork::transfer';
const EVENT_PLAY_AUDIO = 'mod_audio_fork::play_audio';
const EVENT_KILL_AUDIO = 'mod_audio_fork::kill_audio';
const EVENT_DISCONNECT = 'mod_audio_fork::disconnect';
const EVENT_CONNECT = 'mod_audio_fork::connect';
const EVENT_CONNECT_FAILED = 'mod_audio_fork::connect_failed';
const EVENT_MAINTENANCE = 'mod_audio_fork::maintenance';
const EVENT_ERROR = 'mod_audio_fork::error';

if (!wsUrl) throw new Error('must specify ws server to connect to');
console.log(`We will be streaming audio to websocket server at ${wsUrl}`);

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
        doFork(req, dialog, endpoint);  
      })
      .catch((err) => {
        console.log(err, 'Error connecting call to freeswitch');
      });
  });
}

async function doFork(req, dlg, ep) {
  const metadata = {
    callId: req.get('Call-Id'),
    to: req.getParsedHeader('To').uri,
    from: req.getParsedHeader('From').uri,
  }
  ep.addCustomEventListener(EVENT_CONNECT, onConnect);
  ep.addCustomEventListener(EVENT_CONNECT_FAILED, onConnectFailed);
  ep.addCustomEventListener(EVENT_DISCONNECT, onDisconnect);
  ep.addCustomEventListener(EVENT_ERROR, onError);
  ep.addCustomEventListener(EVENT_MAINTENANCE, onMaintenance);
  ep.on('dtmf', (evt) => ep.forkAudioSendText(evt));
  await ep.play('silence_stream://1000');
  await ep.speak({
    ttsEngine: 'google_tts',
    voice: 'en-GB-Wavenet-A',
    text
  });
  ep.api('uuid_audio_fork', `${ep.uuid} start ${wsUrl} mono 16k ${JSON.stringify(metadata)}`);
}

function onConnect(evt) {
  console.log('successfully connected');
}
function onConnectFailed(evt) {
  console.log('connection failed');
}
function onDisconnect(evt) {
  console.log('far end dropped connection');
}
function onError(evt) {
  console.log(`got error ${JSON.stringify(evt)}`);
}
function onMaintenance(evt) {
  console.log(`got event ${JSON.stringify(evt)}`);
}