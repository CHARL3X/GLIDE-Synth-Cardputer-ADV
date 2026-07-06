# 08 — Breed: crossover two sounds into a child patch

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md`, `README.md`, and `docs/random-sound-generation.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** `breedSound(a, b, seed)` — a third verb for the generative engine: take the live sound and any slot, and roll a **child** that inherits traits from both. Randomize explores, Mutate refines, **Breed combines**.

**Architecture:** One new pure function in `dsp/sound_gen.cpp` (uniform crossover with occasional blending, clamped by the existing `Range` table so children can never leave the playable window), plus one CREATE row that picks the partner slot and runs the standard checkpoint → audition flow.

**Tech stack:** `src/dsp/sound_gen.{h,cpp}` + `src/test_dsp.cpp` + one settings row. No codec changes (a child is just params).

**Effort:** S–M (1 day). **Risk:** low.

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (CREATE: `Breed with q..p`). Within budget.

## Why this

The generative loop today is a one-parent story: roll strangers (Randomize) or walk one sound's neighbourhood (Mutate). The move players actually reach for after a week is *"I love the filter bite of my `w` and the airy tail of my `o` — give me that"*. Crossover is the genetic operator for exactly that want, it's ~80 lines next to code that already exists, and it deepens the "your instrument is yours" story: sounds now have *lineage*. Your rack stops being ten islands and becomes a family tree.

It also compounds with everything else: children of a Pluck (doc 01) and a blended pad (doc 02) are new territory neither Randomize nor Mutate reaches efficiently, because crossover preserves *co-adapted groups* (an envelope that suits a filter) instead of re-rolling them independently.

## Design

```cpp
// sound_gen.h — Breed a child from two parents. Deterministic in `seed`.
// Uniform crossover with occasional blending: each continuous field comes
// from a (40%), from b (40%), or lerps between them (20%, random t);
// categorical fields (waveform, filter mode, LFO shapes, glide mode, tilt
// routes) coin-flip between parents — never a third value, so a child is
// always recognizably "theirs". Mod-matrix slots are inherited WHOLE
// (routing + depth from one parent), preserving co-adapted routings.
// Every continuous field is clamped through the same Range table as
// generate/mutate, so a child can never leave the playable window.
GenPatch breedSound(const GenPatch& a, const GenPatch& b, uint32_t seed);
```

- Deterministic: same parents + same seed → same child (host-pinnable, and consistent with the whole engine's philosophy).
- `masterVol` is the player's, not the sound's — copy from parent `a` (the live sound), matching `generateSound`'s "caller keeps volume" rule. Verify how mutate treats it and mirror exactly.
- Naming: the child names itself the standard way (`soundName(patchHash(child))`) — content-derived, like every generated sound.
- **UI flow (CREATE section):** one row, `Breed with: q w e r t y u i o p` — `[`/`]` cycles the partner slot, select/OK runs: `historyCheckpoint()` → `child = breedSound(live, slotPatch, freshSeed)` → apply live → audition lick. Identical rhythm to Randomize/Mutate (checkpoint means Undo always walks back). Breeding with the slot you're on is allowed and harmless (child of identical parents ≈ the parent; don't special-case).
- Generative slots (`o`,`p`) as partners: load their patch data the same way `loadPatchData` materializes them — the partner is whatever the slot *sounds like*, not its seed.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- All bounds via the one `Range` table (CLAUDE.md: "all bounds live in one place"). If a `SynthParams` field exists that breed doesn't handle, that's a bug — the same completeness rule as generate/mutate.
- Settings-row pattern: `format`/`adjust` pair + `kItems[]` row, per CLAUDE.md step 4.

## Tasks

### Task 1: failing tests

**Files:** Modify `src/test_dsp.cpp`.

- [ ] Tests:
  - *determinism*: `breedSound(a, b, 42)` twice → identical patches (memcmp on the named fields, or field-by-field).
  - *inheritance*: with parents at opposite extremes (a: cutoff 200, b: cutoff 11000), over 50 seeds every child's cutoff ∈ [200, 11000] — and at least one child ≈ a's value, one ≈ b's, one strictly between (the three operators all fire).
  - *categorical honesty*: parents saw/square → every child's wave is saw or square, never a third.
  - *bounds*: breed two hand-built patches with fields at Range edges over 100 seeds; assert every child field passes the same bounds assertions the existing generate tests use (reuse their helper if there is one — read the existing tests first).
  - *identity-ish*: breeding a patch with itself returns that patch's values (lerp between equal values is the value).
- [ ] Run native — FAIL (no symbol). Commit red.

### Task 2: implement breedSound

**Files:** Modify `src/dsp/sound_gen.h`, `src/dsp/sound_gen.cpp`.

- [ ] Implement per the header comment above. Structure it against the Range table the way `mutateSound` is structured (read it first; reuse its per-field iteration pattern and its LCG usage so the RNG discipline matches).
- [ ] Handle the `GenPatch` tilt fields (route/depth ×2) as categoricals/continuous respectively.
- [ ] Native green. Commit: `sound_gen: breedSound — crossover, the third generative verb`.

### Task 3: the CREATE row

**Files:** Modify `src/ui/settings_screen.cpp` (and whatever helper materializes a slot's patch — grep `loadPatchData` in `src/storage/glide_config.cpp`).

- [ ] Add the `Breed with` row to CREATE (below Mutate amt): `format` shows the partner slot letter + its short name; `adjust` cycles q..p; select runs checkpoint → breed → apply → audition (mirror the Randomize button's action path exactly — read it first).
- [ ] Fresh seed source: whatever Randomize uses for its per-press seed (find it; don't invent a second entropy path).
- [ ] `pio run`; commit.

### Task 4: device verification

- [ ] Breed ACID (`w`) with Ethereal (`t`) five times: children are audibly "between" (squelchy pads / airy acids). Undo walks back each time.
- [ ] Breed with a generative slot (`o`) works and matches how `o` sounds.
- [ ] Save a child to a slot and SD; reboot; it survives (no codec work needed — verify anyway).

## Acceptance criteria

- Native suite green including all Task 1 assertions.
- The CREATE flow (checkpoint → breed → audition → undo) matches Randomize's rhythm exactly.
- One settings row, zero gestures, bounds provably respected.

## Risks

- Children of wildly different parents can be bland (regression toward the middle) — the 40/40/20 operator split biases toward *inheritance* over averaging by design; if children still feel mushy on device, drop lerp to 10%. Tune by ear, note the final split in the code comment.
- UI slot-picker could creep into a full picker card — resist; the cycle-a-letter row is the whole UI. YAGNI.
