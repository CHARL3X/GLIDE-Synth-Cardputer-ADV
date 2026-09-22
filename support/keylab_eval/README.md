# keylab_eval — score LISTEN against real rooms before flashing

Host-only replay of a key-lab CSV through the SHIPPED classifier, landing and
space-key alternates (`dsp/key_detect.cpp`). A lab build of the firmware logs,
per listen, the song's true key (a human's call) and the 12-bin chroma the
device accumulated; `classifyChroma()` is a pure function of that chroma, so
everything downstream of it can be measured here against dozens of labelled
songs in a second, instead of one song at a time on the couch.

What it does NOT replay: the front end (Goertzel bands, floor and harmonic
subtraction, octave weights). That needs the raw audio, which a lab build
would have to save as WAV next to the chroma.

## CSV

Header row, then one row per listen. Columns are read by POSITION (0-based):

| col | content |
|---|---|
| 4 | true key: `F major`, `D dorian`, `E mixolydian`, `Eb minor` (flats ok) |
| 16..27 | chroma C, C#, D … B — `KeyGuess::chroma`, peak-normalized |
| 28 | title (last column, so commas in titles are harmless) |

Anything else in between is ignored, so a lab build may log whatever it likes
(confidence, rounds, its own alternates). The field data this was written
against lives outside the repo and is deliberately not checked in.

## Build

```
g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
    keylab_eval.cpp ../../src/dsp/key_detect.cpp -o keylab_eval
```

(No SSE flags on Apple Silicon — same rule as the native test gate.)

## Run

```
./keylab_eval <path>/keylab.csv        # the scoreboard
./keylab_eval <path>/keylab.csv -v     # plus one line per song
```

Output:

```
songs=47  nothing-sour=29  home-right=27  both=21   (safe=11 modal=1 tiebreak=1)
first in-song+home landing, cumulative by space position 0..7: 21 26 31 36 37 37 40 44
first merely in-song landing, cumulative:                  29 38 ...
```

- **nothing sour** — every note of the applied scale is in the song's pitch set
- **home right** — the applied root is the song's tonic
- **both** — the landing a player would call correct
- the cumulative lines say how many songs are fixed by the N-th `space`
  press: position 0 is the primary landing, 1 the first alternate, and so on

Change a gate, a profile constant, or the alternates order; rebuild; compare
the lines. Exit status 0 if at least one song scored.
