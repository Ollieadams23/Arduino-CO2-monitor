const express = require('express');
const bodyParser = require('body-parser');
const path = require('path');
const app = express();
const PORT = 5000;

let sensorData = { co2: null, tvoc: null };
let fanState = 'off';

app.use(bodyParser.json());

// Serve the dashboard
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'dashboard.html'));
});

// Endpoint to receive sensor data
app.post('/sensor', (req, res) => {
  sensorData.co2 = req.body.co2;
  sensorData.tvoc = req.body.tvoc;
  res.json({ status: 'ok' });
});

// Endpoint to get latest data (AJAX polling)
app.get('/data', (req, res) => {
  res.json(sensorData);
});

// Fan control endpoints
app.post('/fan/on', (req, res) => {
  fanState = 'on';
  res.json({ fan: 'on' });
});

app.post('/fan/off', (req, res) => {
  fanState = 'off';
  res.json({ fan: 'off' });
});

// Endpoint for Arduino to check fan state
app.get('/fan/state', (req, res) => {
  res.json({ fan: fanState });
});

app.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});
