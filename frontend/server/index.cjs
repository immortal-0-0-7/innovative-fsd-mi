const express = require('express')

const app = express()
const ESP32 = 'http://10.139.114.244'

// Proxy /api/live → ESP32
app.get('/api/live', async (req, res) => {
  try {
    const r = await fetch(`${ESP32}/api/live`)
    const json = await r.json()
    res.json(json)
  } catch {
    res.status(502).json({ error: 'esp32_unreachable' })
  }
})

// Proxy /api/status → ESP32
app.get('/api/status', async (req, res) => {
  try {
    const r = await fetch(`${ESP32}/api/status`)
    const json = await r.json()
    res.json(json)
  } catch {
    res.status(502).json({ error: 'esp32_unreachable' })
  }
})

// Proxy /api/reset → ESP32
app.post('/api/reset', async (req, res) => {
  try {
    const r = await fetch(`${ESP32}/api/reset`, { method: 'POST' })
    const json = await r.json()
    res.json(json)
  } catch {
    res.status(502).json({ error: 'esp32_unreachable' })
  }
})

// Proxy /api/calibrate → ESP32
app.post('/api/calibrate', async (req, res) => {
  try {
    const r = await fetch(`${ESP32}/api/calibrate`, { method: 'POST' })
    const json = await r.json()
    res.json(json)
  } catch {
    res.status(502).json({ error: 'esp32_unreachable' })
  }
})

app.listen(3001, () => {
  console.log('Backend proxy running on http://localhost:3001')
  console.log(`Proxying to ESP32 at ${ESP32}`)
})
