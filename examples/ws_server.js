const WebSocket = require('ws');
const fs = require('fs');
let wstream;

const wss = new WebSocket.Server({ 
  port: 3001,
  handleProtocols: (protocols, req) => {
    return 'audiostream.drachtio.org';
  }
});
 
wss.on('connection', (ws, req) => {
  const path = '/tmp/audio.raw';
  console.log(`received connection from ${req.connection.remoteAddress}, writing audio to ${path}`);
  wstream = fs.createWriteStream(path);

  ws.on('message',  (message) => {
    if (typeof message === 'string') {
      console.log(`received message: ${JSON.stringify(message)}`);
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
