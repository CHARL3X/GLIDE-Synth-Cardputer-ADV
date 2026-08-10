# Random sound generation + the SD patch library

> *"Everyone who uses this has a different experience, versus a standardized
> sound and setup. The randomize really makes that vibe come to life."*

This is the design note for the feature that turns the randomizer from a buried
settings action into the **center of gravity** of the instrument: every device
is unique out of the box, every player can roll/sculpt/keep their own sounds,
and an SD card holds an unlimited personal library. It builds on the seams the
codebase already laid for "the (future) SD-library load path" — it does not
fight the architecture.

## The ethos, made literal

Owning a Cardputer shouldn't mean sounding like everyone else who owns one. The
engine was always deep enough for that (mod matrix, multimode filter, full FX),
but the *default* was a standardized bank of ten named instruments. This feature
flips the default:

- **Your instrument is unique from first boot.** Each unit derives a stable
  random `seed` and rolls its nine non-anchor slots (`w..p`) from it. No two
  devices boot the same nine sounds. Slot `q` stays **GLIDE** — the home/boot
  anchor — so the signature tone and the instant-play experience never change.
- **Discovering a sound is play, not menu-diving.** Roll → audition → Mutate →
  audition → Keep. A tight loop with sound on the whole time.
- **You can explore fearlessly.** A non-destructive Undo/Redo history means a
  roll never costs you the sound you just liked. *Test without trashing.*
- **Your collection is yours and portable.** Save the sounds you love to SD as
  named files; they travel card-to-card and survive firmware updates.

## Architecture

```
dsp/sound_gen.{h,cpp}      PURE. generateSound(seed) / mutateSound(base,amt,seed)
  (host-tested)            -> GenPatch (synth + tilt). patchHash + nameForSeed.
        │
storage/glide_config.cpp   per-device seed (NVS), first-boot unique bank,
                           reRollBank(), RAM-only undo/redo history,
                           applyGenerated() / applyStoredPatch()
        │
storage/patch_codec.{h,cpp}  the tagged patch stream — ONE format for both
                           NVS slots AND SD files (byte-compatible)
        │
io/sd_store.{h,cpp}        .gpat files on microSD: save/list/load/delete
ui/sd_browser.{h,cpp}      the library browser (load-to-live, delete)
ui/settings_screen.cpp     the generative controls, in the existing SOUND menu
```

### Why the generator lives in `dsp/`

The randomizer used to be ~40 lines of `millis()`-seeded LCG inside the settings
UI. Moving it under the pure `dsp/` boundary buys three things at once:

1. **Determinism.** Seeded, so a per-device seed yields a reproducible bank — the
   whole "unique but stable" property depends on this.
2. **Testability.** `env:native` covers it: 200 rolls validated in-range and
   rendering finite/bounded, determinism, mutate range-safety, amount-0 identity.
3. **Portability.** When GLIDE grows into dedicated hardware, the sound
   *generator* ports over with the synth voice — the soul moves intact.

All field bounds live in one `Range` table, shared by `generate` (paint within)
and `mutate` (nudge a fraction of the span, then clamp back) — so a mutation can
never drift a value outside the playable window a roll respects.

## The archetype engine (2026-07): variety by construction

The original generator drew every parameter independently and uniformly from a
narrow middle band. Statistically that regresses every roll to the same center
— sustain could never go below 0.3 (no plucks or bells, ever), attack never
above 0.4 s (no pad swells), the filter envelope never past 2 octaves (no acid),
and `fenvAtkS` / `autoVibCents` / LFO sync were never rolled at all. Players
noticed: a library of saved rolls all sounded like "a buzzy wave with varying
reverb," and morph-blending between any two was nearly imperceptible.

