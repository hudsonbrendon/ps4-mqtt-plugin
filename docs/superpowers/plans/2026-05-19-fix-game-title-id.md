# Fix Game Title ID Implementation Plan

> **Status:** ABANDONED — every non-hook source we tried fails inside the game sandbox. See the "Outcome" section at the bottom. The plan is preserved as documentation of the dead-ends so the next iteration (via GoldHEN_Plugins_SDK Detour hooks) doesn't repeat them.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish a real `CUSAxxxxx` value to `ps4/ps4/game/title_id` whenever the plugin is loaded into a game process, instead of the current empty string.

**Architecture:** The plugin already runs inside the game process — the title_id is *our own* sandbox identity. Replace the current `sceSystemServiceGetAppIdOfBigApp` + `sceLncUtilGetAppTitleId` path (both return errors from inside the sandbox) with one that reads the sandbox identity directly. We probe several candidate sources in a first build, observe which one returns a CUSA, then collapse the code to a single clean call.

**Tech Stack:** C99 PS4 PRX, OpenOrbis toolchain (Docker), MQTT for hardware feedback, existing `src/collectors/app_ps4.c` collector.

---

## Context for the engineer

You won't be able to run code locally — the plugin only runs on a PS4 with GoldHEN. The feedback loop is:

1. Edit C in `src/collectors/app_ps4.c` and/or `src/main.c`
2. `make prx-clean && scripts/build-prx.sh`
3. `curl -T build/ps4-mqtt.prx ftp://192.168.31.166:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx`
4. Ask the user to **close and reopen** the game (plugins load on game launch, not at boot)
5. Read MQTT topics with `mosquitto_sub -h 192.168.31.150 -p 1883 -u hudsonbrendon -P '@Admin996247004' -t 'ps4/ps4/game/#' -v`

You have ~15s of game-launch time before the worker thread starts publishing. The first publishes come within 1-2s of the game's main menu being interactive.

**What we already know is broken (do not re-attempt these blindly):**

| API | Result inside game sandbox |
|---|---|
| `sceSystemServiceGetAppIdOfBigApp()` | returns `-2137784305` (error) |
| `sceLncUtilGetAppTitleId(app_id, ...)` | returns `-1` even when given the "big app" id |
| `fopen("/data/...", "w")` | crashes the game (CE-34878-0) |
| `sceNetCtlInit` | crashes |
| `statvfs("/user")` | crashes |
| `scePadReadStateExt` | privilege error |
| `scePadSetProcessPrivilege(1)` | returns -1 |

Anything that reads-only out of `/proc/self/*` or calls a libc syscall that doesn't escape the sandbox tends to work. File writes and "tell me about other processes" APIs do not.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/collectors/app_ps4.c` | Owns title_id detection | Replace the `sceSystemServiceGetAppIdOfBigApp` + `sceLncUtilGetAppTitleId` body with the probe (Task 1) then the cleaned-up version (Task 4) |
| `src/collectors/collectors.h` | `app_data_t` struct | Already has `char debug[64]` (added during the controller debugging session). Reused here to ship probe results back over MQTT. No change needed. |
| `src/main.c` | Worker that publishes | `publish_state` already publishes `ps4/ps4/game/debug` from `app.debug`. No change needed during the probe phase. Task 5 removes the debug publish once we don't need it. |
| `Makefile` | PS4_LDFLAGS | May need to drop `-lSceLncUtil` and/or add `-lSceSystemService` depending on which method wins. Adjust in Task 4. |
| `README.md` | Sensor table | Update "Game Title ID" note from "blank in XMB" → describe new behavior in Task 5 |

---

## Task 1: Probe five title-id sources from one collector call

**Files:**
- Modify: `src/collectors/app_ps4.c` (replace `collect_app` body and helpers)

**Why this design:** Trying methods one at a time means one round-trip per attempt (rebuild + upload + game restart). Probing five sources in a single build narrows the answer to one game launch. Each source writes its result into `out->debug` so we can read it from the existing `ps4/ps4/game/debug` MQTT topic.

