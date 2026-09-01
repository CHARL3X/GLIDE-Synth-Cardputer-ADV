# The GLIDE manual

Everything the instrument does and how to play it. For install and the five-minute intro, see the [README](../README.md); for the design story and internals, see [design.md](design.md).

## The keymap

```
 string 3 (hi) |  1  2  3  4  5  6  7  8  9  0  |  - oct-   = oct+   bksp PANIC
 string 2      |  q  w  e  r  t  y  u  i  o  p  |  [ bend-  ] bend+  \  tap tempo
 string 1      |  a  s  d  f  g  h  j  k  l  ;  |                   enter tilt
 string 0 (lo) |  z  x  c  v  b  n  m  ,  .  /  |  space sustain

 `     restart (HOLD ~0.7s)  fn (hold)    quick-edit layer   [see note below]
 tab   settings             shift (hold) momentary chromatic
 ctrl/opt volume -/+ (left thumb)        alt loop pedal (left thumb)
 - / =    octave -/+          G0 (RIGHT trigger, top edge) = trigger macro (muffle)
                                         (tap rec/play/dub, hold clear, fn+alt undo)

 fn + q..p         : switch between the ten sounds, live
 fn + shift + q..p : save your current tweaks over that slot
 fn + 1..0         : pick a parameter, [ ] to adjust
 fn + k            : cycle the key (root) up a semitone, live
 fn + s            : cycle the scale, live
 fn + k  (HOLD)    : LISTEN - the mic hears the song and retunes for you
