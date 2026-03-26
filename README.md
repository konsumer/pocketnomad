This is a Reticulum/LXMF client for the CardputerADV.

## development

```sh
# download libs
npm i

# reloading web version
npm start

# build and upload to real cardputer
npm run cardputer
```

On the web, you can inject things it doesn't support (ir/radio/etc) for testing:

```js
import loadFW from './index.mjs'

const m = await loadFW({ canvas: document.getElementById('canvas') })

// LoRa
m.loraOut = bytes => console.log('LoRa sent:', bytes)
m.loraInject(new TextEncoder().encode("Hello from web."))

// GPS
m.satelliteCount = 5
m.gpsSet(51.5074, -0.1278)

// Motion — ax,ay,az (G), gx,gy,gz (deg/s), temp (°C, optional)
m.motionSet(0, 0, 1,  0, 0, 0,  25.0)

// IR is TX-only — hook outgoing transmissions
m.irOut = pkt => console.log('IR sent:', pkt) // { protocol, address, command }

// Power — set simulated battery state (read each frame by c.batteryLevel() etc.)
m.batteryLevel   = 85      // 0–100 %
m.batteryVoltage = 3900    // mV
m.isCharging     = false
```