# 29 — The diagnostic build: what the next lab firmware must capture

**Status: SPEC 2026-09-22, for the tester's own fork. Nothing here ships in
GLIDE.** Written so an agent working in a fork can build it cold, with no
other context than this repo at the commit named below.

**Audience:** Jordan (the tester) and the agent he points at his fork. Read
`CLAUDE.md`, then `docs/roadmap/28-field-data-round-1.md` — that doc is what
round one of your data produced, and it explains why every field below is
asked for.

**Base commit:** the tip of `master` that contains doc 28's two code
commits, `LISTEN lands the song, not the scale you were in` and `Gen V6: the
pool learns from 202 rated rolls`. Rebase your lab fork onto it. Round two
of ratings must hit the V6 engine and the new landing, or the numbers cannot
be compared to round one.

## Why a second round, and why audio this time

Round one (203 rated rolls, 47 logged listens) did two things: it tuned the
randomizer (V6) and it fixed the part of LISTEN that lives downstream of the
chroma (the landing and the space walk). It could not touch the part that
lives upstream — the Goertzel bands, the floor and harmonic subtraction, the
octave weights — because no audio was saved, and ten of the 47 misses are
"dominant heard as tonic" inside the chroma itself. With the audio, that
whole front end becomes a host regression against labelled songs
(`support/keylab_eval` already scores everything from the chroma onward;
its front-end twin gets written the day the first WAVs arrive).

So the diagnostic build has two jobs: **rate V6 the same way V5 was rated**,
and **record what the mic heard**. Everything else in this doc is detail.

## Ground rules (the same ones the main firmware lives by)

- **Rule 7 still applies to you.** The 65 KB frame buffer boots within ~1 KB
  of the RAM ceiling. No new statics. Anything the lab needs is `malloc`'d
  for a modal's lifetime and freed on every exit path. LISTEN sizes its
  record rounds from the largest free block — a lingering allocation
  silently shortens every round you log (round one's rounds were ~0.6 s for
  exactly this reason; see "round length" below).
- **SD is mounted once at boot and never cycled.** Open, write, close per
  file. FATFS costs ~4 KB per open file.
- **Keyboard is read positionally** (`keyList()`, codes `y*14+x`), never via
  the char. Rating keys are positional too.
- **Never block the render task with SD writes while a note can sound.** In
  LISTEN the render task is parked and the speaker is off, so writes inside
  the round callback are safe. In the roll lab, write the `.gpat` and the CSV
  line after the audition finishes (`audition::lengthMs()`), not during it.
- Failures must be visible: an SD write that fails shows a red HUD pill and
  the session continues. Silent data loss is worse than a missing row.

## Part 1 — the roll lab, round two

Same protocol as round one; the columns are a superset so the two rounds
join on `seed`.

**Hook:** `ui/settings_screen.cpp` `aRandomize(int)` — after
`store::applyGenerated(...)` and `soundcard::showRolled(...)`, the lab
records `sd`, `arch`, `dsp::styleForSeedV6(sd)` and `dsp::kGenVerNewest`,
then arms the rating prompt for after the audition. Everything about the
roll is reproducible from `(seed, arch, genver)` — `generateSoundV6(sd,
arch)` on the host regenerates it bit-identically — but save the `.gpat`
anyway: it is the proof, and `support/gpat_stats` reads it.

**Files:** `/glide/lab2/` on the card.

- `lab2.csv`, one row per press, header:
  `rating,verdict,preview_ok,played,seed,genver,arch,arch_name,style,style_word,name,classify_v2,note`
  - `rating` 1..4 = bad / meh / good / great, `verdict` the word.
  - `preview_ok` 1 if the audition lick represented the sound, 0 if it
    played silent or misleading (the bell/whistle complaint from v3.2).
  - `played` 1 if you played it on the keys before rating (please do;
    round one had 2 unplayed rows).
  - `seed` 8 hex digits, `genver` = 6, `arch` = the painted archetype
    index, `style` 0..2, `style_word` from `ui/sound_card.cpp`
    `kStyleWord` (empty for classic / wild / drone / gate).
  - `classify_v2` = `dsp::archetypeName(dsp::classifySoundV2(synth))` — the
    name the sound gets, next to the family that painted it. Round one
    found 17/202 disagreements; V6 must not make that worse.
  - `note`: free text, optional, no commas (write `;` instead). "too
    quiet", "clicks on release", "sounds like a pad" — anything. Round one
    had no notes and we guessed.
- `<rating><verdict>-<seed>-<name>.gpat`, exactly as round one.

**Rating keys:** keep whatever round one used; the numbers only need to be
consistent. If a fifth key is cheap, add `0` = "skip, don't log" for
accidental presses.

**Protocol:** 200 presses minimum, the same room and volume as round one,
rate after playing at least a few notes. Do not rate the name. Do not skip
families you dislike — the sinks are what we tune.

**What round two is checked against:** `support/gpat_stats/roll_diff`
(what V6 changed on the rated V5 rolls), then the same per-archetype table
as doc 28. The target is not a number: chip, wild, pad and whistle stop
sitting at the bottom while lead stays where it was.

## Part 2 — the key lab, round two: the chroma AND the audio

**Hooks, all in `ui/listen_screen.cpp`:**

- `onSegment(user, mono, n)` runs once per round, after the round's samples
  are fully in `mono` and before the next round records into the same
  buffer (`io/listen.cpp` guarantees this ordering). This is where the WAV
  and the per-round chroma row are written. The speaker is off and the
  render task is parked for the whole capture, so the write costs only
  wall-clock, which the capture budget already tolerates (rounds are not
  contiguous today either).
