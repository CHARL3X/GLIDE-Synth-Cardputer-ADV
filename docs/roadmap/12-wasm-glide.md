# 12 — GLIDE in the browser: compile dsp/ to WASM

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **No firmware risk: this doc never touches device code paths.**

**Goal:** The entire musical core — synth, glide engine, FX, generative engine, patch codec — running in a web page: play it on a QWERTY keyboard, roll/mutate sounds, load and preview `.gpat` files from your SD card, hear exactly what the device would play.

**Architecture:** `dsp/` is pure C++ *by law* (CLAUDE rule 1) and patch_codec already compiles host-side — the porting boundary this project has defended since day one gets its first second incarnation. A ~150-line `extern "C"` façade compiles with Emscripten into a WASM module; an `AudioWorkletProcessor` pulls 128-sample blocks (the device's own block size); a static page maps physical QWERTY to the 4×10 grid positionally (KeyQ-style `event.code`, mirroring the firmware's "never the char" rule). Deployed by CI to GitHub Pages, alongside a CI job that finally runs the native tests on every push.

**Tech stack:** Emscripten (pinned via `mymindstorm/setup-emsdk` in CI), vanilla JS + Web Audio AudioWorklet, GitHub Actions + Pages. **No frameworks, no npm** — one HTML file, one worklet JS, one glue JS, the WASM.