`generateSound` now commits to a **character archetype first** — a weighted
pick among *pluck, bell, pad, bass, acid, lead, brass, chip* and *wild* (the
old anything-goes chaos, kept in the pool) — then paints every field from that
character's own correlated window, which is finally allowed to reach the
extremes (sustain 0, slow swells, 3.5-octave squelches, the brass filter-attack
swell). A small "spice" chance adds one off-archetype twist so the families
stay connected instead of collapsing into nine fixed presets with jitter.
`archetypeForSeed(seed)` is exposed and `generateSound(seed, archetype)` exists
as the hook for a future "roll me a pad" style picker.

Every roll then passes `sanitizePatch` — deterministic musical guardrails that
kill the known ways an in-range roll still sounds like trash: a highpass parked
so high the note whispers away, screaming resonance stacked on heavy drive,
echo + hall washing jointly into mush, struck sounds with no decay body, slow
swells that choke, and above all mod-matrix **pitch** routings past ~1 semitone
of wobble (previously a roll could wire an LFO to pitch at ±7 semitones —
atonal warble on a scale-locked instrument).

`mutateSound` keeps its neighbourhood semantics (amount 0 is still an exact
identity) but now also nudges the once-neglected fields, and clamps pitch-mod
depth only on routings *it* rewires — a player's own deliberate settings are
never "corrected."

**Glide must land.** The first archetype cut handed out glide times up to
0.35 s; the one-pole slew needs ~4.6× `glideS` to settle, so at playing speed
those rolls never *reached* a pitch — every note sat flat, parked between
where it was and where it was aimed. Two rules fix it, both tested: archetype
glide windows now live where the hand-tuned factory presets do (roughly
0.01–0.18 s, skewed quick), and `sanitizePatch` caps **always-glide** patches
at 0.16 s — in Always mode *every* note slides in, so the glide must be quick
enough to land; longer dreamy glides remain available to LegatoOnly rolls,
where fresh attacks hit pitch instantly and only deliberate hammer-on slides
ride the glide. Relatedly, the audition lick (`ui/audition.cpp`) now uses a
distinct id per note: re-pressing a sounding id takes the synth's legato path
and glides, so the old single-id phrase made every preview note slide even for
LegatoOnly sounds — rolls previewed far worse than they play. A native test
walks every roll through the lick's exact timings and asserts the final pitch
lands.

**Names follow the sound.** `soundNameForPatch` replaced the hash-only word
pick for everything newly minted: `classifySound` hears which family a patch
belongs to from its parameters alone (works on hand-built and mutated sounds
too), the noun comes from that family's bank (a bell gets bell/chime/halo, a
bass root/rumble/boom, acid wasp/venom/spiral…), the adjective from the
patch's texture and brightness, and `patchHash` bits still pick *within* the
banks so naming stays deterministic. The legacy `soundName` tables are frozen
behind the same `genver` gate — a genver-1 device re-derives its o/p slot
labels every boot, so the update changes neither those sounds *nor their
names*. Saved sounds everywhere are unaffected either way: their names are
baked into the patch data itself.

**Update safety (the seed contract).** A device's two generative slots (o, p)
are regenerated from its seed on every load, so changing the generator would
have silently changed sounds players already live with. Instead the old
generator is kept verbatim as `generateSoundLegacy` — pinned by golden hashes
in `test_dsp.cpp`, never to be edited — and storage gates on an NVS `genver`
flag: existing seeds keep rolling with the legacy engine until the player
re-rolls the bank (or the seed is otherwise fresh: first boot / wiped NVS),
which is the moment `genver` moves to 2. The **Randomize** button always uses
the archetype engine — each press is a fresh hardware-random seed with no
continuity to preserve. The native tests assert the archetype variety actually
occurs (plucks, bells, pads, sub-basses, acid sweeps, brass swells, every
waveform, every archetype across a 400-seed sweep), that every guardrail holds
on every roll, and that the legacy generator's output is frozen.

## The second wave (2026-08): five more characters, zero disturbed sounds

After enough rolling, the nine families start to feel *known* — and several
timbres the engine can clearly make were simply unreachable: no archetype
could roll a **sustained pure wave** (Lead only draws saw/square/fat/pulse;
Bell is percussive), an **instant-on held tone with no filter bloom**, a held
middle sustain between pluck and pad, or **deep tempo-synced filter movement**.
The expanded (genver-3) pool adds five archetypes aimed at exactly those holes:

