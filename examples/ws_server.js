const WebSocket = require('ws');
const fs = require('fs');
const argv = require('minimist')(process.argv.slice(2));
const recordingPath = argv._.length ? argv._[0] : '/tmp/audio.raw';
const port = argv.port && parseInt(argv.port) ? parseInt(argv.port) : 3001
let wstream;

console.log(`listening on port ${port}, writing incoming raw audio to file ${recordingPath}`);

const wss = new WebSocket.Server({ 
  port,
  handleProtocols: (protocols, req) => {
    return 'audiostream.drachtio.org';
  }
});
 
wss.on('connection', (ws, req) => {
  console.log(`received connection from ${req.connection.remoteAddress}`);
  wstream = fs.createWriteStream(recordingPath);

  ws.on('message',  (message) => {
    if (typeof message === 'string') {
      console.log(`received message: ${message}`);
    }
    else if (message instanceof Buffer) {
      wstream.write(message);
    }
  });

  ws.on('close', (code, reason) => {
    console.log(`socket closed ${code}:${reason}`);
    wstream.end();
  });
});
