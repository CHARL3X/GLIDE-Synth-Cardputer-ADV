# 09 — Freeze: infinite reverb hold as a G0 trigger action

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination — this doc appends **`TriggerAction::Freeze`**.

**Goal:** Hold (or latch) G0 and the reverb tail **freezes** — whatever was ringing hangs as an infinite wash while new notes play dry on top. Release, and the wash decays naturally.

**Architecture:** One live-mod float on `SynthParams` (`fxFreeze`, never persisted — same class as `bendCents`), consumed inside `Fx`'s reverb: feedback ramps toward ~1.0, reverb *input* ramps toward 0 (the frozen tail takes no new sound — that's what lets the solo sit dry on top), damping disengages so the wash doesn't darken, and the wet return gets a floor so freeze is audible even on dry patches. One new `TriggerAction` enum entry — the G0 gesture, its depth, and its momentary/latch mode all already exist.

**Tech stack:** `src/dsp/params.h`, `src/dsp/fx.cpp`, `src/storage/glide_config.h` (enum + names), the G0 handler in `src/io/keys.cpp`. Host-tested.

**Effort:** S (half a day). **Risk:** low.

**UI-cost budget (the simplicity rule):** zero new gestures, **zero new settings rows** — the existing `Trigger action` row gains one entry. Tied with doc 07 as the cheapest feature here.

## Why this

The G0 trigger macro is GLIDE's one-finger performance system (muffle, brighten, dive, grit, morph) — all of them *shape the voice*. Freeze is the first one that shapes the *room*, and it's the move continuous-pitch instruments crave: hold a chord, freeze it into a pad, then solo dry over your own frozen harmony — a third backing layer that costs no voices, no events, no memory, because it lives in the comb filters that are already running. Every serious reverb pedal grew a freeze button for this exact reason. On an instrument whose identity is "one hand backs, the other solos," this is the purest expression yet: the backing is *the sound you just made, suspended*.

## Design

- `SynthParams` live-mod block gains `float fxFreeze = 0.f;  // 0..1, G0 -> reverb freeze; live-mod, never persisted` (placed with `bendCents`/`vibratoCents` — the patch-save hygiene that excludes live mods must exclude it too; verify where that hygiene lives and confirm it's positional/field-based, not "everything").
- `TriggerAction::Freeze` appended before `Count`; names: `"freeze (reverb)"` / `"FREEZE"`.
- G0 handler in `keys.cpp`: mirror exactly how `Muffle` ramps its 0..1 value from held/latched state × trigger depth — Freeze drives `params.fxFreeze` the same way (same ramp constants; read the Muffle path first).
- In `Fx::process`, reverb section — with `fz = clamp01(p.fxFreeze)` smoothed at block rate like the other fx params (`fzSm_` alongside `rvMixSm_`):
  - comb feedback: `fbEff = combFbSm_ + (0.9985f - combFbSm_) * fz` (0.9985, not 1.0: unconditionally stable, decays over ~minutes — effectively infinite, provably safe),
  - comb **input** gain: `× (1.f - fz)` (frozen tail accepts nothing new),
  - per-comb damping: `× (1.f - fz)` (don't let the wash dull out while held),
  - wet return floor: `rvMixEff = max(rvMixSm_, 0.6f * fz)` so freeze is audible even when the patch's reverb send is 0 — the *tail that existed at freeze time* is what plays; on a bone-dry patch that tail is near-silent, which is honest (you froze silence). The floor matters for patches with *some* send.
- Interactions: panic (`AllOff`) already calls `Fx::reset()` — panic kills a frozen wash. Correct; document in the code. The backing layer's sends also feed the same room — freezing a progression wash works and is glorious.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- Append-only enums (`TriggerAction::Freeze` at the end). `fxFreeze` must never enter the patch codec or NVS.
- Neutral default: `fxFreeze = 0` must leave `Fx::process` bit-for-bit (the `fz`-dependent terms must be exact no-ops at 0 — write the identity test).

## Tasks

### Task 1: failing host tests

**Files:** Modify `src/test_dsp.cpp`.

- [ ] Tests against `Fx` directly (init 32 kHz; `p.reverbMix = 0.5f`):
  - *identity*: with `fxFreeze = 0`, process a fixed input sequence; output must be byte-identical to the same run on params without the field set (i.e., before/after the change, guarded by comparing against a golden checksum captured pre-change — capture it FIRST, before touching fx.cpp).
  - *freeze holds*: feed 0.25 s of a 220 Hz tone, then silence with `fxFreeze = 1` for 2 s: RMS of the last 0.5 s ≥ 0.6 × RMS of the first 0.5 s of silence-period output.
  - *unfreeze releases*: same, but drop `fxFreeze` to 0 after 1 s: final 0.5 s RMS < 0.1 × the held level.
  - *dry solo stays dry*: while frozen, feed a new tone; the *input-correlated* component of the output should be ≈ the dry tone (assert output RMS ≈ dry RMS + frozen-tail RMS within tolerance — keep it a sanity band, not an exact decomposition).
- [ ] Run native — identity passes (nothing changed yet), the rest FAIL. Commit red.

### Task 2: params + fx implementation

**Files:** Modify `src/dsp/params.h`, `src/dsp/fx.h`, `src/dsp/fx.cpp`.

- [ ] Add the field, the `fzSm_` smoother, and the four effects listed in Design inside the reverb section (read the comb loop carefully — apply input gain where the send enters the combs, feedback inside each comb's recursion, damping on the per-comb one-pole).
- [ ] Native green (all Task 1 tests). Commit: `dsp: reverb freeze — the room becomes a holdable layer`.

### Task 3: the trigger action

**Files:** Modify `src/storage/glide_config.h` (enum + both name functions), the G0 action dispatch in `src/io/keys.cpp`.

- [ ] Append `Freeze` + names; wire the dispatch case mirroring Muffle's ramp, writing `fxFreeze` (target = ramped value × trigger depth).
- [ ] Latch mode: G0's existing momentary/latch setting applies for free — verify by reading the mode handling, not by assumption.
- [ ] `pio run`; commit.

### Task 4: device verification

- [ ] Ethereal + progression, latch Freeze mid-wash: the wash hangs, solo plays dry over it, unlatch → natural decay. Panic kills it instantly.
- [ ] Hold-freeze for 3+ minutes: no runaway level (the 0.9985 ceiling), no NaN/limiter events.
- [ ] Trigger depth at 50%: a *partial* freeze (long-but-decaying tail) — musically useful; note how it feels.

## Acceptance criteria

- Native suite green including the identity-at-zero golden test.
- The hold/latch/depth semantics match every other trigger action with zero new UI.
- Frozen wash + dry solo verified by ear; panic behavior correct.

## Risks

- Level buildup if input isn't fully muted at fz=1 — it is by construction (`×(1-fz)`); the 3-minute soak test is the backstop.
- The wet-return floor (0.6·fz) on a reverb-send-0 patch surfaces a near-silent tail — honest but potentially confusing; if testers stumble, drop the floor to `0.4·fz` or gate the action's HUD label to hint (`FREEZE ~` when send < 5%). Decide on device.