- [ ] **Step 1: Read current app_ps4.c so you understand what's there**

```bash
cat src/collectors/app_ps4.c
```

You'll see the current `collect_app` calls `sceSystemServiceGetAppIdOfBigApp` and `sceLncUtilGetAppTitleId` and a `parse_titleid_from_cwd` helper that opens `/proc/self/cwd` with `fopen`. All three paths return nothing useful.

- [ ] **Step 2: Replace the entire file with the probe version**

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int sceKernelGetSandboxName(char *buf, int size);
extern int sceKernelGetCurrentDirName(char *buf, int size);
extern int sceSystemServiceGetAppId(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

static int extract_cusa(const char *src, char *out, size_t out_len) {
    if (!src || !out || out_len == 0) return -1;
    const char *p = strstr(src, "CUSA");
    if (!p) return -1;
    size_t i = 0;
    while (i < out_len - 1 && p[i] && p[i] != '/' && p[i] != '_'
           && p[i] != '\0' && p[i] != ' ') {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return (i >= 5) ? 0 : -1;
}

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char sandbox[64] = {0};
    int rc_sandbox = sceKernelGetSandboxName(sandbox, sizeof(sandbox));

    char cwd[256] = {0};
    char *rc_cwd_ptr = getcwd(cwd, sizeof(cwd));

    char exe[256] = {0};
    int rc_exe = (int)readlink("/proc/self/exe", exe, sizeof(exe) - 1);

    int app_id = sceSystemServiceGetAppId();
    char lnc_title[16] = {0};
    int rc_lnc = -1;
    if (app_id > 0) {
        rc_lnc = sceLncUtilGetAppTitleId(app_id, lnc_title,
                                         sizeof(lnc_title));
    }

    char from_sandbox[16] = {0};
    char from_cwd[16] = {0};
    char from_exe[16] = {0};
    if (rc_sandbox == 0)  extract_cusa(sandbox, from_sandbox, sizeof(from_sandbox));
    if (rc_cwd_ptr)       extract_cusa(cwd,     from_cwd,     sizeof(from_cwd));
    if (rc_exe > 0)       extract_cusa(exe,     from_exe,     sizeof(from_exe));

    snprintf(out->debug, sizeof(out->debug),
             "sb=%s cwd=%s exe=%s lnc=%s app=%d",
             from_sandbox[0] ? from_sandbox : "-",
             from_cwd[0]     ? from_cwd     : "-",
             from_exe[0]     ? from_exe     : "-",
             lnc_title[0]    ? lnc_title    : "-",
             app_id);

    const char *winner = NULL;
    if (from_sandbox[0])      winner = from_sandbox;
    else if (lnc_title[0])    winner = lnc_title;
    else if (from_cwd[0])     winner = from_cwd;
    else if (from_exe[0])     winner = from_exe;

    if (winner) {
        out->in_game = 1;
        strncpy(out->title,    winner, sizeof(out->title)    - 1);
        strncpy(out->title_id, winner, sizeof(out->title_id) - 1);
    }
    return 0;
}
```

**Why these specific four sources (and not more):**

| Source | Why it should work | Why it might not |
|---|---|---|
| `sceKernelGetSandboxName` | Specifically designed to return the running app's sandbox dir like `CUSA12345_000`. Lives in libkernel — no privilege escalation. | Symbol might not exist in this PS4 firmware. Linker will fail loudly. |
| `getcwd` | Pure libc. Returns `/app0` or the app's mount inside the sandbox. The sandbox path **may** contain CUSA. | The mount usually strips the `/mnt/sandbox/CUSAxxx_000/` prefix and you just get `/app0`. May return useless data — that's fine, we just won't see CUSA in the debug string. |
| `readlink("/proc/self/exe", ...)` | POSIX syscall. PS4 supports `/proc/self/*` symlinks. The exe path lives at `/mnt/sandbox/CUSAxxx_000/app0/eboot.bin`. | `readlink` may return ENOSYS on PS4 — returns -1 which we handle. |
| `sceSystemServiceGetAppId` (no "BigApp") | This returns the *caller's* app id, not the foreground app's. Calling from inside the game means we ARE the caller. Then feed that into `sceLncUtilGetAppTitleId`. | `sceLncUtilGetAppTitleId` already failed once with the BigApp id; might fail with this id too. Worth one slot to be sure. |

- [ ] **Step 3: Build**

```bash
make prx-clean > /dev/null && scripts/build-prx.sh 2>&1 | tail -5
```

Expected last line: `>>> Done. Output: -rw-r--r-- ... build/ps4-mqtt.prx`

If the build fails with `undefined symbol: sceKernelGetSandboxName`, that PS4 firmware doesn't export it. Remove the `sceKernelGetSandboxName` block (decl + variable + `extract_cusa(sandbox, ...)` call) and rebuild. Note the failure in the next step's debug string by emitting `sb=none-symbol` instead of `sb=-`.

If the build fails with `undefined symbol: sceKernelGetCurrentDirName`, do nothing — that symbol isn't referenced in the code above, but if you added it during a search step, remove it.

- [ ] **Step 4: Confirm FTP is reachable, then upload**

```bash
nc -zv -w 3 192.168.31.166 2121
```

Expected: `Connection to 192.168.31.166 port 2121 [tcp/scientia-ssdb] succeeded!`

If refused: the user needs to re-run the GoldHEN exploit (PS4 lost the payload after a reboot). Ask them, then re-try.

```bash
curl -sS -T build/ps4-mqtt.prx \
  ftp://192.168.31.166:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
```

Expected: silent success (no curl error).

- [ ] **Step 5: Commit the probe**

```bash
git add src/collectors/app_ps4.c
git commit -m "debug(app): probe four title_id sources in collect_app

The current path (sceSystemServiceGetAppIdOfBigApp + sceLncUtil
GetAppTitleId) returns errors from inside a game sandbox, so
ps4/ps4/game/title_id is empty.

Probes sceKernelGetSandboxName, getcwd, readlink(/proc/self/exe),
and sceSystemServiceGetAppId-then-lnc, writes all four results into
app_data_t.debug (already wired to ps4/ps4/game/debug), and uses the
first that yielded a CUSA prefix as title_id.

Temporary — Task 4 collapses this to the single winning source."
```

---

## Task 2: Capture probe results from the running game

**Files:** none modified.

- [ ] **Step 1: Start a subscriber for the probe output**

```bash
timeout 60 mosquitto_sub -h 192.168.31.150 -p 1883 \
  -u hudsonbrendon -P '@Admin996247004' \
  -t 'ps4/ps4/game/#' -v -W 58
```

Run this in the foreground. It will block for ~60s.

- [ ] **Step 2: Ask the user to close and reopen a game**

Exact message to send the user: *"Fecha e abre o jogo de novo — vou ler o resultado em ~60s."*

- [ ] **Step 3: Record the `ps4/ps4/game/debug` line**

Look in the subscriber output for a line like:

```
ps4/ps4/game/debug sb=CUSA12345 cwd=- exe=CUSA12345 lnc=- app=12345
```

Save the exact values you saw. Each of `sb`, `cwd`, `exe`, `lnc` is either a CUSA, `-` (the source returned nothing parseable), or `none-symbol` (the source's symbol didn't link — see Task 1 Step 3).

- [ ] **Step 4: Also record the live `title_id`**

In the same subscriber output, look for:

```
ps4/ps4/game/title_id CUSA12345
```

If this is non-empty, the probe found a working source via the priority list (`sb` > `lnc` > `cwd` > `exe`). If still empty, none of the four worked and Task 3 has to research alternatives.

---

## Task 3: Choose the winning source

**Files:** none modified — this is an analysis step.

- [ ] **Step 1: Pick the source from the debug string**

Use the priorities `sceKernelGetSandboxName` > `sceLncUtilGetAppTitleId` > `getcwd` > `readlink`. Pick the first one in this list whose probe field is not `-` and not `none-symbol`.

Why this priority order:
1. `sceKernelGetSandboxName` is purpose-built and returns clean `CUSAxxx_000` with no parsing.
2. `sceLncUtilGetAppTitleId` returns the bare `CUSAxxx` directly.
3. `getcwd` requires substring parsing but is dead-simple libc.
4. `readlink` requires substring parsing and is the most likely of the four to ENOSYS on older firmware.

- [ ] **Step 2: If ALL four sources returned `-`**

Stop and re-plan. Don't proceed to Task 4. Open `docs/superpowers/plans/2026-05-19-fix-game-title-id.md` and add a Task 3a that researches more sources — candidates worth trying next:

- `getenv("HOME")` and other env vars (cheap, no syscall)
- Parse `argv[]` passed to `plugin_load(int argc, const char *argv[])` — the GoldHEN loader may pass useful strings
- `sceKernelGetProcessName` if it exists in libkernel
- Read `/proc/self/cmdline` via `open` + `read` (not `fopen`, which we know crashes for writes — read is untested but worth trying via `sceKernelOpen`/`sceKernelRead` syscalls)

Stop here, do not write code for those without re-planning.

- [ ] **Step 3: Record the choice**

Write the chosen source and the CUSA you observed into the commit message that ends Task 4.

---

## Task 4: Collapse the probe to a single clean call

**Files:**
- Modify: `src/collectors/app_ps4.c` (rewrite around the winning source)
- Modify: `Makefile` (drop unused libs if relevant)

The exact code below shows the four shapes — pick the one matching Task 3's winner and use **only** that. Delete the others before committing.

- [ ] **Step 1: Open `src/collectors/app_ps4.c` and replace `collect_app` with the chosen shape**

**Shape A — sceKernelGetSandboxName won:**

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>

extern int sceKernelGetSandboxName(char *buf, int size);

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char sandbox[64] = {0};
    if (sceKernelGetSandboxName(sandbox, sizeof(sandbox)) != 0) return 0;

    const char *p = strstr(sandbox, "CUSA");
    if (!p) return 0;
    size_t i = 0;
    while (i < sizeof(out->title_id) - 1
           && p[i] && p[i] != '/' && p[i] != '_') {
        out->title_id[i] = p[i];
        i++;
    }
    out->title_id[i] = '\0';
    if (i < 5) { out->title_id[0] = '\0'; return 0; }
    strncpy(out->title, out->title_id, sizeof(out->title) - 1);
    out->in_game = 1;
    return 0;
}
```

**Shape B — sceLncUtilGetAppTitleId (with `sceSystemServiceGetAppId`, not `BigApp`) won:**

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>

extern int sceSystemServiceGetAppId(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int app_id = sceSystemServiceGetAppId();
    if (app_id <= 0) return 0;
    if (sceLncUtilGetAppTitleId(app_id, out->title_id,
                                sizeof(out->title_id)) != 0) {
        out->title_id[0] = '\0';
        return 0;
    }
    strncpy(out->title, out->title_id, sizeof(out->title) - 1);
    out->in_game = 1;
    return 0;
}
```

**Shape C — getcwd won:**

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char cwd[256] = {0};
    if (!getcwd(cwd, sizeof(cwd))) return 0;

    const char *p = strstr(cwd, "CUSA");
    if (!p) return 0;
    size_t i = 0;
    while (i < sizeof(out->title_id) - 1
           && p[i] && p[i] != '/' && p[i] != '_') {
        out->title_id[i] = p[i];
        i++;
    }
    out->title_id[i] = '\0';
    if (i < 5) { out->title_id[0] = '\0'; return 0; }
    strncpy(out->title, out->title_id, sizeof(out->title) - 1);
    out->in_game = 1;
    return 0;
}
```

**Shape D — readlink won:**

```c
#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char exe[256] = {0};
    int n = (int)readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';

    const char *p = strstr(exe, "CUSA");
    if (!p) return 0;
    size_t i = 0;
    while (i < sizeof(out->title_id) - 1
           && p[i] && p[i] != '/' && p[i] != '_') {
        out->title_id[i] = p[i];
        i++;
    }
    out->title_id[i] = '\0';
    if (i < 5) { out->title_id[0] = '\0'; return 0; }
    strncpy(out->title, out->title_id, sizeof(out->title) - 1);
    out->in_game = 1;
    return 0;
}
```

- [ ] **Step 2: Adjust Makefile libs if needed**

If you picked **Shape A or C or D**, remove `-lSceLncUtil` from `PS4_LDFLAGS` — nothing references it any more, and dropping it cuts one import:

```bash
sed -i.bak 's/ -lSceLncUtil//g' Makefile && rm Makefile.bak
grep PS4_LDFLAGS Makefile
```

If you picked **Shape B**, leave the Makefile alone.

- [ ] **Step 3: Build**

```bash
make prx-clean > /dev/null && scripts/build-prx.sh 2>&1 | tail -3
```

Expected: `>>> Done. Output: ... build/ps4-mqtt.prx`.

- [ ] **Step 4: Upload**

```bash
curl -sS -T build/ps4-mqtt.prx \
  ftp://192.168.31.166:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
```

- [ ] **Step 5: Verify on hardware**

Ask the user to close and reopen the game.

```bash
timeout 30 mosquitto_sub -h 192.168.31.150 -p 1883 \
  -u hudsonbrendon -P '@Admin996247004' \
  -t 'ps4/ps4/game/title_id' -v -W 28
```

Expected: a `ps4/ps4/game/title_id CUSAxxxxx` line appears within ~20s of game launch.

If empty: the cleaned-up shape lost something the probe had. Revert this task's `app_ps4.c` change with `git checkout -- src/collectors/app_ps4.c`, restart from Task 1.

- [ ] **Step 6: Commit**

```bash
git add src/collectors/app_ps4.c Makefile
git commit -m "fix(app): use <SOURCE> to detect title_id from inside game

<SOURCE> works from inside the game sandbox where the previous
sceSystemServiceGetAppIdOfBigApp + sceLncUtilGetAppTitleId path
returned errors. Observed live: title_id now publishes as
<CUSA-YOU-SAW> when the game is running.

Drops the probe + the unused libs / fallback paths."
```

Replace `<SOURCE>` with one of: `sceKernelGetSandboxName`, `sceSystemServiceGetAppId+sceLncUtilGetAppTitleId`, `getcwd`, `readlink(/proc/self/exe)`. Replace `<CUSA-YOU-SAW>` with the actual value from Step 5.

---

## Task 5: Remove the debug topic and update docs

**Files:**
- Modify: `src/main.c` (drop the `app.debug` publish)
- Modify: `README.md` (sensor table note)

- [ ] **Step 1: Read the relevant section of main.c**

```bash
grep -n "game/debug\|app.debug" src/main.c
```

You should see one line publishing `ps4/ps4/game/debug` from `app.debug`. That's now dead weight — Task 4 doesn't write anything useful into `app.debug` (it isn't even initialized in the cleaned-up `collect_app`).

- [ ] **Step 2: Delete that publish**

```bash
# Open src/main.c, find the block:
#
#     app_data_t app;
#     if (collect_app(&app) == 0) {
#         mqtt_client_publish(c, "ps4/ps4/game/title_id",
#                             app.in_game ? app.title_id : "", 0);
#         mqtt_client_publish(c, "ps4/ps4/game/in_game",
#                             app.in_game ? "yes" : "no", 0);
#         mqtt_client_publish(c, "ps4/ps4/game/debug", app.debug, 0);   <-- remove this line
#     }
```

Apply the edit and confirm by re-grepping:

```bash
grep -n "game/debug\|app.debug" src/main.c
```

Expected: no output.

- [ ] **Step 3: Clear the retained debug topic on the broker**

```bash
mosquitto_pub -h 192.168.31.150 -p 1883 \
  -u hudsonbrendon -P '@Admin996247004' \
  -t 'ps4/ps4/game/debug' -r -m ''
```

- [ ] **Step 4: Update README.md**

Find the sensor table row:

```
| Game Title ID | `ps4/ps4/game/title_id` | CUSA code, blank in XMB |
```

Change the note to match what now actually works:

```
| Game Title ID | `ps4/ps4/game/title_id` | CUSA code of the running game (only published while in-game) |
```

- [ ] **Step 5: Build, upload, verify**

```bash
make prx-clean > /dev/null && scripts/build-prx.sh 2>&1 | tail -2
curl -sS -T build/ps4-mqtt.prx \
  ftp://192.168.31.166:2121/data/GoldHEN/plugins/ps4-mqtt/ps4-mqtt.prx
```

Ask the user to close and reopen the game. Then:

```bash
timeout 25 mosquitto_sub -h 192.168.31.150 -p 1883 \
  -u hudsonbrendon -P '@Admin996247004' \
  -t 'ps4/ps4/game/#' -v -W 23
```

Expected: you see `title_id` and `in_game` but **no** `game/debug` line. The retained debug topic should also be gone.

- [ ] **Step 6: Commit**

```bash
git add src/main.c README.md
git commit -m "chore: drop game/debug probe topic and update sensor doc

The probe served its purpose in Task 1-4 (identifying which
sandbox-safe source returns title_id). The cleaned-up collect_app
no longer writes app.debug, so drop the publish and the retained
broker entry. README's sensor table now reflects the working
behavior instead of 'blank in XMB'."
```

- [ ] **Step 7: Push**

```bash
git push origin main
```

---

## Task 6 (optional, only if you have appetite): map CUSA → game name

**Files:**
- Modify: `src/collectors/app_ps4.c` (already has `app_data_t.title` field set to title_id; this task fills it with a human name)
- Optional: `src/collectors/title_db.c` (new — small static map)

**Why this is optional:** the user explicitly said "deixa isso pra depois" about the controller battery rabbit hole; the same caveat applies here. The current state ships a working `Game Title ID`. A human name is a *nice to have*. Punt unless the user asks for it.

If asked to proceed:

- [ ] **Step 1: Decide on the data source**

Three candidates, pick one:

1. **Static map in code** — a `static const struct { const char *id; const char *name; }` table of CUSAs you care about. Pros: zero syscalls. Cons: covers only games on the list; needs PR each time a new game is wanted.
2. **`sceAppContentInitialize` + `sceAppContentGetAddcontInfoList`** — exposes the title's content info. Untested in-sandbox; do a single-method probe like Task 1 first.
3. **HA-side template** — publish only the CUSA; have Home Assistant resolve to a friendly name via a `template_sensor` against a YAML map. Pros: no plugin change. Cons: configuration lives in HA, not the plugin.

If unsure, do **3** — costs you zero PRX rebuilds. Update README with a copy-pasteable HA template.

- [ ] **Step 2: If you picked option 1, this is the code**

Create `src/collectors/title_db.c`:

```c
#include <string.h>

struct title_map { const char *id; const char *name; };

static const struct title_map TITLES[] = {
    {"CUSA00001", "The Playroom"},
    {"CUSA12345", "Replace with real CUSA + name"},
    {NULL, NULL}
};

const char *title_db_lookup(const char *title_id) {
    if (!title_id || !title_id[0]) return "";
    for (const struct title_map *t = TITLES; t->id; t++) {
        if (strcmp(t->id, title_id) == 0) return t->name;
    }
    return "";
}
```

Add it to `PS4_SOURCES` in `Makefile`. Declare `extern const char *title_db_lookup(const char *)` in `app_ps4.c`. In `collect_app`, after computing `title_id`, replace the `strncpy(out->title, out->title_id, ...)` line with:

```c
const char *name = title_db_lookup(out->title_id);
strncpy(out->title, name[0] ? name : out->title_id, sizeof(out->title) - 1);
```

Add a `ps4/ps4/game/title` publish in `main.c` and a Discovery sensor `game_title` with no `device_class`.

- [ ] **Step 3: Commit**

```bash
git add src/collectors/app_ps4.c src/collectors/title_db.c Makefile src/main.c
git commit -m "feat(app): publish human game name via static title_id map"
git push origin main
```

---

## Self-review

**Spec coverage:** the request was "fix the game title ID that's coming through empty." Tasks 1–4 deliver a working title_id. Task 5 cleans up the diagnostic topic so we don't ship debug data. Task 6 is the natural follow-on (human name) but explicitly optional.

**Placeholder scan:** the only intentional placeholders are `<SOURCE>` and `<CUSA-YOU-SAW>` in the Task 4 commit template — those *must* be filled in by the engineer based on what the hardware actually returned, and the surrounding prose tells them so. The Task 6 static map has `"CUSA12345"` placeholder, but that task is explicitly optional and the placeholder location is called out.

**Type consistency:** `app_data_t.title_id` is `char[16]`, `app_data_t.title` is `char[64]`, `app_data_t.debug` is `char[64]`, `app_data_t.in_game` is `int`. All four task shapes write within those bounds. The `extract_cusa` helper takes `out_len` so the same function reuses across sources without buffer assumptions.

---

## Outcome (2026-05-20)

Probed 9 candidate sources across 3 build iterations. None work from
inside a game sandbox without a Detour hook.

| Source | Result |
|---|---|
| `sceKernelGetSandboxName` | symbol not exported by OpenOrbis toolchain |
| `readlink("/proc/self/exe", ...)` | symbol needs `-lkernel_sys` (untested for crash safety, not added) |
| `getcwd(...)` | crashes the game |
| `sceSystemServiceGetAppIdOfBigApp` + `sceLncUtilGetAppTitleId` | returns -2137784305 / -1 (no crash, no data) |
| `argv[]` from `plugin_load(argc, argv)` | empty — GoldHEN loader passes no title info |
| `getenv("SCE_TITLEID" / "SCE_BREADCRUMB_DUMP_ROOT" / "HOME" / "PWD")` | all NULL or no CUSA in value |
| `open("/proc/self/cmdline", O_RDONLY)` | returns -1 (FreeBSD-style /proc is partial) |
| `sceLncUtilGetAppId` / `sceLncUtilGetApp0DirPath` (Task 1c) | crashes the game on call |
| `sceAppInstUtilAppGetInsertedDiscTitleId` (Task 1c) | crashes the game on call |

**Code state after this plan:**
- `src/collectors/app_ps4.c` removed from `PS4_SOURCES` — it produced
  no useful output for any of the call shapes above, and the
  in-game crash from Task 1c made the simpler shapes unsafe too.
- `src/main.c` stops publishing `ps4/ps4/game/title_id` and the
  debug probe topic. `ps4/ps4/game/in_game` stays, hardcoded to
  `"yes"` (the worker thread only runs inside a game, so this is
  always true).
- HA Discovery for `Game Title ID` is dropped.
- README "Not yet supported" section enumerates each blocked source
  so future contributors don't re-litigate the same APIs.

**Path forward for the next attempt:** integrate `GoldHEN_Plugins_SDK`
with `crtprx.o` + `libGoldHEN_Hook.a`, then `HOOK_INIT` +
`HOOK_CONTINUE` on a function the running game itself calls that
takes the title id as an argument or returns it (e.g. the path the
game uses internally to load resources from `/app0/`). This shifts
the privilege boundary from "plugin asks the kernel" to "plugin
sees what the game already has." That work is the same prerequisite
as the controller-battery rabbit hole — bundling them together makes
sense.
