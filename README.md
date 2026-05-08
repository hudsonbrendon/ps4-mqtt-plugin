# ps4-mqtt-plugin

GoldHEN plugin for PlayStation 4 (jailbroken, FW 11.00+) that publishes
console telemetry to an MQTT broker for Home Assistant integration.

Auto-creates HA entities via MQTT Discovery for: console state, current
game, CPU/SoC temperature, fan speed, memory, network (IP/SSID/RSSI),
storage, uptime, firmware version.

## Status

Pre-alpha. Design phase — see
[`docs/superpowers/specs/2026-05-07-ps4-mqtt-plugin-design.md`](docs/superpowers/specs/2026-05-07-ps4-mqtt-plugin-design.md).

## Requirements

- PlayStation 4 with jailbreak (targets FW 11.00)
- GoldHEN 2.4b18.3 or later
- MQTT broker (e.g. Mosquitto)
- Home Assistant with MQTT integration enabled

## License

TBD
