/*
  =====================================================================
  HTPT - Web Monitor Server
  ---------------------------------------------------------------------
  Vai trò:
    - Connect tới Mosquitto qua MQTT (port 1883)
    - Serve trang web tĩnh ở public/
    - Forward MQTT messages → browser qua Socket.IO
    - Nhận lệnh control từ browser → publish ra MQTT broker
  ---------------------------------------------------------------------
  Chạy:
    npm install
    npm start           # mặc định http://localhost:3000
    PORT=8080 npm start # đổi port
    MQTT_URL=mqtt://192.168.1.50:1883 npm start  # nếu broker máy khác
  =====================================================================
*/

const express = require('express');
const http    = require('http');
const path    = require('path');
const { Server } = require('socket.io');
const mqtt    = require('mqtt');

// ----- Config (sửa qua biến môi trường) -----
const PORT      = parseInt(process.env.PORT || '8005', 10);
const MQTT_URL  = process.env.MQTT_URL  || 'mqtt://localhost:1883';
const MQTT_USER = process.env.MQTT_USER || 'webserver';
const MQTT_PASS = process.env.MQTT_PASS || 'web_pass_2026';

// Các topic cần forward về browser
const SUB_TOPICS = [
  'sensor/data',
  'sensor/ack',
  'monitor/sent',
  'monitor/retry',
  'monitor/failed',
  'monitor/received',
  'monitor/processed',
  'monitor/stats/A',
  'monitor/stats/B'
];

// Whitelist topic mà browser được phép publish (an toàn cho demo)
const PUBLISHABLE_TOPICS = new Set([
  'control/drop_rate',
  'control/ack_drop_rate',
  'control/threshold'
]);

// ----- Setup Express + Socket.IO -----
const app    = express();
const server = http.createServer(app);
const io     = new Server(server);

app.use(express.static(path.join(__dirname, 'public')));

// ----- MQTT client -----
console.log(`[MQTT] connecting to ${MQTT_URL} as ${MQTT_USER} ...`);
const mqttClient = mqtt.connect(MQTT_URL, {
  username: MQTT_USER,
  password: MQTT_PASS,
  reconnectPeriod: 2000,
  clientId: 'htpt-monitor-server-' + Math.random().toString(16).slice(2, 8)
});

mqttClient.on('connect', () => {
  console.log(`[MQTT] connected`);
  SUB_TOPICS.forEach(t => {
    mqttClient.subscribe(t, { qos: 0 }, err => {
      if (err) console.error(`[MQTT] subscribe ${t} failed:`, err.message);
    });
  });
  io.emit('mqtt-status', { connected: true, url: MQTT_URL });
});

mqttClient.on('reconnect', () => {
  console.log(`[MQTT] reconnecting...`);
  io.emit('mqtt-status', { connected: false, url: MQTT_URL });
});

mqttClient.on('close', () => {
  console.log(`[MQTT] connection closed`);
  io.emit('mqtt-status', { connected: false, url: MQTT_URL });
});

mqttClient.on('error', err => {
  console.error(`[MQTT] error:`, err.message);
});

mqttClient.on('message', (topic, payload) => {
  let body;
  const raw = payload.toString();
  try { body = JSON.parse(raw); }
  catch { body = raw; }
  io.emit('mqtt-message', { topic, payload: body, ts: Date.now() });
});

// ----- Socket.IO handlers -----
io.on('connection', socket => {
  console.log(`[SOCKET] client connected: ${socket.id} (total=${io.engine.clientsCount})`);
  socket.emit('mqtt-status', { connected: mqttClient.connected, url: MQTT_URL });

  socket.on('mqtt-publish', ({ topic, payload }) => {
    if (!PUBLISHABLE_TOPICS.has(topic)) {
      console.warn(`[SOCKET] reject publish to unauthorized topic: ${topic}`);
      return;
    }
    if (!mqttClient.connected) {
      console.warn(`[SOCKET] cannot publish, MQTT disconnected`);
      return;
    }
    mqttClient.publish(topic, String(payload), { retain: true, qos: 0 });
    console.log(`[PUB] ${topic} = ${payload}`);
  });

  socket.on('disconnect', () => {
    console.log(`[SOCKET] client disconnected: ${socket.id} (total=${io.engine.clientsCount})`);
  });
});

// ----- Start server -----
server.listen(PORT, () => {
  console.log(`[WEB] dashboard: http://localhost:${PORT}`);
  console.log(`[WEB] mở trên thiết bị khác cùng LAN qua IP máy này:`);
  const nets = require('os').networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const net of nets[name]) {
      if (net.family === 'IPv4' && !net.internal) {
        console.log(`        http://${net.address}:${PORT}`);
      }
    }
  }
});

// ----- Graceful shutdown -----
function shutdown() {
  console.log('\n[WEB] shutting down...');
  mqttClient.end();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(1), 2000);
}
process.on('SIGINT',  shutdown);
process.on('SIGTERM', shutdown);