- **Whistle** — the slide-whistle / theremin voice, the instrument's original
  concept made rollable: sine/triangle, env-gated breath noise, singing
  vibrato (4–12 cents), glide-forward (65% always-glide, capped so notes still
  LAND), tilt on vibrato like a theremin hand.
- **Organ** — drawbars under a rotary: square/sine over a 0.4–0.8 sub (the
  16'), instant attack, full sustain, **fenvOct stays 0** (the one envelope
  shape nothing else rolls), LFO1→Amp rotary at spin (5–7 Hz) or chorale
  (0.6–1.3 Hz) speed, tilt on volume — the swell pedal.
- **Keys** — the tine piano: triangle/sine with a 0.5–1.5-oct filter bark, a
  held 0.28–0.48 sustain no pluck reaches, wobbling-speaker tremolo, one roll
  in five driven dirty.
- **Wobble** — dub bass: low cutoff (500–1100 Hz), heavy sub, and LFO1
  **tempo-synced** into the cutoff at 0.25–0.5 depth (±1–2 octaves), so the
  filter pumps on the same clock the delay and progression share. Square LFO
  = the gate chop. Tilt is the manual wub.
- **Strings** — the bowed ensemble: fat-saw with 12–28 cents detune, 0.10–0.26 s
  attack (bowed, not swelled — quicker than Pad's 0.25+), section vibrato,
  chorus 0.45–0.7 on nine rolls in ten, occasionally a thin HP chamber voicing.

Weights: a 32-entry table keeps the core nine ~three-quarters of rolls (pads
still the most common), so the bank's identity holds and the second wave stays
a discovery, not a takeover.

### The freeze mechanics (how this shipped without changing anyone's sound)

The seed contract now has *two* frozen engines. genver-2 devices re-derive
their o/p slots through `generateSound(seed)` on every load, so the update
adds `generateSoundV3(seed)` (the same paint engine over an expanded
`archetypeForSeedV3` table) rather than touching the v2 path:

- `archetypeForSeed`, the nine v2 paint windows, and `sanitizePatch` are
  byte-identical — and now **pinned by golden hashes** in `test_dsp.cpp`,
  exactly like the legacy engine (they previously weren't; the expansion
  closed that gap). The new archetypes live in new switch cases the v2
  selection can never reach.
- storage gates three ways: genver 1 → `generateSoundLegacy`, 2 →
  `generateSound`, ≥3 → `generateSoundV3`. genver moves to 3 only with a
  fresh seed (first boot, wiped NVS, Re-roll bank) — never on update.
- **Randomize** uses the expanded pool immediately on every device (fresh
  hardware seed each press; no continuity to preserve).
- **A pre-flight audit caught a dead-roll path before it shipped**: rendering
  120 held rolls per new archetype found near-silent patches (peaks down to
  0.001) — the *spice* twist flipping a pure-wave (sine/triangle) patch into a
  highpass/bandpass whose passband sits above the note's only partial. A
  second-wave-only polish (gated on the new archetypes, no RNG, so the v2 pool
  stays bit-exact) reverts that flip on pure waves — and refuses HP on wobble,
  whose sub carry is the whole point. Harmonic waves keep their spice. The
  measured floor went from 0.001 to 0.05+, and a held-note audibility test
  (peak ≥ 0.04 across forced rolls of all five) keeps it there. The v2 pool
  retains a rarer, milder version of this quirk; it is frozen and stays.
- **Naming can't relabel anyone**: `classifySound` and every word table are
  untouched. Each second-wave window is shaped to land in a frozen family
  whose words read right — whistle sings (autoVib ≥ 4) so it names from the
  lead bank ("golden-voice", "misty-siren"); wobble carries sub so it names
  as bass ("deep-rumble"); organ holds under 2.8 kHz and lands in the
  choir/cavern (or depth) banks; keys name from the generic bank; strings
  from pad or lead. `kFamNouns` gained five *reserved* rows (append-only,
  unreachable) against a future versioned classifier. The native tests assert
  the whole mapping per archetype, plus determinism, every guardrail, bounded
  rendering, and the audition-lick pitch landing across the V3 pool.

(Shipping note: pinning the v2 goldens surfaced a host-toolchain float trap —
the 32-bit mingw x87 default carries excess precision through one
hash-quantise product (`0.35f * 100`) and truncates 34 where the device FPU
rounds 35. `env:native` now pins `-msse2 -mfpmath=sse` so host hashes mean
the same thing the instrument computes.)

### The slot model (what changed, what didn't)

The 10 NVS slots and the `fn+q..p` / `fn+shift+q..p` gestures are **unchanged**.
The named factory bank still exists in firmware (the "roots") and is what
*Reset all sounds* and per-slot *Sound reset* restore to. What changed:

| | before | after |
|---|---|---|
| `w..p` at first boot | named factory instruments | **rolled unique per device** |
| `q` (home/boot) | GLIDE | GLIDE (unchanged anchor) |
| `*` next to a name | "you saved over this slot" | "your own sound (made or saved), not stock" |
| randomize | buried, ephemeral | foregrounded loop: Roll/Mutate/Undo/Keep |
| library size | 10 slots | 10 fast slots + unlimited SD |

**Migration safety (Hard Rule #3 — never wipe a save).** The first-boot bank
only fills slots on a *fresh* device (`prevBoot == 0`) and never overwrites a
slot that already carries an override; it runs once (`bankgen` sentinel). An
existing device updating to this firmware keeps its factory bank and every saved
sound untouched, and opts into the generative world deliberately via
**Re-roll bank**.

### The SD library

One `.gpat` file per patch under `/glide` on the card. The file *is* the tagged
`patch_codec` stream the NVS slots already use, so a card patch and a slot patch
are byte-compatible and both forward/backward-compatible across firmware
versions. Saved sounds get an evocative, deterministic name derived from the
sound's own hash (`warm-haze-3f`) — re-saving the same sound is idempotent.

The instrument is **fully playable with no card**: `sdstore::available()` gates
every call, failures are visible (the browser shows the error), and the ten NVS
slots are the always-works core. SD only *extends* the library.

## ⚠️ Hardware-unverified: the SD/SPI pins

This environment has no device build and no way to exercise SPI/SD. The SD pins
in `config.h` (`kSdSckPin/MisoPin/MosiPin/CsPin`) are the **standard
M5Cardputer** values; the **Cardputer ADV pinout may differ**. In the spirit of
the Phase 0 probe: confirm on a real unit before trusting SD. If saves don't
appear, the CS/clock pins are the first suspects — fix them in `config.h`.
Everything else (generation, mutation, history, the unique bank, NVS slots) is
device-independent and host-tested.

## Player-facing surface (settings → SOUND)

- **Randomize** — roll a whole new patch; auditions instantly.
- **Mutate** + **Mutate amt** — evolve the current sound a little or a lot.
- **Undo / Redo** — step back/forward through sounds you've had this session.
- **Init sound** — a blank slate to build from by hand.
- **Save to SD** / **Load from SD** — the personal library.
- **Re-roll bank** — nine brand-new slots (`w..p`); `q` stays GLIDE.
- **Sound reset / Reset all sounds** — back to the named factory roots.

Keep a sound the fast way with `fn+shift+q..p` (onto a slot); keep it the
unlimited way with Save to SD.

## Possible next steps (not in this branch)

- A text-entry rename for SD patches (the Cardputer has a full keyboard; today
  names are auto-generated). Mind Hard Rule #4 if reading the keyboard.
- "Breed" two saved patches (genetic crossover) as a third generation verb.
- Persisting *Mutate amt* to NVS (currently a session preference).
- A boot-time `sdstore::begin()` so SD status is known before the first save.