```

**A note on the two ways out.** Holding `` ` `` saves your work and reboots —
but the Cardputer boots straight back into GLIDE, so what you actually get is
the splash again, not Launcher. To reach Launcher, press the **left trigger on
the top edge, marked `BTN RST`**, and tap any key as the device comes up. (The
*right* trigger is G0, GLIDE's own trigger macro, and never restarts anything.)

## How you play it

- Press keys. It sounds good immediately. Scale lock is on by default (A minor pentatonic, degree-mapped: every key is a scale tone, no dead keys, and sliding a shape sideways is a diatonic transposition). You can't really hit a wrong note. That's on purpose. It's the same thing that happens when you connect the pentatonic boxes across a guitar neck.
- **Hammer-on:** press a new key on the same row while holding one and the voice glides there. **Pull-off:** release it and the voice glides back. Each row behaves like a real string.
- **Slide a chord:** hold a shape across rows, then re-finger it elsewhere while the old notes still ring. Every voice glides. This is the thing.

  <img src="../assets/glide-slide.svg" alt="The slide: hold one key, tap another on the same row and the voice glides up to it; release and it glides back. The bottom row latches a chord progression under you." width="100%">
- **Hold `shift` to break out of the scale.** Pure chromatic semitones, only while held. That's the skill gate. The scale keeps beginners safe; shift is how you earn the notes in between.
- **Match a song's key on the fly with `fn`+`k`.** Each tap walks the root up a semitone (wrapping at B), so you can step the key, play a phrase against whatever's on, and step again until it locks in, with no trip to settings. The current key shows on the status bar and flashes in the HUD on every tap.
- **Change the mood with `fn`+`s`.** The same audition loop for scale color: each tap walks the scale table (pentatonics, the modes, blues, exotics — the HUD names each in full), held notes keep ringing, and new notes land in the new scale. Key and scale together are the whole "play along with anything" gesture, and they live under one finger.
- **Or let the instrument find it: hold `fn`+`k` and it LISTENS.** The synth goes quiet while the mic hears whatever's playing in the room, in ~3-second rounds, stopping the moment it's sure and listening up to ~9 s when the song is being coy (a single round can land on one chord and name *its* key; more rounds hear the changes). A chromagram works out the song's key (root *and* major/minor), and the instrument retunes itself. It hears the *mode*, not just major-or-minor: the degrees that separate Dorian from minor and Mixolydian from major are read straight from the capture, and a two-chord vamp that fools the textbook reading (an Am7-D9 groove scores as "D major" as honestly as "A minor") gets its tonic re-seated where the song actually lives. The verdict then lands in YOUR scale family: the plain seven-note scales play the detected mode itself; pentatonics swap flavor and sit on the tonic; Blues is never switched, only re-centred (the tonic over minor, Dorian, and dominant grooves, the relative-minor boxes trick over a plain major song); other exotics keep their flavor and take the relative root as always. Weak evidence changes nothing, since mode and tonic corrections sit behind stricter gates than the key itself. The same capture reads the song's tempo from its onsets: a confident beat sets the jam clock (the synced delay and LFOs follow), a beatless room leaves it untouched, and the card shows the locked BPM. And when a song refuses to pick a side — it audibly plays *both* sixths, or a note your scale asserts is one the song contradicts (a Lydian #4, a Phrygian b2) — the landing retreats to the pentatonic at the tonic, which simply omits the clash note: when unsure, play fewer notes rather than a wrong one (the card says "clash heard - safe pent"). While it listens you watch the twelve pitch-class bins fill in real time, pulsing as each round lands, with its forming verdict underneath. The result card shows the bars it heard, the detected key and mode, a confidence meter, and the applied root, with an amber strip marking which notes your applied scale contains; a weak or silent room says NO SIGNAL and changes nothing. The card is a playing surface, not a wall: it holds for about six seconds and the keyboard stays live underneath it, so you can play the key it just named while you are still reading the verdict — a running loop and any jam motion keep going too. If the verdict is *close but not quite*, tap `space` while the card is up to cycle the second guesses (the sibling mode, the safe pentatonic, the relative twin — each applied live as you cycle, and each press handing you a fresh six seconds to judge it); `` ` `` or `enter` keeps what's showing and dismisses early, and tapping `fn`+`k` again re-listens. (Cardputer ADV only, since it needs the mic; no mic just means a visible "mic unavailable", never a broken instrument.)

  <img src="../assets/glide-listen.svg" alt="LISTEN: a song plays in the room, hold fn+k, the mic works out the key, mode, and tempo, and the instrument retunes itself" width="100%">
- **`fn` + top row** picks a parameter (glide, ADSR, wave, cutoff, voices, bend range, volume); `[` `]` adjust it live. Nothing is hardcoded. Every sound parameter has a control, and everything survives a reboot.
- The **oscilloscope** is live. That's the actual output waveform, with a phosphor afterglow. The note readout tracks the lead voice in cents *through* glides and bends, so you can see exactly where you are between the notes.
- Or flip the display to the **pitch trail** (settings → *Display*): the lead voice's pitch drawn over time, scrolling across ~7 seconds, with root-note gridlines as fret markers. On an instrument about the space *between* notes, this is the scope for the other axis. Every glide, hammer-on, and bend becomes a visible curve (bend-pulled segments draw amber), and with tilt-vibrato on you can watch the line shimmer.

## Your sounds are yours

This is the other half of GLIDE, and arguably the bigger one. The factory bank is a starting point you grow past.

Ten slots live on `fn`+`q`..`p`. Eight are a curated bank, led by **GLIDE** on `q` (the home/boot sound) and **ACID** on `w`. The last two, `o` and `p`, are **generative**: rolled from a seed unique to your unit, so they're different on every device on Earth. From there you build your own.

| key | sound | character | tilt (per-sound mode) |
|-----|-------|-----------|-----------|
| q | **GLIDE** | the signature saw, now with a body: synth brass that swells into each note. the literal boot tone | filter (roll: vibrato) |
| w | **ACID** | resonant squelch. lean into it, tilt is the wah | filter (full) |
| e | **Organ** | drawbar organ with a leslie shimmer. holds a chord forever and sits *under* a solo — the bed | filter (roll: vibrato) |
| r | **Taser** | open saw + sub. gets *darker* as you play up, and leaning swells the echo | vibrato (roll: filter) |
| t | **Crisp Horn** | bright reed horn that sings its own vibrato without you leaning | filter (roll: vibrato) |
| y | **Fat Square** | punchy square, bright per-note filter bloom, attack knock | filter |
| u | **Hollow** | driven square through a *notch* filter, phasey and hollow | volume swell (roll: filter) |
| i | **Big** | highpass square ringing at the corner: hollow and enormous at once, on a 1/4 echo | filter |
| o | *generative* | rolled unique to your device, yours alone | (rolled) |
| p | *generative* | rolled unique to your device, yours alone | (rolled) |

A `*` in the `fn`+`q..p` list marks a slot holding *your own* sound. A `*` on the top status bar means the live sound has **unsaved edits** (shift-save to keep them). *Sound reset* restores one slot; *Reset all sounds* brings the whole bank back.

The bank is just the floor. The point is **rolling your own**:

<p align="center">
  <img src="../assets/glide-roll.svg" alt="Rolling a sound: one tap of Randomize commits to a character, paints every parameter inside that character's bounds, and lands a named, playable sound" width="100%">
</p>

- **Randomize.** A whole new patch in one tap, and a whole new *kind* of patch. Every roll first commits to a character (a pluck that stops, a bell that rings, a pad that swells, a sub-heavy bass, an acid squelch, a singing lead, a brass swell, a chip trill, a breathy slide whistle, a rotary organ, a tine piano, a tempo-locked wobble bass, a bowed string section, or pure chaos), then paints everything inside that character's musical bounds, with guardrails so a roll is always playable: never dead, blown out, or warbling off-key. Roll till you love one.
- **Mutate** (with **Mutate amt**). Don't start over, evolve what you have. A gentle mutate is a neighbour, same character nudged. A wild one rewrites it. Sculpting toward a vibe instead of pulling a slot machine.
- **Undo / Redo.** Every roll, mutate, and init checkpoints first, so you can always step back to the sound you just had. Experiment without ever trashing a keeper.
- **Init.** A blank, neutral sound to build up by hand.

Every action auditions on the spot with a short fixed lick, so you can A/B two rolls by ear. It all opens *first* in settings, as two big **Randomize** and **Mutate** buttons at the top of the **CREATE** section. (Settings is a collapsible accordion now, with only CREATE unfolded on open so the whole map fits at a glance.)

**Keeping what you find, two ways:**

- **Fast:** `fn`+`shift`+`q`..`p` saves the live sound onto one of the ten slots. Your quick-access favourites.
- **Unlimited:** **Save to SD** writes the sound to the microSD as a `.gpat` file. It asks what to call it, with the sound's own auto-name (`warm-haze-3f`, `frost-choir-1a`) already in the box, so `enter` keeps the rolled name, or you type over it and the sound is called whatever you want. **Load from SD** browses your whole library back (and renames anything there later). The card holds as many sounds as you'll ever roll, they're named so they read as *yours*, and because every file uses the same tagged format as the slots, the library survives firmware updates and travels card-to-card. (No card? The instrument still plays perfectly. SD only grows the library past ten.)

**Re-roll bank** resets the slots to the curated presets and rolls fresh randoms for `o` and `p` from a new seed. New sounds whenever you want them, presets intact. *Reset all sounds* is the way back without changing the seed.

> The seeded generator lives in `dsp/sound_gen`: pure, deterministic, and host-tested, same as the synth voice. See [random-sound-generation.md](random-sound-generation.md) for the design (and the note on the hardware-unverified SD pins).

Under the hood every sound rides five engine character-makers: a paraphonic **filter envelope** (retriggered by fresh attacks, never by legato hand-offs, so slides stay smooth), a **sub-oscillator**, env-gated **noise**, **drive** into the soft clipper, and built-in **vibrato**. All of it editable live and saved per slot.

## Tilt

The gyro debate, resolved as agreed, then promoted, because in practice it's fantastic. Tilt is an *assignable* effects modulator, toggled with `enter`, and **never pitch bend** (nobody wants to lean the instrument over again).

<p align="center">
  <img src="../assets/glide-tilt.svg" alt="Tilt: lean the device forward and back to morph between the live sound and the last one; left and right adds vibrato; enter toggles; never pitch bend" width="100%">
</p>

- **Your rig, or the sound's.** By default the tilt map is *global*: forward/back and left/right each hold a route that follows your hands across every sound instead of resetting per patch. Out of the box that's **Morph on forward/back** (lean into the sound you were just on) at 90% and **vibrato on left/right** at 60%. The morph axis is deep on purpose: at 60% a lean only half-arrives at the other sound, which reads as a wobble instead of a blend. Set it once and play. Flip settings → *Tilt map* to **per sound** and each patch carries its own route and depth instead (ACID into a full wah, Taser into vibrato, per the table above), saved with the slot.
- **Depth** (settings): how hard the motion drives the effect, 0 to 100%.
- **Center calibration** (settings → *Tilt center*): "flat" becomes wherever *you* hold the thing, not wherever gravity says. Set it while holding the device in playing position.

## The layering jam (drones)

The brainstorm's "one hand plays the backing, the other solos over it," solved the way continuous-pitch instruments always have: with **drones** (sitar, bagpipes, hurdy-gurdy lineage). Settings → *Jam rows*:

- The bottom one or two rows become **tap-to-latch drones**. Tap a key and it rings an octave down, hands-free, until you tap it again. Lay down a root, or root and fifth, and solo on the rows above.
- Drones are protected. They don't count against the lead's voice cap, and chord-slide stealing can never grab them. Your backing survives anything your solo hand does.
- The backing is *pitch-stable*: bend keys and tilt vibrato move only the solo layer. Your fretting hand bends strings; the open strings keep droning. (Drones do keep the patch's own built-in vibrato. That's part of the sound.)
- Release one and it fades with a long tail instead of stopping dead under your solo.
- The backing is visible. Latched drones show **amber** on the mini grid-map (held leads stay green) with a `+n` count, and jam motion blinks each struck key white on the beat so you can watch the arp walk. The `vox` counter shows leads against the cap only, since drones never count.
- Octave shifts sweep the drones along with everything else. Panic (bksp) clears them.

On by default (bottom row) with the **progression** motion ready, so out of the box you tap a chord loop on the bottom row and solo on the three above. Turn *Jam rows* off for a plain uniform grid.

## The loop pedal

The other half of "one hand backs, the other solos": **alt** (left thumb, since space already covers sustain) is a one-button looper. What it records is the *performance*, not the audio. The note events themselves, replayed through the live engine.

- **tap**: start recording. **tap again**: the loop closes and plays on that press. **tap again**: overdub a layer; once more seals it.
- **hold** (~0.7 s): clear the whole loop and start over. It's performance state, so there's no confirm; the hold just sits far enough past a normal overdub tap that a lingering thumb can't nuke the take.
- **fn + alt**: peel the last overdub (undo). Repeat the chord and it walks back up the stack; the gesture bounces at the ends, so it undoes to the base take and redoes to the top. The base loop is protected. You only ever peel the dubs you stacked on it. The annunciator shows the audible layer count (`x3`, or `x2/3` while peeled).
- **panic** (bksp) silences the loop but keeps the take. Tap alt and it plays again.
- The hint line goes loop-aware while a take exists (`alt dub  hold clear  fn+alt undo`), so the gestures are always on screen.
- Because the loop is events, it costs kilobytes. The good part: it **plays through whatever sound is selected**. Record an Organ line, switch to Taser, solo over it. Swap sounds mid-jam and the whole arrangement re-voices itself. Recorded slides, hammer-ons, and octave sweeps replay as slides, hammer-ons, and sweeps.
- Loop playback is a protected backing layer like the drones. Its voices ride outside the voice cap, can't be robbed by chord-slide stealing, ignore live bends and tilt vibrato, never hijack the note readout, and survive sound switches and settings trips. Internally it plays on its own string lanes (4 to 7) with its own key ids, so it can never collide with your hands.
- Timing belongs to the audio thread. Playback events are *scheduled* (block-accurate, ~4 ms), not fired from the ~33 ms UI frame, so the loop doesn't swing with the frame rate.
- **The loop locks to the jam clock.** The tap that closes a take snaps its length to the nearest **bar** of the *Jam tempo* (minimum one bar; a tap 40% into bar one was meant as a 1-bar loop), so the loop and the auto-progression share one clock instead of drifting apart a little more every cycle. Notes played just past the bar line wrap to the downbeat; a note still held at the close rings to the loop end. Settings → *Loop snap* picks `bar` (default), `beat`, or `off` for the old free-time behaviour.
- Status sits top-left of the scope: **REC** blinks red with elapsed time, **LOOP** green with a cycle-progress bar, **OVR** amber while layering, dim `LOOP --` for a stopped take. `FULL` means the take hit the 1024-event ceiling.

Loops are performance state. They live until cleared or power-off, and never hit flash.

## The chord progression (the easy way to back yourself)

The loop pedal records a *performance*, which means your timing has to be right, and a loop is one phrase, not a chord change. The drones fixed the timing problem (tap to latch, no rhythm) but a drone is one chord forever. The **auto-progression** is the missing middle: a soft chord progression you spell with no timing at all, then solo over. Settings → *Jam motion: progression* (needs *Jam rows* on):

- **Tap the chords in order on the jam row. That's it.** Each tap appends a step (repeats allowed: I-IV-V-IV is four taps). No pocket to hit — and if you want a click to build against, the metronome (`fn`+`\`) locks to the same clock. The HUD confirms each one (`PROG  3: E`).
- **The sounding chord names its harmony.** The PROG readout boxes the current step and adds its Roman numeral (`F# vi`): uppercase major, lowercase minor, `°` diminished — the progression teaches itself as it plays, in the same system every theory book uses.
- The beat clock walks the steps **one chord per bar**, looping, at the *Jam tempo*. *Chord length* sets the beats per chord. The backing glides from chord to chord (of course it does) and re-blooms each bar, so on a pad or strings patch it's a soft wash you solo straight over.
- Each step is a **diatonic triad** built from the current scale: real major/minor/dim color, and always in key. The same "you can't hit a wrong note" guarantee the melody gets, now for the backing too. (Hold `shift` while tapping a step for a chromatic power-chord voicing instead.)
- It's a protected backing layer like the drones and the loop: cap-exempt, steal-proof, ignores your bends and tilt vibrato, and **re-voices through whatever sound you switch to** mid-jam. Lay down Organ, solo on Taser.
- The progression is on screen: a `PROG  A  D  E  ▸` strip across the top of the scope with the current chord boxed, and its root outlined on the grid-map so you can watch the changes walk.
- **bksp (panic)** clears the progression to start over, the same gesture that clears the drones. Like them, it's performance state and never hits flash.

Pick Organ, Hollow, or Big for the bed, set a slow tempo, tap four chords, and you've got a song to solo on in about ten seconds.

## Soloing over the jam: a separate register and sound

Once the backing is looping, you don't want to be stuck in its octave or its sound. You want to *solo* over it. So the moment you change the solo while a jam runs, the backing holds its ground:

- **Different register.** Shift octave (or even change key/scale) and only your **solo** moves. The progression keeps looping in the register and key it was built in. Build a progression low, solo two octaves up over it.
- **Different sound.** Switch patches (`fn`+letter) over a running jam and the backing freezes onto the sound it was playing while the new patch becomes your solo voice. Lay down an Organ progression, flip to Crisp Horn, and wail over it. The organ keeps holding. An amber **`LK`** by the octave readout (and a `SOLO` flash on the switch) tells you the split is engaged.
- **Their own voice, a shared room.** The backing and the solo each keep their own oscillator, filter, envelope, and drive, but they wash into one shared reverb/delay space (the solo patch's), so the whole thing sits together instead of sounding like two unrelated machines.
- **`bksp` (panic)** clears the jam and drops the split. The next sound switch goes back to changing everything, as normal.

No new gesture to learn. Start the jam, then change your sound. The split appears when you need it and disappears when the jam's gone.

## Tempo, the synced delay, and the live FX rack

One tempo (the *Jam tempo*) drives both the progression and the echo. Two things make a solo over that backing sound produced:

- **Tap tempo**, on `\`, right on the keyboard, so you can match a song's groove without leaving the instrument. Tap it in time and the BPM follows your hand; the HUD reads back the tempo on every tap. A single tap after a pause only *reports* the tempo, so a stray press can't move anything; it takes two taps to make a beat. (Also in settings → *Tap tempo*, tapped with `,` or `/`; it's the same series either way, so you can start in one place and finish in the other.)
- **The metronome lives on the same key: `fn`+`\` clicks it on and off.** A soft wood-block pulse, timed on the audio engine itself (not the screen), with a brighter tick on beat 1 of the bar. It locks onto your tap-tempo taps as you make them and onto the progression's chord changes, so click and backing land together. `fn`+`ctrl`/`opt` sets its volume (the same thumb keys that do master volume, one layer up); settings → *Metronome vol* holds the level between sessions. The click never records into the looper and always starts quiet on boot.
- **Or let LISTEN set it by ear.** Hold `fn`+`k` at a song and the detected key arrives with its tempo: when the beat is confident the jam clock takes the BPM, and everything synced to it follows. A weak or absent beat moves nothing.
- **Tempo-synced delay** (settings → *Delay sync*): lock the echo to a musical division (`1/4`, `1/8.` the dotted eighth and the Edge/Gilmour trick, `1/8`, `1/8T`, or `1/16`) and every repeat lands on the beat. Taser and Big ship with it on; switch to Taser over a progression and the repeats cascade right in the pocket. (Set it to `free` for a plain ms delay.) If a division is too long for the delay line at a slow tempo, it folds down an octave so it stays on the grid instead of clipping.

The whole **send-FX rack is live on-device** (settings): *Chorus*, *Delay send / time / sync / feedback*, *Reverb send / size*. Dial the space to taste and `fn`+`shift`+letter saves it with the slot, like every other sound parameter. The effects were the one thing you couldn't reach before. Now nothing about the sound is off-limits.

## The modulation matrix (get far from the default)

Tilt was the first assignable modulator. Now there's a whole rack of them, which is exactly how two players with the same device end up with sounds that share no DNA. Settings → *MOD SOURCES* and *MOD MATRIX*:

- **Two LFOs**, each with a shape (sine / tri / saw / square / **S&H** random) and either a free rate in Hz or a **tempo-sync** division locked to the *Jam tempo* (same `1/4` to `1/16` vocabulary as the delay), so a wobble or a filter sweep breathes in time with the progression.
- **A second envelope** (attack / decay) that retriggers on each note.
- **Six routing slots.** Each picks a *source* (LFO1, LFO2, mod-env, key-track, bend), a *destination* (pitch, cutoff, resonance, amp, filter-env depth), and a bipolar *amount*. Six slots across those sources and destinations is a huge sound-design space: vibrato, tremolo, auto-wah, growl, evolving pads, random steppers, all from a handful of primitives, all on the lead voice (the backing bed stays steady underneath). Everything defaults to off, so a fresh patch is the original GLIDE tone until you wire a slot.

**Filter modes** (settings → TONE → *Filter mode*): the filter does **lowpass** (the original voice), **highpass** (thin/airy), **bandpass** (vocal/telephone), and **notch** (hollow/phasey), free, because the filter already computes them all.

**Drift** (settings → TONE → *Drift*): every voice wanders in pitch on its own slow random walk, a few cents either side. It is the imperfection that separates a warm analog synth from a sterile digital one — real oscillators never sat still, and a chord whose notes are *mathematically* identical is a chord your ear reads as fake. It ships on, subtly, because a feature nobody finds is a feature nobody has; turn it to **off** for dead-still digital, or up to 12 cents for something that has not seen a service in years. It is per-voice, so a held chord shimmers against itself rather than sliding as a block. The note readout deliberately does not wander with it.

Every one of these saves with the slot (`fn`+`shift`+letter) and survives a reboot. And because patches use a **tagged format**, adding the next knob, or the one after that, will never wipe the sounds you've already saved. Expansion is the point. The further you get from the default, the more the instrument is *yours*.

### The two motion macros

Most G0 actions are a *throw* — you shove the sound somewhere and it stays there
while you hold. Two of them **move on their own**, locked to the instrument's
tempo, so they work with the jam instead of across it.

- **wah** — a resonant peak sweeps 350 Hz to 2.4 kHz, one sweep every two beats.
  It is a real wah, not a tone control: at full depth the filter belongs to the
  pedal, whatever the patch's own cutoff was, and the Q goes right up so the
  peak sings. Both layers sweep, so a held drone breathes with your solo.
- **gate** — the whole instrument is chopped on sixteenths, reverb tail and all.
  Depth sets how deep the chop cuts, from a pulse to a hard stutter. The
  metronome is deliberately not gated; a stuttering click is a broken click.

Put either on **latch** and it keeps running with your hands free — hold a chord,
tap G0, and play over your own moving texture. Depth is the whole range between
"a hint" and "the point", and both sit still at 0%, so an unpressed button
changes nothing.

Tempo comes from the same place the jam and the synced delay read it, so tap
tempo (`\\`) or the BPM setting moves the sweep and the chop with it.

## Every parameter

| param | range | default | where |
|---|---|---|---|
| glide time | 0-2000 ms | 120 | fn+1 |
| attack / decay / sustain / release | 0-2s / 0-2s / 0-100% / 0-3s | 5ms / 120ms / 70% / 250ms | fn+2..5 |
| waveform | sine, tri, saw, sqr, fat, pwm | saw | fn+6 |
| cutoff / resonance | 80-12k Hz / 0-95% | 4k / 30% | fn+7 / settings |
| voices | 1-8 | 6 | fn+8 |
| bend range / bend time | 1-12 st / 50-1000 ms | 2 st / 250 ms | fn+9 / settings |
| volume | 0-100% | 70% | ctrl/opt (left thumb) or fn+0 |
| root / scale / row interval | C-B / 13 scales / 1-12 st | A / min pent / 4th | fn+k / fn+s (live), settings |
| glide mode | legato-only / always | legato-only | settings (per sound) |
| allocation | strings (mono rows) / free poly | strings | settings |
| jam rows (drones) | off / bottom / bottom 2 | bottom | settings |
| jam motion | sustained / pulse / arp / progression | progression | settings |
| jam tempo / chord length | 40-240 bpm / 1-8 beats | 100 / 4 | settings |
| loop snap | off / beat / bar | bar | settings |
| octave keys | sweep (glide) / re-strike | sweep | settings |
| trigger action / depth / mode | muffle, brighten, pitch dive, drive grit, synth morph, wah, gate / 0-100% / momentary, latch | muffle / 70% / momentary | settings (right trigger, G0) |
| sound slots | 10 (q=GLIDE, w=ACID, e..i curated, o/p generative per device) | curated + 2 rolled | fn+q..p, fn+shift+q..p |
| generate | randomize / mutate (+amount) / undo-redo / init / re-roll bank | live | settings (CREATE) |
| SD library | save / load / delete named .gpat patches (unlimited) | live | settings (LIBRARY), browser |
| filter env (atk/dec/depth) | 1ms-2s / 10ms-2s / 0-3+ oct | per sound | settings, saved in sound |
| sub / noise / drive / auto-vib | 0-1 / 0-1 / 1-8 / cents | per sound | saved in sound |
| chorus / delay / reverb send | 0-100% each | per sound | settings (live) |
| delay time / sync / feedback | 10-600ms / free+5 divisions / 0-90% | per sound | settings (live) |
| tap tempo | 40-240 bpm, tapped | live | `\` key, settings |
| metronome | on/off + 0-100% volume | off / 60% | fn+`\`, fn+ctrl/opt, settings |
| tilt map | global (follows your hands) / per sound | global | settings |
| tilt routing (f/b + l/r) | off / cutoff / vibrato / volume / morph | Morph f/b + vibrato l/r | settings, enter toggles |
| tilt depth | 0-100% | 90% morph f/b, 60% vibrato l/r | settings |
| tilt center | calibrated "flat" | 0 | settings (hold + set) |
| display | waveform scope / pitch trail | pitch trail | settings |
| theme | 10 palettes + **custom** | cassette | settings |
| custom: hue / accent / vividness / ground / contrast | full circle / angle from hue / 0-100% / black..bright / 0-100% | fitted to the palette you left | settings (only while theme = custom) |
| screen idle | off / dim / dim + screensaver | dim + screensaver | settings |
| solo/backing split | auto when you change sound/octave over a jam | live | live |

### Making the palette yours

Ten palettes ship with the instrument. The eleventh, **custom**, you turn yourself.

It is not eleven colour pickers — it is five dials, and the rest follows:

- **Hue** — the instrument's colour. Turn this one dial and the whole palette
  rotates with it, because everything else is defined *relative* to it.
- **Accent** — the angle from that hue to the annunciator colour. The row names
  the relationship (*same*, *near*, *wide*, *triad*, *split*, *opposite*) rather
  than a number, because the relationship is what you are choosing, and it
  survives turning Hue.
- **Vividness** — how saturated the whole thing runs.
- **Ground** — the stock, from *black* through *dusk* and *ash* up to *paper* and
  *bright*. Cross into the light half and GLIDE flips its whole colour model:
  ink pools instead of light summing, and every screen stays right.
- **Contrast** — how far the ink sits off the stock.

Two things worth knowing. Cycling onto *custom* from a palette you liked opens
it as a **copy of that palette**, so you start from something good rather than
from a jarring default — nudge it from there. And **Roll look** rolls a whole
coherent palette at once, the same bargain as the sound randomizer: you do not
have to want to design anything to end up with an instrument that is yours.

The dials appear only while the theme reads *custom*; nothing changes for
anyone who never opens it. You cannot make GLIDE unreadable with them — every
combination is held to a minimum contrast against the stock, so the worst you
can do is a palette you don't like, never one you can't play. And because the
whole recipe lives in the device's own storage rather than in the firmware, it
survives every update: a custom palette is yours to keep.


## The instrument teaches you

Real players kept missing the gestures above — `fn`+`k`, `fn`+`s`, the LISTEN hold, even Randomize — for weeks, because a pocket instrument has no manual in your hand. So GLIDE now teaches with *your* hands, three ways:

- **The tour.** A ~60-second playable ritual: a banner along the bottom asks for one gesture at a time (press a key → the slide → `fn`+`k` → `fn`+`s` → `fn`+`w` → Randomize → the LISTEN tell) and advances only when your fingers actually do it — the instrument keeps playing normally the whole way through. A fresh unit boots straight into it (it replaces the old intro card); a unit that updated gets asked once, with a card (`enter` starts, any other key passes). `` ` `` skips at any point, and settings → *Tutorial* replays it — it's also the thing to hand a friend with the device.
- **One-shot tips.** The coach watches for the moment a gesture would have helped and names it exactly once, ever: play a lot of off-scale (`shift`) notes and it mentions `fn`+`k` / `fn`+`s`; go boots without touching the sound slots or Randomize and it points them out. A tip never repeats, never fires twice a session, and retires silently the moment you use the gesture on your own.
- **The hints know the layer.** While `fn` is held, the bottom line now spells the whole layer (`q-p sound  k key  s scale  hold k: mic`), and the resting hint line alternates between the classic `fn edit  tab setup...` and the fn-layer headliners. *How to play* — the full scrollable cheat sheet — moved to the very first row of settings.

## Persistence and reset

**Your saved sounds live on the microSD card.** Saving over a slot (`fn`+`shift`+letter) writes a file in `/glide/slots/` on the card — the same `.gpat` format as the library, so a slot, a library patch, and a Discord attachment are all the same thing. No card in? The instrument plays exactly the same (the ten slots fall back to their factory and generative sounds, and everything about *making* sound is card-free); only saving asks for a card, and it says so in plain words: **no SD card — insert a card to save sounds.** Pop the card back in and your slots return, mid-session, no reboot.

Settings and the live working sound persist on the device itself, so they survive reboots, firmware updates, and card swaps. That sliver of flash is shared with the Launcher and every other app — and since v2.8 it **manages itself**: GLIDE keeps its footprint tiny, quietly mirrors your settings and live sound to the card, and if another app ever fills the shared space, the next boot cleans it out and restores everything automatically. You'd see a green **STORAGE FIXED — nothing to do, play on** note for two seconds, and that's the whole event. (Settings → SYSTEM → *Storage* just reads `OK`.)

Settings → SYSTEM also keeps the **odometer**: a quiet lifetime count of the notes you have struck and your hands-on hours. No goals, no streaks; just the instrument's life with you. It is a record, not a setting: it survives every reset below, the factory one included. Three ways back:

- settings → *Sound reset*: current slot back to factory
- settings → *Reset defaults*: all settings back to factory (saved sounds kept)
- **press and hold backspace during the boot splash:** full factory reset — settings and the ten slots (the card files too, so the reset means what it says), while your `/glide` library on the card is never touched. Hold it through the red confirm bar (~1.5 s); release at any point cancels. Deliberate on purpose, since a stray tap used to wipe people's sessions. It has to be a press made *during* the splash and then sustained; the ADV's keyboard chip is event-driven and can't see a key held from power-on. If device storage is truly wedged it escalates to erasing and rebuilding the whole shared partition (your two generative slots keep their identity, and the odometer rides across); other apps' settings and the Launcher's saved Wi-Fi networks are cleared with it and rebuild their defaults on next use. Day to day you should never need this — storage looks after itself.

---

Stuck, or want to share what you've made? **[Join the Discord](https://discord.gg/uRcuJGCeHG)**: patches travel as plain `.gpat` files, so trading sounds is just dragging an attachment onto your SD card.
