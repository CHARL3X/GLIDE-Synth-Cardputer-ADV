# The GLIDE manual

Everything the instrument does and how to play it. For install and the five-minute intro, see the [README](../README.md); for the design story and internals, see [design.md](design.md).

## The keymap

```
 string 3 (hi) |  1  2  3  4  5  6  7  8  9  0  |  - oct-   = oct+   bksp PANIC
 string 2      |  q  w  e  r  t  y  u  i  o  p  |  [ bend-  ] bend+  \  tap tempo
 string 1      |  a  s  d  f  g  h  j  k  l  ;  |                   enter tilt
 string 0 (lo) |  z  x  c  v  b  n  m  ,  .  /  |  space sustain

 `     exit (HOLD ~0.7s)     fn (hold)    quick-edit layer   [see note below]
 tab   settings             shift (hold) momentary chromatic
 ctrl/opt volume -/+ (left thumb)        alt loop pedal (left thumb)
 - / =    octave -/+          G0 (RIGHT trigger, top edge) = trigger macro (muffle)
                                         (tap rec/play/dub, hold clear, fn+alt undo)

 fn + q..p         : switch between the ten sounds, live
 fn + shift + q..p : save your current tweaks over that slot
 fn + 1..0         : pick a parameter, [ ] to adjust
 fn + k            : cycle the key (root) up a semitone, live
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
- **Or let the instrument find it: hold `fn`+`k` and it LISTENS.** The synth goes quiet while the mic hears whatever's playing in the room, in ~3-second rounds, stopping the moment it's sure and listening up to ~9 s when the song is being coy (a single round can land on one chord and name *its* key; more rounds hear the changes). A chromagram works out the song's key (root *and* major/minor), and the instrument retunes itself. It hears the *mode*, not just major-or-minor: the degrees that separate Dorian from minor and Mixolydian from major are read straight from the capture, and a two-chord vamp that fools the textbook reading (an Am7-D9 groove scores as "D major" as honestly as "A minor") gets its tonic re-seated where the song actually lives. The verdict then lands in YOUR scale family: the plain seven-note scales play the detected mode itself; pentatonics swap flavor and sit on the tonic; Blues is never switched, only re-centred (the tonic over minor, Dorian, and dominant grooves, the relative-minor boxes trick over a plain major song); other exotics keep their flavor and take the relative root as always. Weak evidence changes nothing, since mode and tonic corrections sit behind stricter gates than the key itself. The same capture reads the song's tempo from its onsets: a confident beat sets the jam clock (the synced delay and LFOs follow), a beatless room leaves it untouched, and the card shows the locked BPM. And when a song refuses to pick a side — it audibly plays *both* sixths, or a note your scale asserts is one the song contradicts (a Lydian #4, a Phrygian b2) — the landing retreats to the pentatonic at the tonic, which simply omits the clash note: when unsure, play fewer notes rather than a wrong one (the card says "clash heard - safe pent"). While it listens you watch the twelve pitch-class bins fill in real time, pulsing as each round lands, with its forming verdict underneath. The result card shows the bars it heard, the detected key and mode, a confidence meter, and the applied root, with an amber strip marking which notes your applied scale contains; a weak or silent room says NO SIGNAL and changes nothing. If the verdict is *close but not quite*, tap `space` while the card is up to cycle the second guesses (the sibling mode, the safe pentatonic, the relative twin — each applied live as you cycle); `` ` `` or `enter` keeps what's showing, and tapping `fn`+`k` again re-listens. (Cardputer ADV only, since it needs the mic; no mic just means a visible "mic unavailable", never a broken instrument.)

  <img src="../assets/glide-listen.svg" alt="LISTEN: a song plays in the room, hold fn+k, the mic works out the key, mode, and tempo, and the instrument retunes itself" width="100%">
- **`fn` + top row** picks a parameter (glide, ADSR, wave, cutoff, voices, bend range, volume); `[` `]` adjust it live. Nothing is hardcoded. Every sound parameter has a control, and everything survives a reboot.
- The **oscilloscope** is live. That's the actual output waveform, with a phosphor afterglow. The note readout tracks the lead voice in cents *through* glides and bends, so you can see exactly where you are between the notes.
- Or flip the display to the **pitch trail** (settings → *Display*): the lead voice's pitch drawn over time, scrolling across ~7 seconds, with root-note gridlines as fret markers. On an instrument about the space *between* notes, this is the scope for the other axis. Every glide, hammer-on, and bend becomes a visible curve (bend-pulled segments draw amber), and with tilt-vibrato on you can watch the line shimmer.

## Your sounds are yours

This is the other half of GLIDE, and arguably the bigger one. The factory bank is a starting point you grow past.

Ten slots live on `fn`+`q`..`p`. Eight are a curated bank, led by **GLIDE** on `q` (the home/boot sound) and **ACID** on `w`. The last two, `o` and `p`, are **generative**: rolled from a seed unique to your unit, so they're different on every device on Earth. From there you build your own.

| key | sound | character | tilt (per-sound mode) |
|-----|-------|-----------|-----------|
| q | **GLIDE** | the signature dry saw, and the literal boot tone | vibrato (roll: filter) |
| w | **ACID** | resonant squelch. lean into it, tilt is the wah | filter (full) |
| e | **Bass** | fat pulse bass: square sub for weight, driven, snappy filter pluck | filter (roll: vibrato) |
| r | **Solo** | bright square lead, always-gliding, 1/8-triplet delay in the pocket | vibrato |
| t | **Ethereal** | soft triangle pad, long glide, roomy hall. a bed to solo over | vibrato (roll: swell) |
| y | **Fat Square** | punchy square, bright per-note filter bloom, attack knock | filter |
| u | **Hollow** | driven square through a *notch* filter, phasey and hollow | volume swell (roll: filter) |
| i | **Drift** | lush always-gliding square, deep chorus and a 1/8 delay | filter |
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

- **Your rig, or the sound's.** By default the tilt map is *global*: forward/back and left/right each hold a route that follows your hands across every sound instead of resetting per patch. Out of the box that's **Morph on forward/back** (lean into the sound you were just on) and **vibrato on left/right**, both at 60%. Set it once and play. Flip settings → *Tilt map* to **per sound** and each patch carries its own route and depth instead (ACID into a full wah, Ethereal and Solo into vibrato, per the table above), saved with the slot.
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
- Because the loop is events, it costs kilobytes. The good part: it **plays through whatever sound is selected**. Record a Bass line, switch to Solo, solo over it. Swap sounds mid-jam and the whole arrangement re-voices itself. Recorded slides, hammer-ons, and octave sweeps replay as slides, hammer-ons, and sweeps.
- Loop playback is a protected backing layer like the drones. Its voices ride outside the voice cap, can't be robbed by chord-slide stealing, ignore live bends and tilt vibrato, never hijack the note readout, and survive sound switches and settings trips. Internally it plays on its own string lanes (4 to 7) with its own key ids, so it can never collide with your hands.
- Timing belongs to the audio thread. Playback events are *scheduled* (block-accurate, ~4 ms), not fired from the ~33 ms UI frame, so the loop doesn't swing with the frame rate.
- **The loop locks to the jam clock.** The tap that closes a take snaps its length to the nearest **bar** of the *Jam tempo* (minimum one bar; a tap 40% into bar one was meant as a 1-bar loop), so the loop and the auto-progression share one clock instead of drifting apart a little more every cycle. Notes played just past the bar line wrap to the downbeat; a note still held at the close rings to the loop end. Settings → *Loop snap* picks `bar` (default), `beat`, or `off` for the old free-time behaviour.
- Status sits top-left of the scope: **REC** blinks red with elapsed time, **LOOP** green with a cycle-progress bar, **OVR** amber while layering, dim `LOOP --` for a stopped take. `FULL` means the take hit the 1024-event ceiling.

Loops are performance state. They live until cleared or power-off, and never hit flash.

## The chord progression (the easy way to back yourself)

The loop pedal records a *performance*, which means your timing has to be right, and a loop is one phrase, not a chord change. The drones fixed the timing problem (tap to latch, no rhythm) but a drone is one chord forever. The **auto-progression** is the missing middle: a soft chord progression you spell with no timing at all, then solo over. Settings → *Jam motion: progression* (needs *Jam rows* on):

- **Tap the chords in order on the jam row. That's it.** Each tap appends a step (repeats allowed: I-IV-V-IV is four taps). No metronome, no pocket to hit. The HUD confirms each one (`PROG  3: E`).
- The beat clock walks the steps **one chord per bar**, looping, at the *Jam tempo*. *Chord length* sets the beats per chord. The backing glides from chord to chord (of course it does) and re-blooms each bar, so on a pad or strings patch it's a soft wash you solo straight over.
- Each step is a **diatonic triad** built from the current scale: real major/minor/dim color, and always in key. The same "you can't hit a wrong note" guarantee the melody gets, now for the backing too. (Hold `shift` while tapping a step for a chromatic power-chord voicing instead.)
- It's a protected backing layer like the drones and the loop: cap-exempt, steal-proof, ignores your bends and tilt vibrato, and **re-voices through whatever sound you switch to** mid-jam. Lay down Ethereal, solo on Solo.
- The progression is on screen: a `PROG  A  D  E  ▸` strip across the top of the scope with the current chord boxed, and its root outlined on the grid-map so you can watch the changes walk.
- **bksp (panic)** clears the progression to start over, the same gesture that clears the drones. Like them, it's performance state and never hits flash.

Pick Ethereal, Drift, or Hollow for the bed, set a slow tempo, tap four chords, and you've got a song to solo on in about ten seconds.

## Soloing over the jam: a separate register and sound

Once the backing is looping, you don't want to be stuck in its octave or its sound. You want to *solo* over it. So the moment you change the solo while a jam runs, the backing holds its ground:

- **Different register.** Shift octave (or even change key/scale) and only your **solo** moves. The progression keeps looping in the register and key it was built in. Build a progression low, solo two octaves up over it.
- **Different sound.** Switch patches (`fn`+letter) over a running jam and the backing freezes onto the sound it was playing while the new patch becomes your solo voice. Lay down an Ethereal progression, flip to Solo, and wail over it. The pad keeps padding. An amber **`LK`** by the octave readout (and a `SOLO` flash on the switch) tells you the split is engaged.
- **Their own voice, a shared room.** The backing and the solo each keep their own oscillator, filter, envelope, and drive, but they wash into one shared reverb/delay space (the solo patch's), so the whole thing sits together instead of sounding like two unrelated machines.
- **`bksp` (panic)** clears the jam and drops the split. The next sound switch goes back to changing everything, as normal.

No new gesture to learn. Start the jam, then change your sound. The split appears when you need it and disappears when the jam's gone.

## Tempo, the synced delay, and the live FX rack

One tempo (the *Jam tempo*) drives both the progression and the echo. Two things make a solo over that backing sound produced:

- **Tap tempo**, on `\`, right on the keyboard, so you can match a song's groove without leaving the instrument. Tap it in time and the BPM follows your hand; the HUD reads back the tempo on every tap. A single tap after a pause only *reports* the tempo, so a stray press can't move anything; it takes two taps to make a beat. (Also in settings → *Tap tempo*, tapped with `,` or `/`; it's the same series either way, so you can start in one place and finish in the other.)
- **Or let LISTEN set it by ear.** Hold `fn`+`k` at a song and the detected key arrives with its tempo: when the beat is confident the jam clock takes the BPM, and everything synced to it follows. A weak or absent beat moves nothing.
- **Tempo-synced delay** (settings → *Delay sync*): lock the echo to a musical division (`1/4`, `1/8.` the dotted eighth and the Edge/Gilmour trick, `1/8`, `1/8T`, or `1/16`) and every repeat lands on the beat. Solo and Drift ship with it on; switch to Solo over a progression and the repeats cascade right in the pocket. (Set it to `free` for a plain ms delay.) If a division is too long for the delay line at a slow tempo, it folds down an octave so it stays on the grid instead of clipping.

The whole **send-FX rack is live on-device** (settings): *Chorus*, *Delay send / time / sync / feedback*, *Reverb send / size*. Dial the space to taste and `fn`+`shift`+letter saves it with the slot, like every other sound parameter. The effects were the one thing you couldn't reach before. Now nothing about the sound is off-limits.

## The modulation matrix (get far from the default)

Tilt was the first assignable modulator. Now there's a whole rack of them, which is exactly how two players with the same device end up with sounds that share no DNA. Settings → *MOD SOURCES* and *MOD MATRIX*:

- **Two LFOs**, each with a shape (sine / tri / saw / square / **S&H** random) and either a free rate in Hz or a **tempo-sync** division locked to the *Jam tempo* (same `1/4` to `1/16` vocabulary as the delay), so a wobble or a filter sweep breathes in time with the progression.
- **A second envelope** (attack / decay) that retriggers on each note.
- **Six routing slots.** Each picks a *source* (LFO1, LFO2, mod-env, key-track, bend), a *destination* (pitch, cutoff, resonance, amp, filter-env depth), and a bipolar *amount*. Six slots across those sources and destinations is a huge sound-design space: vibrato, tremolo, auto-wah, growl, evolving pads, random steppers, all from a handful of primitives, all on the lead voice (the backing bed stays steady underneath). Everything defaults to off, so a fresh patch is the original GLIDE tone until you wire a slot.

**Filter modes** (settings → TONE → *Filter mode*): the filter does **lowpass** (the original voice), **highpass** (thin/airy), **bandpass** (vocal/telephone), and **notch** (hollow/phasey), free, because the filter already computes them all.

Every one of these saves with the slot (`fn`+`shift`+letter) and survives a reboot. And because patches use a **tagged format**, adding the next knob, or the one after that, will never wipe the sounds you've already saved. Expansion is the point. The further you get from the default, the more the instrument is *yours*.

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
| root / scale / row interval | C-B / 13 scales / 1-12 st | A / min pent / 4th | settings |
| glide mode | legato-only / always | legato-only | settings (per sound) |
| allocation | strings (mono rows) / free poly | strings | settings |
| jam rows (drones) | off / bottom / bottom 2 | bottom | settings |
| jam motion | sustained / pulse / arp / progression | progression | settings |
| jam tempo / chord length | 40-240 bpm / 1-8 beats | 100 / 4 | settings |
| loop snap | off / beat / bar | bar | settings |
| octave keys | sweep (glide) / re-strike | sweep | settings |
| trigger action / depth / mode | muffle, brighten, pitch dive, drive grit / 0-100% / momentary, latch | muffle / 70% / momentary | settings (right trigger, G0) |
| sound slots | 10 (q=GLIDE, w=ACID, e..i curated, o/p generative per device) | curated + 2 rolled | fn+q..p, fn+shift+q..p |
| generate | randomize / mutate (+amount) / undo-redo / init / re-roll bank | live | settings (CREATE) |
| SD library | save / load / delete named .gpat patches (unlimited) | live | settings (LIBRARY), browser |
| filter env (atk/dec/depth) | 1ms-2s / 10ms-2s / 0-3+ oct | per sound | settings, saved in sound |
| sub / noise / drive / auto-vib | 0-1 / 0-1 / 1-8 / cents | per sound | saved in sound |
| chorus / delay / reverb send | 0-100% each | per sound | settings (live) |
| delay time / sync / feedback | 10-600ms / free+5 divisions / 0-90% | per sound | settings (live) |
| tap tempo | 40-240 bpm, tapped | live | `\` key, settings |
| tilt map | global (follows your hands) / per sound | global | settings |
| tilt routing (f/b + l/r) | off / cutoff / vibrato / volume / morph | Morph f/b + vibrato l/r | settings, enter toggles |
| tilt depth | 0-100% | 60% | settings |
| tilt center | calibrated "flat" | 0 | settings (hold + set) |
| display | waveform scope / pitch trail | pitch trail | settings |
| screen idle | off / dim / dim + screensaver | dim + screensaver | settings |
| solo/backing split | auto when you change sound/octave over a jam | live | live |

## Persistence and reset

Everything you touch (sounds, tweaks, octave, scale, tilt setup, jam rows) saves to flash moments after you change it and survives reboots *and* firmware updates, since NVS lives outside the app partition.

Settings → SYSTEM also keeps the **odometer**: a quiet lifetime count of the notes you have struck and your hands-on hours. No goals, no streaks; just the instrument's life with you. It is a record, not a setting: it survives every reset below, the factory one included. Three ways back:

- settings → *Sound reset*: current slot back to factory
- settings → *Reset defaults*: all settings back to factory (saved sounds kept)
- **press and hold backspace during the boot splash:** full factory reset, settings and saved sounds, even if stored state ever wedges the UI. Hold it through the red confirm bar (~1.5 s); release at any point cancels. Deliberate on purpose, since a stray tap used to wipe people's sessions. It has to be a press made *during* the splash and then sustained; the ADV's keyboard chip is event-driven and can't see a key held from power-on. This reset is also the cure for a **STORAGE FULL** boot warning: when the shared flash partition is full, the reset erases and rebuilds the whole partition (save your sounds to SD first — your two generative slots keep their identity, and the odometer rides across). Because that partition is shared, other apps' settings and the Launcher's saved Wi-Fi networks are cleared with it; they rebuild their defaults on next use.

---

Stuck, or want to share what you've made? **[Join the Discord](https://discord.gg/uRcuJGCeHG)**: patches travel as plain `.gpat` files, so trading sounds is just dragging an attachment onto your SD card.
