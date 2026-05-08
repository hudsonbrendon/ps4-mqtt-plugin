# PS4 MQTT Home Assistant Plugin — Design

**Date:** 2026-05-07
**Status:** Approved
**Author:** Hudson Brendon

## Goal

GoldHEN PRX plugin que coleta métricas do PS4 e publica via MQTT no Home Assistant, criando entidades automaticamente via MQTT Discovery.

## Context

- **Hardware:** PlayStation 4 (FW 11.00)
- **CFW:** GoldHEN 2.4b18.3+
- **SDK:** OpenOrbis PS4 Toolchain (C/C++)
- **Home Assistant:** já rodando, com broker MQTT (Mosquitto) configurado e auth user/senha
- **Objetivo:** observabilidade do console em tempo real no HA

## Scope

### MVP — métricas incluídas

| Métrica | Fonte | Tipo |
|---------|-------|------|
| Estado console (on/standby) | Plugin lifecycle | enum |
| Jogo atual (título + ID) | `libLNC` (sceLncUtilGetAppId) | string |
| Temperatura CPU | ICC sensors via sysctl | °C |
| Temperatura SoC | ICC sensors | °C |
| Velocidade ventoinha | ICC | RPM |
| Memória usada / total | sysctl `hw.physmem` + vm stats | MB |
| IP local | libNetCtl | string |
| SSID WiFi | libNetCtl | string |
| RSSI WiFi | libNetCtl | dBm |
| Espaço em disco usado/total | `statfs` | GB |
| Uptime | `clock_gettime(CLOCK_UPTIME)` | seg |
| Versão firmware | sysctl `kern.osrelease` | string |

### Out of scope (fase 2 ou descartado)

- Uso CPU% (precisa hook ou root sysctl — complexo)
- FPS de jogo (precisa hook GNM)
- Consumo energia em watts (sem API; PS4 não expõe wattmeter)

## Non-Goals

- TLS/MQTTS (broker local, rede confiável)
- MQTT SUBSCRIBE (plugin só publica)
- Configuração via UI no PS4 (edita JSON via SSH/FTP)
- Suporte a múltiplos brokers
- QoS 2

## Architecture

```
┌─────────────────────────────────────────┐
│  PS4 (FW 11.00 + GoldHEN 2.4b18.3+)     │
│                                         │
│  ┌───────────────────────────────────┐  │
│  │  ps4-mqtt.prx (plugin)            │  │
│  │  ┌─────────────┐  ┌────────────┐  │  │
│  │  │ Collectors  │→ │ Publisher  │  │  │
│  │  │ (sysctl,    │  │ (MQTT 3.1.1│  │  │
│  │  │  ICC, libNet│  │  custom)   │  │  │
│  │  └─────────────┘  └─────┬──────┘  │  │
│  │         ↑               │         │  │
│  │  ┌─────────────┐        │         │  │
│  │  │ Config JSON │        │         │  │
│  │  └─────────────┘        │         │  │
│  └────────────────────────┼─────────┘  │
└───────────────────────────┼────────────┘
                            │ TCP 1883
                            ▼
                  ┌──────────────────┐
                  │  Mosquitto       │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │  Home Assistant  │
                  │  (auto-discovery)│
                  └──────────────────┘
```

### Fluxo de execução

1. PS4 liga → GoldHEN auto-carrega `ps4-mqtt.prx` de `/data/GoldHEN/plugins/ps4-mqtt/`
2. Plugin lê `config.json`, conecta ao broker MQTT
3. Publica MQTT Discovery configs (HA cria entidades)
4. Publica `availability=online` (retain)
5. Loop: thread principal coleta métricas a cada 10s, publica em `ps4/<slug>/<sensor>`
6. Keepalive PINGREQ a cada 60s
7. Reconnect com backoff exponencial em falha
8. Plugin unload → publica `availability=offline`, DISCONNECT, cleanup

## Components

### File Structure

```
ps4-mqtt-plugin/
├── Makefile                  # OpenOrbis build
├── README.md
├── config.example.json       # Template config
├── src/
│   ├── main.c                # Entry point, plugin lifecycle
│   ├── config.c/.h           # JSON parser
│   ├── mqtt/
│   │   ├── mqtt_client.c/.h  # Connect, publish, ping, disconnect
│   │   ├── mqtt_packet.c/.h  # Encode/decode MQTT 3.1.1 frames
│   │   └── mqtt_socket.c/.h  # libNet wrapper (BSD-style API)
│   ├── collectors/
│   │   ├── system.c/.h       # Uptime, FW, memória
│   │   ├── thermal.c/.h      # CPU/SoC temp, fan speed (ICC)
│   │   ├── network.c/.h      # IP, SSID, sinal WiFi
│   │   ├── storage.c/.h      # Espaço em disco
│   │   └── app.c/.h          # Jogo atual (libLNC)
│   ├── ha_discovery.c/.h     # MQTT discovery payload builder
│   └── log.c/.h              # Klog wrapper
├── third_party/
│   └── cJSON/                # JSON parser (embedded, MIT)
└── tests/
    ├── test_mqtt_packet.c
    ├── test_config.c
    └── test_ha_discovery.c
```