**Effort:** L (2–3 days). **Risk:** low (worst case: the page doesn't ship; the firmware never knows).

**UI-cost budget:** n/a — zero device UI. The simplicity rule is satisfied vacuously; this is ecosystem, not instrument.

## Why this

Three products for one build target:

1. **Try-before-flash.** The README's pitch ("slides between every note, builds its own sounds") becomes a *link*. Anyone deciding whether to buy a Cardputer for GLIDE can play GLIDE first. This is the highest-leverage marketing artifact the project can own, and it's generated from the *same source files* as the firmware, so it can never lie about the sound.
2. **A patch workbench.** Drop a `.gpat` from your SD card onto the page → hear it, read every parameter, roll variants at desktop speed. The tagged codec is the same bytes; the browser becomes the library manager the 240×135 screen can't be.
3. **A development instrument.** DSP changes become audible in seconds without hardware. Every future roadmap doc (pluck, blend, freeze…) can be *heard* in the page before a single flash cycle. CI running `env:native` on every push falls out of the same workflow file.

The deep reason: CLAUDE rule 1 exists because "when this instrument grows into real hardware, the entire musical core moves over unchanged." WASM is the cheapest possible proof that the boundary actually holds — anything that creeps into dsp/ that shouldn't be there breaks this build loudly.

## Design

### The façade (`src/wasm/glide_api.cpp`, compiled only by emcc — never in any pio env)

```cpp
extern "C" {
void  glide_init(float sampleRate);                  // wavetables + synth init
void  glide_note(int type, int id, int lane, int legato, float pitchMidi);
void  glide_render(float* out, int n);               // -> worklet's Float32Array
int   glide_load_patch(const uint8_t* buf, int len); // decodePatch -> setParams; 0/1
int   glide_save_patch(uint8_t* buf, int cap);       // encodePatch of live params
void  glide_randomize(uint32_t seed);                // generateSound -> live
void  glide_mutate(float amount, uint32_t seed);
void  glide_name(uint32_t seed, char* out, int cap); // soundName for display
void  glide_set_param(int tag, float v);             // by codec tag — one setter,
float glide_get_param(int tag);                      // the codec table is the schema
}
```
The tag-addressed getter/setter is the design coup: the patch codec's tag table already enumerates every parameter with its type — the web UI enumerates *the same tags* instead of hand-mirroring 40 fields. (Implement by encoding/decoding through a scratch `PatchData` — slow-path fine at UI rate; do NOT add a parallel reflection table.)

### The page (`web/` at repo root)

- `web/index.html` — the instrument: a drawn 4×10 grid (keys light on press), scope canvas (reuse the logic of the device scope conceptually — draw the rendered buffer), buttons: Randomize / Mutate / drop-zone for `.gpat` / download current sound. Aesthetic: the GLIDE splash's look (dark, phosphor green, the logo from `assets/`).
- `web/worklet.js` — `AudioWorkletProcessor`: instantiates the WASM inside the worklet scope (fetch + compile in `process`-safe init; Emscripten `-s SINGLE_FILE=1` keeps deployment to one .js), calls `glide_render(128)` per quantum. Browser runs at 44.1/48 kHz: pass the **real** context rate to `glide_init` — dsp/ is sample-rate-parameterized throughout (`init(float sampleRate)`); do not resample.
- `web/main.js` — keyboard: `event.code` positional map `KeyQ..KeyP` → lane 2, `Digit1..0` → lane 3, `KeyA..Semicolon` → lane 1, `KeyZ..Slash` → lane 0 (matching the device's string layout), Shift = momentary chromatic, Space = sustain. Reimplement the *thin* key→NoteEvent logic (grid math via a JS port of `gridToMidi`'s few lines — acceptable duplication, note it; the alternative of compiling keys.cpp drags io/ across the boundary, which is exactly what the boundary forbids).
- Scale lock / degree mapping: port `rowDegrees` + `gridToMidi` to ~20 lines of JS with a comment pinning them to `src/dsp/pitch.h` as the source of truth.

### CI (`.github/workflows/ci.yml`)

- Job 1 **native-tests**: `pip install platformio` → `pio run -e native` → `.pio/build/native/program` (the repo finally gets its test gate on every push/PR).
- Job 2 **web** (needs job 1): setup-emsdk (pin a version) → the emcc command (in `support/build_wasm.sh` so humans can run it locally) → upload `web/` as the Pages artifact → deploy on pushes to master.

## Global constraints

- **Nothing under `src/wasm/` or `web/` may be compiled by any PlatformIO env** — check `build_src_filter` excludes `src/wasm/` in every env (add `-<wasm/>` explicitly to `[env:cardputer-adv]`, probe, and native).
- dsp/ purity is consumed here, never modified. If a dsp/ change is "needed" for the WASM build, that's a purity bug to fix in dsp/ terms, not an `#ifdef EMSCRIPTEN` to add.
- Native gate stays green: `pio run -e native && .pio/build/native/program`.
- No external JS dependencies; the page must work offline once loaded (it's an instrument, not a service).

## Tasks

### Task 1: façade + local WASM build

**Files:** Create `src/wasm/glide_api.cpp`, `support/build_wasm.sh`. Modify `platformio.ini` (the `-<wasm/>` exclusions).

- [ ] Write the façade per Design (thin: every function ≤ 10 lines, all logic stays in dsp/).
- [ ] `support/build_wasm.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
emcc -O2 -std=gnu++14 -DGLIDE_HOST_BUILD -I src \
  src/dsp/*.cpp src/storage/patch_codec.cpp src/wasm/glide_api.cpp \
  -s SINGLE_FILE=1 -s MODULARIZE=1 -s EXPORT_NAME=GlideModule \
  -s EXPORTED_FUNCTIONS=_glide_init,_glide_note,_glide_render,_glide_load_patch,_glide_save_patch,_glide_randomize,_glide_mutate,_glide_name,_glide_set_param,_glide_get_param,_malloc,_free \
  -s EXPORTED_RUNTIME_METHODS=ccall,HEAPF32,HEAPU8 \
  -o web/glide.js
```
- [ ] Build locally; verify `pio run` and `pio run -e native` still build with the exclusions in place. Commit.

### Task 2: worklet + minimal sound

**Files:** Create `web/index.html`, `web/worklet.js`, `web/main.js`.

- [ ] Worklet pulls 128-sample blocks into the output channel; main.js boots the context on first click (autoplay policy), sends a test note. **Gate:** a held Q at the device's default patch sounds like the device's GLIDE tone. Commit.

### Task 3: the keyboard + the grid

- [ ] Positional QWERTY map + JS `gridToMidi` port + legato/hammer-on lane logic (fresh On vs legato hand-off: mirror the firmware's rule — new key on a held lane = legato). Shift chromatic, Space sustain. On-screen grid mirrors key state.
- [ ] **Gate:** hold a chord, re-finger it three columns over — every voice glides. That's the product demo; if it doesn't feel like the YouTube short, iterate before proceeding. Commit.

### Task 4: the workbench

- [ ] Randomize/Mutate buttons (seed from `crypto.getRandomValues`), sound name display via `glide_name(patchHash…)` — expose the hash through the façade if needed (add `glide_hash()` — extend the ledger note in 00-INDEX if you do).
- [ ] `.gpat` drag-drop → `glide_load_patch`; download button → `glide_save_patch` → blob named via the sound's name. Round-trip a real `.gpat` from a device SD card. Commit.

### Task 5: CI + Pages

**Files:** Create `.github/workflows/ci.yml`.

- [ ] Both jobs per Design. Push a branch, verify the native job fails when a test is deliberately broken (prove the gate bites), then fix and merge.
- [ ] Enable Pages; verify the deployed URL plays. Add the link to README's demo section. Commit.

## Acceptance criteria

- The Pages URL plays GLIDE's default tone with working chord slides on QWERTY.
- A `.gpat` saved on-device loads in the page and sounds the same by ear.
- CI runs native tests on every PR; the WASM build fails if dsp/ purity breaks.
- Zero diffs to firmware behavior; `pio run` output byte-identical.

## Risks

- Worklet + Emscripten module loading has sharp edges (module instantiation inside the worklet scope) — `SINGLE_FILE=1` plus the documented `audioWorklet.addModule` pattern handles it; budget iteration time in Task 2.
- The JS `gridToMidi` port can drift from pitch.h — mitigated by the pin-comment and by its tiny size; if it ever grows, that's the sign to stop and reconsider, not to port more.
- Scope creep magnet (mobile touch UI, MIDI-in, patch sharing gallery…) — all explicitly out. YAGNI; ship the four gates.
