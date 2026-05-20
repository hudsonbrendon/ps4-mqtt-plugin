<p align="center">
  <img src="assets/logo.png" alt="ps4-mqtt-plugin" width="480">
</p>

# ps4-mqtt-plugin

GoldHEN plugin for jailbroken PlayStation 4 (FW 11.00 target) that
publishes console telemetry to an MQTT broker. Home Assistant
auto-creates the sensors via MQTT Discovery.

The plugin loads inside the game process at launch, starts a
background worker thread, and keeps publishing to the broker for
the entire game session.

## Requirements

- PlayStation 4, jailbroken, GoldHEN 2.4b18.3 or later
- Mosquitto (or compatible) MQTT broker reachable from the PS4
- Home Assistant with the MQTT integration enabled
- Docker (for building the PRX from source)

## Build

```bash
scripts/build-prx.sh
# produces build/ps4-mqtt.prx
```

First run downloads the OpenOrbis PS4 toolchain into a Docker image
(~5 min, ~1.5 GB). Subsequent builds are fast.

## Install on PS4

1. **Enable the Plugins Loader in GoldHEN** (one-time, in the PS4 UI):
   - XMB → GoldHEN icon (top-left)
   - Plugin Settings
   - Check **Enable Plugins Loader**
   - Check **Enable Game Patch Plugin** (optional)

2. **Copy files via FTP** (GoldHEN FTP server runs on port 2121):

   ```bash
   curl -T build/ps4-mqtt.prx \
     ftp://PS4_IP:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
   curl -T config.json \
     ftp://PS4_IP:2121/data/GoldHEN/plugins/ps4-mqtt/config.json
   ```

3. **Register the plugin** in `/data/GoldHEN/plugins.ini`. The
   `[default]` section makes the plugin load for every game:

   ```ini
   [default]
   /data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
   ```

   Upload the file:

   ```bash
   curl -T plugins.ini ftp://PS4_IP:2121/data/GoldHEN/plugins.ini
   ```

4. **Launch any game**. GoldHEN injects the plugin at game launch;
   the plugin connects to MQTT and starts publishing within a few
   seconds. The plugin keeps running for the whole game session.

Note: plugins only load on **game launch**, not at boot. The plugin
goes silent when you return to XMB.

## Configure

`config.json` (use `config.example.json` as template) — currently
broker connection is **hardcoded** in `src/main.c`. Loading from
`config.json` at runtime is not yet wired (the JSON read path uses
syscalls that crash inside the game sandbox; the loader still needs
work).

To change broker for now: edit the `#define BROKER_*` block in
`src/main.c` and rebuild.

## Available sensors

All sensors appear under a single HA device named **PS4**. They
auto-create via MQTT Discovery on each (re)connect.

| Sensor | Topic | Notes |
|---|---|---|
| State | `ps4/ps4/state` | `on` while plugin is alive |
| Availability | `ps4/ps4/availability` | LWT — `online` / `offline` |
| Heartbeat | `ps4/ps4/heartbeat` | Worker tick counter |
| Uptime (raw) | `ps4/ps4/uptime_sec` | System uptime in seconds |
| Uptime (pretty) | `ps4/ps4/uptime` | Formatted `Xh Ym` / `Ym` |
| Firmware | `ps4/ps4/firmware` | Hardcoded `11.00` |
| Game Title ID | `ps4/ps4/game/title_id` | CUSA code, blank in XMB |
| In Game | `ps4/ps4/game/in_game` | `yes` / `no` |
| Controller Connected | `ps4/ps4/controller/connected` | `yes` / `no` |
| System Time | `ps4/ps4/system/time` | ISO 8601, UTC |
| Boot Time | `ps4/ps4/system/boot_time` | ISO 8601, UTC |
| Plugin Uptime (raw) | `ps4/ps4/plugin/uptime_sec` | Since plugin start |
| Plugin Uptime (pretty) | `ps4/ps4/plugin/uptime` | Formatted |
| Plugin Publishes | `ps4/ps4/plugin/publish_count` | Total publishes |
| Plugin Reconnects | `ps4/ps4/plugin/reconnect_count` | MQTT reconnects |

## Not yet supported (roadmap)

These are blocked because the plugin runs inside the game's sandbox
and the corresponding PS4 APIs require elevated privilege the
sandbox doesn't grant — calling them crashes the game
(error `CE-34878-0`). Unlocking them needs the GoldHEN Plugins SDK
with `libGoldHEN_Hook` Detour to intercept the privileged calls the
game itself makes.

- **Controller battery / charging state** — `scePadReadStateExt`,
  `scePadGetExtControllerInformation`, `sceHidControlGetBatteryState`
  all return privilege errors or zeros
- **Network info** (IP, SSID, RSSI, link type) — `sceNetCtlInit`
  crashes
- **Storage** (used/free/total GB) — `statvfs("/user")` crashes
- **Real memory** (used/total MB) — `sysctl(HW_PHYSMEM)` /
  `sysctlbyname("vm.stats.vm.*")` not verified safe
- **Temperatures** (CPU °C, SoC °C) — `sceKernelGetCpuTemperature`,
  `sceKernelGetSocSensorTemperature` privileged
- **Fan RPM** — `sysctlbyname("machdep.fan_speed")`
- **Game title (name, not just CUSA)** — `sceLncUtilGetAppTitleId`
  returns -1 inside the game
- **Per-controller stats** (multiple pads, motion, light bar)

The plan to add them is to integrate the GoldHEN Plugins SDK and
use `HOOK_INIT` / `HOOK_CONTINUE` on the relevant `scePad*` and
`sceHid*` entry points so the plugin sees the data the game itself
reads.

## How it works

- `_init` → `module_start` → `plugin_load` (GoldHEN convention)
- `plugin_load` spawns a POSIX thread and returns immediately so the
  game launch isn't blocked
- Worker thread:
  1. Connects MQTT with a Last Will (`offline` retained)
  2. Publishes HA Discovery configs for every sensor (retained)
  3. Loops: collect → publish → sleep `POLL_INTERVAL`s → ping
  4. On disconnect, reconnects with backoff
- `mqtt_client_disconnect` drains the recv socket after sending
  DISCONNECT, otherwise the broker drops PUBLISH bytes still in the
  kernel buffer
- `log_ps4.c` writes only to `sceKernelDebugOutText` — file writes
  via `fopen` crash inside the game sandbox

PRX format quirks (for anyone porting):
- Build with `-pie -e _init --export-dynamic` (not `-shared`)
- `_sceProcessParam` magic `0x13C13F4BF`, SDK version `0x4508101`
- `module_start` must be exported in the PRX dynsym
- `create-fself --libname=ps4-mqtt --sdkver 72319233`

## Architecture

- C99, single worker thread
- Custom minimal MQTT 3.1.1 publisher (CONNECT, PUBLISH, PINGREQ,
  DISCONNECT only — no SUBSCRIBE, no QoS 2, no TLS)
- LWT keeps Home Assistant availability accurate when the plugin
  dies or the game exits
- Discovery payloads published once on each connect (retained)

## Limitations

- No TLS — broker must be on a trusted local network
- No SUBSCRIBE — plugin only publishes
- Broker credentials hardcoded in `src/main.c` (config.json loader
  not yet sandbox-safe — see roadmap)
- Plugin only loads when a game starts; it doesn't run in XMB
- Firmware string is hardcoded `11.00` (real `kern.osrelease` lookup
  is sandbox-blocked — see roadmap)

## License

TBD