- The apply path after `listen::capture(...)` returns — where the
  `[listen] chroma ...` and `[listen] raw ...` serial lines are printed —
  is where the per-listen row is written: it already has `ctx.guess`, the
  six alternates from `dsp::listenAlternates`, `ctx.rounds`, the tempo
  guess and `appliedBpm`.
- The space-key cycle (the `[listen] nudge` serial line) is where the
  player's chosen alternate is logged if they pick one.

**Files:** `/glide/keylab2/`.

- `keylab2.csv`, one row per listen, header:
  `song,section,true_key,true_bpm,note,rounds,audible_rounds,heard_s,round_len_s,conf,raw_key,raw_minor,tonic,mode,modal,tiebreak,safe,land_scale,land_root,g1,g2,g3,g4,g5,g6,picked,tempo_bpm,tempo_conf,tempo_applied,C,C#,D,D#,E,F,F#,G,G#,A,A#,B,title`
  - `song` an integer you increment; `section` free text (`verse`,
    `chorus`, `intro`, `solo`) — **please listen to each song at least
    twice, at different sections**, so temporal variance is measurable.
  - `true_key` as round one (`F major`, `D dorian`, `E mixolydian`); add
    `true_bpm` when you know it (a metronome app is fine), else empty.
  - `note`: free text, no commas. Especially: `power chords`, `no third`,
    `modulates`, `bass only`, `phone speaker`, `across the room`, `loud
    room`. Round one's disputed labels were guessable only from titles.
  - `rounds`, `audible_rounds`, `heard_s` (`ctx.heardSamples / 16000`),
    `round_len_s` (`n / 16000` for the round size this listen used — it
    depends on the largest free block, and round one's was ~0.6 s; the
    shipped build on a clean heap gets 3 s).
  - `conf` = `ctx.guess.confidence` (song-aware since doc 28), `raw_key`
    the K-S winner, `raw_minor` 0/1.
  - `tonic`, `mode` (`MAJ`/`DOR`/`MIX`/`MIN`), `modal`, `tiebreak`, `safe`
    from `dsp::landListen` — the primary landing's truth.
  - `land_scale`, `land_root` = the applied landing (`kScales[].shortName`,
    note name). `g1..g6` the six alternates as `root:short` (`A:mpent`), in
    the shipped order: primary, twin, runner-up, runner-up, pent, blues.
  - `picked` = the alternate index the player kept (0 = the primary), or
    -1 if the card was dismissed without cycling.
  - `tempo_bpm`, `tempo_conf`, `tempo_applied` (0/1) — the beat detector
    has never had labelled data.
  - the 12 chroma bins = `ctx.guess.chroma` (peak-normalized), `title` last.
- `keylab2_rounds.csv`, one row per **audible** round, header:
  `song,section,round,len_samples,wav,C,C#,D,D#,E,F,F#,G,G#,A,A#,B`
  - the chroma here is the ROUND's own normalized vote
    (`accumulateChromaNormalized` on that round alone), not the running
    sum — that is what lets the host replay the accumulation order.
- `NNN-SS-rM.wav`: the round's audio. 16 kHz, mono, int16 PCM, a plain
  44-byte RIFF header; `NNN` = song, `SS` = section index, `M` = round.
  ~96 KB per 3 s round; a 47-song session at two sections is well under
  100 MB. Write it from `mono` inside `onSegment` before returning.
  Silent rounds (`!dsp::segmentAudible`) are skipped like today — log
  nothing for them, they carry no evidence.

**Protocol:** the same 46 songs as round one if you can (the comparison is
the point), plus anything that fooled it in daily use. Two sections each.
Hold the device where you actually play it. Do not chase a lock: hit
`fn`+`k`, let it stop when it stops, write down what it said, and only
then cycle `space` if you want to see the alternates — `picked` is what you
KEPT, not what you looked at.

**What round two is checked against:** `support/keylab_eval` on
`keylab2.csv` (the same scoreboard as doc 28), and a front-end replay tool
over the WAVs that will be written when they arrive: it re-runs
`accumulateChroma` on the host from the audio and must reproduce the
logged round chromas bit-for-bit before any front-end constant is touched.

## Part 3 — session context (once per boot, `/glide/lab2/session.csv`)

One line per boot of the lab firmware:
`boot_time_ms,fw_version,genver,device_seed,free_heap,largest_block,round_len_s,sd_free_mb`

`fw_version` = `cfg::kVersion` plus your lab tag; `genver` from storage;
`device_seed` = `store::deviceSeed()` (it only identifies the unit — this is
a tester's device); heap numbers from `heap_caps_get_free_size` /
`heap_caps_get_largest_free_block` on the internal heap. This is the row
that explains a run whose rounds came out short.

## What NOT to build

- No changes to `dsp/`, `storage/`, the landing, the generator or the
  codec. The lab observes; it never alters what it observes. If the lab
  needs a hook the shipped code lacks, add the hook as a no-op call in
  your fork and say so in the notes — do not fork the algorithm.
- No settings rows, no new gestures in the shipped keymap. Rating keys and
  the lab's own prompts are fine because the lab firmware is yours alone.
- No wifi, no upload. The card comes here.

## Deliverable

Zip `/glide/lab2/` and `/glide/keylab2/` off the card with the session
file, and the lab fork's commit hash, into `Jordan's Testing/round2/` on the
main machine (that folder is gitignored; it never leaves). The host tools
then produce: the per-archetype rating table before/after, the LISTEN
scoreboard before/after, and — new — the front-end replay.

## Later: making the diagnostics public

If this works, the same capture could ship in GLIDE behind a hidden
"diagnostics" toggle so any player can send a card's worth of evidence
when something sounds wrong. That is a separate doc with a real UI price
(one settings row, a privacy line in the manual, and an on-screen consent
to write audio to the card). Not now: prove the pipeline on one tester's
fork first.