### Module responsibilities

| Módulo | Responsabilidade | Interface chave |
|--------|------------------|-----------------|
| `main.c` | Lifecycle: `plugin_load()` cria thread, `plugin_unload()` cleanup | GoldHEN entry hooks |
| `config.c` | Lê `/data/GoldHEN/plugins/ps4-mqtt/config.json` em struct | `config_t* config_load(const char* path)` |
| `mqtt_packet.c` | Serializa/parseia frames MQTT 3.1.1 (CONNECT, PUBLISH, PINGREQ, DISCONNECT, PINGRESP, CONNACK) | `int mqtt_encode_publish(uint8_t* buf, size_t buflen, const char* topic, const uint8_t* payload, size_t payload_len, uint8_t qos, bool retain)` |
| `mqtt_socket.c` | Wrap `sceNetSocket`/`sceNetConnect`/`sceNetSend`/`sceNetRecv` em API tipo BSD | `int socket_connect(const char* host, int port)`, `int socket_send(int fd, const void* buf, size_t len)`, `int socket_recv(int fd, void* buf, size_t len, int timeout_ms)`, `void socket_close(int fd)` |
| `mqtt_client.c` | Estado conexão, reconnect com backoff, keepalive, LWT setup | `mqtt_client_t* mqtt_client_new(config_t* cfg)`, `int mqtt_publish(mqtt_client_t* c, const char* topic, const char* payload, uint8_t qos, bool retain)`, `int mqtt_loop(mqtt_client_t* c)` |
| `collectors/*.c` | Cada coletor lê uma fonte, retorna struct | `int thermal_read(thermal_data_t* out)` |
| `ha_discovery.c` | Gera payload JSON config pra cada sensor | `int ha_publish_discovery(mqtt_client_t* c, const char* device_slug, const char* device_name, const char* fw_version)` |
| `log.c` | Wrap `sceKernelDebugOutText` / klog | macros `LOG_INFO`, `LOG_WARN`, `LOG_ERR`, `LOG_DEBUG` |

### Config struct

```c
typedef struct {
    char broker_host[64];     // required
    int  broker_port;         // default 1883
    char username[32];        // required
    char password[64];        // required
    char device_name[32];     // default "PS4"
    int  poll_interval_sec;   // default 10
} config_t;
```

### Config JSON example

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

## MQTT Topics

**Device slug:** lowercase + underscore do `device_name`. Ex: `"Sala"` → `ps4_sala`.

### State topics (publish a cada 10s, QoS 0)

```
ps4/<slug>/state              "on" | "standby"
ps4/<slug>/availability       "online" | "offline" (retain, LWT)
ps4/<slug>/game/title         "Bloodborne"
ps4/<slug>/game/title_id      "CUSA00900"
ps4/<slug>/cpu/temp           62.5
ps4/<slug>/soc/temp           58.0
ps4/<slug>/fan/rpm            1800
ps4/<slug>/memory/used_mb     4096
ps4/<slug>/memory/total_mb    8192
ps4/<slug>/network/ip         "192.168.1.50"
ps4/<slug>/network/ssid       "MinhaRede"
ps4/<slug>/network/rssi       -55
ps4/<slug>/storage/used_gb    320
ps4/<slug>/storage/total_gb   500
ps4/<slug>/uptime_sec         12345
ps4/<slug>/firmware           "11.00"
```

### Discovery topics (publish 1x na conexão, retain=true)

Padrão: `homeassistant/sensor/ps4_<slug>_<sensor>/config`

Exemplo payload `ps4_sala_cpu_temp`:

```json
{
  "name": "CPU Temperature",
  "unique_id": "ps4_sala_cpu_temp",
  "state_topic": "ps4/sala/cpu/temp",
  "availability_topic": "ps4/sala/availability",
  "unit_of_measurement": "°C",
  "device_class": "temperature",
  "state_class": "measurement",
  "device": {
    "identifiers": ["ps4_sala"],
    "name": "PS4 Sala",
    "manufacturer": "Sony",
    "model": "PlayStation 4",
    "sw_version": "11.00"
  }
}
```

Cada sensor (CPU temp, SoC temp, fan, mem used, etc) recebe payload similar com `unique_id`, `state_topic`, `unit_of_measurement` e `device_class` apropriados. `binary_sensor` usado para `state` (on/off) com `payload_on=on`, `payload_off=standby`.

### LWT (Last Will and Testament)

Configurado no CONNECT packet:
- Topic: `ps4/<slug>/availability`
- Payload: `offline`
- Retain: true
- QoS: 1

Se cliente desconectar abrupto, broker publica LWT → HA marca dispositivo indisponível.

Plugin publica `online` (retain) imediatamente após CONNACK bem-sucedido.

## Error Handling

### Conexão broker

