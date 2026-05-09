<p align="center">
  <img src="assets/logo.png" alt="ps4-mqtt-plugin" width="480">
</p>

# ps4-mqtt-plugin

GoldHEN plugin for jailbroken PlayStation 4 (FW 11.00 target) that
publishes console telemetry to an MQTT broker for Home Assistant.

Sensors auto-create via Home Assistant MQTT Discovery: console state,
current game, CPU/SoC temperature, fan speed, memory usage, network
(IP/SSID/RSSI), storage, uptime, firmware.

## Requirements

- PlayStation 4, jailbroken, GoldHEN 2.4b18.3 or later
- Mosquitto (or compatible) MQTT broker reachable from the PS4
- Home Assistant with the MQTT integration enabled
- Docker (for building the PRX) — only when building from source

## Build

### Host tests (no PS4 needed)

```bash
make test
```

### Integration tests (needs Mosquitto installed locally)

```bash
brew install mosquitto    # or apt install mosquitto on Linux
make integration
```

### PS4 PRX (uses Docker — works on macOS arm64/x86_64 and Linux)

```bash
scripts/build-prx.sh
# produces build/ps4-mqtt.prx
```

The script builds a Docker image with the OpenOrbis PS4 Toolchain on
first run (~5 min, ~1.5 GB), then runs `make prx` inside it.

### PS4 PRX (native, if OpenOrbis SDK installed natively)

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
make prx
```

## Install on PS4

1. Copy `build/ps4-mqtt.prx` to `/data/GoldHEN/plugins/ps4-mqtt/`
2. Copy `config.json` to `/data/GoldHEN/plugins/ps4-mqtt/` (use
   `config.example.json` as a template)
3. Add the plugin path to `/data/GoldHEN/plugins.ini`:

   ```ini
   ps4-mqtt=/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
   ```

4. Reboot the PS4

The PS4 GoldHEN FTP server runs on port 2121 — copy files via FTP
(FileZilla, or `curl -T file ftp://PS4_IP:2121/...`).

## Configure

`config.json` fields (see `config.example.json`):

| Field               | Required | Default | Notes |
|---------------------|----------|---------|-------|
| `broker_host`       | yes      | —       | IP or hostname of MQTT broker |
| `broker_port`       | no       | 1883    | TCP port |
| `username`          | yes      | —       | Broker auth |
| `password`          | yes      | —       | Broker auth |
| `device_name`       | no       | `PS4`   | Shown in Home Assistant |
| `poll_interval_sec` | no       | 10      | Seconds between sensor publishes |

Example:

```json
{
  "broker_host": "192.168.1.10",
  "broker_port": 1883,
  "username": "ps4",
  "password": "secret",
  "device_name": "Sala",
  "poll_interval_sec": 10
}
```

## Verify

After PS4 reboot:

1. Watch GoldHEN klog: `nc PS4_IP 9998` — expect lines prefixed
   `[ps4-mqtt][INFO]` within 10 s of boot
2. In Home Assistant: Settings → Devices & services → MQTT — a device
   named `PS4 <device_name>` should appear with all sensors

### Smoke test checks

| Check | Expected |
|-------|----------|
| Wait 10 s | All sensors update at least once |
| Open a game | `game/title` and `game/title_id` change |
| Return to home menu | `game/title` becomes empty |
| Stop the broker | HA marks device offline within ~30 s (LWT) |
| Start the broker | HA back online within `poll_interval_sec` |
| Power off PS4 | HA marks offline (LWT fires when broker drops dead client) |

## Architecture

- C99, single thread polling collectors and publishing
- Custom minimal MQTT 3.1.1 publisher (CONNECT, PUBLISH, PINGREQ,
  DISCONNECT only — no SUBSCRIBE, no QoS 2, no TLS)
- Last Will and Testament keeps Home Assistant availability accurate
- Discovery payloads published once on connect (retained)
- 5 collectors: system, thermal, network, storage, app

See `docs/superpowers/specs/2026-05-07-ps4-mqtt-plugin-design.md` for
the full design spec and
`docs/superpowers/plans/2026-05-07-ps4-mqtt-plugin.md` for the
implementation plan.

## Limitations (MVP)

- No TLS/MQTTS — broker should be on the local trusted network
- CPU% and FPS are not collected (out of scope)
- Wall power consumption cannot be measured (no API)
- Plugin only publishes; SUBSCRIBE is not implemented
- PS4-side functionality unverified on hardware (Phase 2 code is built
  but smoke test on console is pending)

## License

TBD
