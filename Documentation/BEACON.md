# GPS Beacon Mode

RNode devices with GPS hardware (e.g. Heltec V4) can autonomously transmit GPS position beacons over LoRa when no host computer is connected. This is useful for vehicle tracking, field asset monitoring, or any scenario where the RNode operates standalone on battery power.

## How it works

When no KISS host activity is detected for 15 seconds, the RNode enters beacon mode and transmits a GPS position every 30 seconds. When a host reconnects (e.g. laptop running `rnsd`), beaconing stops automatically and normal RNode operation resumes.

Beacon packets are standard Reticulum packets containing a JSON payload:

```json
{"lat":51.507400,"lon":-0.127800,"alt":15.0,"sat":8,"spd":0.5,"hdop":1.2,"bat":87,"fix":true}
```

## Beacon modes

### Plaintext (default)

Out of the box, beacons are sent as RNS PLAIN packets to the well-known destination `rnlog.beacon` (hash `18bcd8a3dea16ef6765c6b27d008d220`). Any Reticulum node running the `rnlog` collector can receive them.

**Pros**: Works with zero configuration.
**Cons**: Anyone in LoRa range can read the GPS data.

### Encrypted (recommended)

When provisioned with a collector's public key, beacons are sent as RNS SINGLE packets encrypted with X25519 ECDH + AES-256-CBC. Only the collector with the matching private key can decrypt the position data.

Each beacon uses a fresh ephemeral key — there are no shared secrets or session state to manage.

**Pros**: Position data is only readable by your collector.
**Cons**: Requires one-time provisioning (see below).

## Provisioning encrypted beacons

### Prerequisites

- A Heltec V4 (or other GPS-equipped RNode) flashed with beacon-capable firmware
- The `rnlog` collector package installed (`pip install .` from the rns-collector repo)
- A USB connection to the RNode

### Step 1: Get the collector key

Run on the machine where your collector runs (rnsd not required for this step):

```sh
rnlog provision
```

This prints a 64-byte hex string — the collector's X25519 public key (32B), identity hash (16B), and destination hash (16B).

### Step 2: Send the key to the RNode

The key is sent via KISS command `CMD_BCN_KEY` (0x86). Connect the RNode via USB and run:

```python
import serial, time

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
CMD_BCN_KEY = 0x86

# Paste the "Combined (64B)" hex from rnlog provision
key_hex = "PASTE_YOUR_64_BYTE_HEX_HERE"
key_bytes = bytes.fromhex(key_hex)

# Build KISS frame
frame = bytearray([FEND, CMD_BCN_KEY])
for b in key_bytes:
    if b == FEND:    frame.extend([FESC, TFEND])
    elif b == FESC:  frame.extend([FESC, TFESC])
    else:            frame.append(b)
frame.append(FEND)

# Send to device
port = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
time.sleep(0.5)
port.write(frame)
port.flush()
time.sleep(1)

# Check for CMD_READY (0x0F) acknowledgement
resp = port.read(port.in_waiting or 64)
if 0x0F in resp:
    print("Provisioned successfully")
else:
    print(f"Unexpected response: {resp.hex()}")
port.close()
```

The key is stored in EEPROM and persists across power cycles. You only need to do this once per RNode.

### Step 3: Start the collector

```sh
rnlog serve
```

The collector automatically handles both encrypted (SINGLE) and plaintext (PLAIN) beacons. Encrypted beacons show as "encrypted beacon received" in the log.

## Radio parameters

Beacons use fixed LoRa parameters that must match the collector's RNode interface:

| Parameter       | Value     |
|-----------------|-----------|
| Frequency       | 868 MHz   |
| Bandwidth       | 125 kHz   |
| Spreading Factor| 7         |
| Coding Rate     | 4/5       |
| TX Power        | 17 dBm    |

These are set in `Beacon.h`. If your collector uses different radio parameters, update the `BEACON_*` defines and rebuild.

## Timing

| Parameter                | Default | Define                      |
|--------------------------|---------|-----------------------------|
| Beacon interval          | 30s     | `BEACON_INTERVAL_MS`        |
| Startup delay            | 10s     | `BEACON_STARTUP_DELAY_MS`   |
| Host inactivity timeout  | 15s     | `BEACON_NO_HOST_TIMEOUT_MS` |

## Packet sizes

| Mode       | Header | Payload           | Total  |
|------------|--------|-------------------|--------|
| Plaintext  | 19B    | ~93B JSON         | ~112B  |
| Encrypted  | 19B    | 32+16+96+32 = 176B| ~195B  |

Both fit within the RNS MTU of 508 bytes.

## Clearing provisioning

To revert to plaintext beacons, clear the beacon config byte in EEPROM. This can be done by sending a firmware reset command or by re-flashing the firmware (EEPROM is preserved, but you can write 0x00 to the config flag address).

## Files

| File             | Purpose                                          |
|------------------|--------------------------------------------------|
| `Beacon.h`       | Beacon state machine, JSON construction, TX logic|
| `BeaconCrypto.h` | X25519 ECDH, HKDF, AES-256-CBC, HMAC via mbedTLS|
| `GPS.h`          | GPS parsing (TinyGPS++)                          |
| `ROM.h`          | EEPROM addresses for beacon key storage          |
| `Framing.h`      | KISS command definitions including CMD_BCN_KEY    |