| Falha | Ação |
|-------|------|
| Conexão TCP falha | Retry exponential backoff: 5s → 10s → 30s → 60s (cap 60s) |
| CONNACK rejeita (auth) | Log ERR, retry após 60s (não crash; senha pode mudar) |
| Conexão cai mid-flight | Detecta no próximo `send` ou ping timeout, reconnect com backoff |
| Timeout PINGRESP | Considera desconectado, reconnect |

### Coletor falha

| Falha | Ação |
|-------|------|
| sysctl retorna erro | Log warning, skip publish daquele sensor neste ciclo |
| libLNC indisponível (PS4 no menu) | Publica `game/title=null`, `game/title_id=null` |
| ICC sensor erro | Skip thermal, próximo ciclo tenta novamente |

Nenhum erro de coletor derruba thread. Sensor "stuck" no último valor publicado é aceitável; HA pode usar `expire_after` se quiser timeout no client side.

### Config inválida

| Caso | Ação |
|------|------|
| JSON malformado | Log ERR, plugin não inicia thread (no-op) |
| Campo obrigatório faltando (`broker_host`/`username`/`password`) | Log ERR, plugin não inicia |
| Campo opcional ausente | Aplica default |

### Plugin unload (graceful)

1. Sinal `stop_flag` pra thread principal
2. Thread principal sai do sleep, publica `availability=offline` (retain)
3. Envia DISCONNECT MQTT
4. Fecha socket, libera memória
5. `pthread_join` com timeout 2s; força cancel se não responder

### Logs

- Saída: `klog` (visível via `nc <ps4-ip> 9998` quando kdebug ativo) e/ou arquivo `/data/GoldHEN/log.txt`
- Níveis: `ERR`, `WARN`, `INFO`, `DEBUG`
- DEBUG desabilitado por default (compile-time flag)

## Testing Strategy

### Filosofia

Maximizar código testado no host (gcc/clang). Isolar dependências PS4-only nos coletores. MQTT encoding e config parsing são puro C — testáveis offline.

### Unit tests (host build, gcc + minunit)

| Arquivo | Cobertura |
|---------|-----------|
| `test_mqtt_packet.c` | CONNECT, PUBLISH, PINGREQ, DISCONNECT, CONNACK encoding/decoding bytes corretos vs spec MQTT 3.1.1 |
| `test_mqtt_packet.c` | Variable length integer encoding (1, 2, 3, 4 bytes) |
| `test_mqtt_packet.c` | Edge cases: payload vazio, topic com caracteres especiais |
| `test_config.c` | JSON válido completo → struct correta |
| `test_config.c` | JSON parcial → defaults aplicados |
| `test_config.c` | JSON malformado → erro |
| `test_config.c` | Campos obrigatórios faltando → erro |
| `test_ha_discovery.c` | Payload contém `unique_id`, `state_topic`, `device.identifiers` |
| `test_ha_discovery.c` | Slug gerado correto (lowercase + underscore) |

### Integration tests (host com mosquitto local)

| Arquivo | Cobertura |
|---------|-----------|
| `test_integration_publish.c` | Conecta mosquitto local, publica, mosquitto_sub recebe payload correto |
| `test_integration_reconnect.c` | Mata broker → cliente reconecta com backoff conforme esperado |
| `test_integration_lwt.c` | Mata cliente abrupto → broker publica LWT offline |
| `test_integration_discovery.c` | Discovery payloads aparecem em `homeassistant/sensor/.../config` |

### Manual smoke test (PS4 real)

1. Copia `homebrew.elf` build artifact pra `/data/GoldHEN/plugins/ps4-mqtt/` (FTP)
2. Copia `config.json` mesma pasta
3. Liga PS4 — plugin auto-carrega
4. Abre Home Assistant — verifica device "PS4 <name>" aparece com todos sensores
5. Inicia jogo — `game/title` muda em <30s
6. Volta pro menu home — `game/title_id` vai pra null
7. Mata broker (`systemctl stop mosquitto`) — HA marca offline em ~30s (LWT após keepalive)
8. Sobe broker — HA volta online em <60s

### Build targets

```makefile
make            # PS4 PRX (default, OpenOrbis)
make test       # Host unit tests
make integration # Host integration tests (precisa mosquitto local em :1883)
make clean
```

## Dependencies

- **OpenOrbis PS4 Toolchain** — build do PRX
- **GoldHEN 2.4b18.3+** — runtime
- **cJSON** — parser JSON (embedded em `third_party/`, MIT license)
- **minunit** — test framework (header-only, MIT)
- **mosquitto-clients** (host only) — `mosquitto_pub`/`mosquitto_sub` pra integration tests
- **gcc/clang** (host) — build dos tests

## Open Questions

Nenhuma bloqueante. Detalhes implementação (offsets ICC sysctl exatos, libLNC bindings) resolvidos durante implementação consultando GoldHEN source e plugins existentes (ex: ps4debug, leeful).

## References

- [GoldHEN GitHub](https://github.com/GoldHEN/GoldHEN)
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
- [MQTT 3.1.1 Spec](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
- [Home Assistant MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
- [cJSON](https://github.com/DaveGamble/cJSON)
