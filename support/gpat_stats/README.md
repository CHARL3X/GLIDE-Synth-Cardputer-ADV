# gpat_stats — the bad-roll harvest tool

Host-only CSV dump of `.gpat` patch files: name, roll provenance (seed /
painted archetype / generator version / style, when the save carries the
`T_rollProv` record), both classifier verdicts, and every continuous field.

Built for the range-tuning workflow: the team saves a pile of deliberately
BAD rolls on-device, the card's folders come here, and each bad sound maps
straight back to the paint window and style that produced it — tuning becomes
data work instead of ear forensics. Rolls saved by firmware older than the
provenance record still print (their provenance columns are just empty).

## Build

Any host with g++ (this is the same host-safe file set the native tests use;
on Apple Silicon use exactly this line — no SSE flags):

```
g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
    gpat_stats.cpp ../../src/storage/patch_codec.cpp \
    ../../src/dsp/sound_gen.cpp -o gpat_stats
```

## Run

```
./gpat_stats /Volumes/<card>/glide/library /Volumes/<card>/glide/slots > rolls.csv
```

One row per patch; the header row names every column. Unreadable or
non-`.gpat` files are reported on stderr and skipped. Exit status: 0 if at
least one patch printed.

## roll_diff — the rated rolls audit the next version

Once a rating session exists, the next generator version has a contract it
can be held to before a second field round is spent: every roll the ear
called BAD must change on at least one field, and every roll it called GREAT
must be bit-identical. `roll_diff` re-rolls each seed in a `lab.csv` through
the rated version (V5) and the version under tuning (V6) and checks both.

```
g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
    roll_diff.cpp ../../src/dsp/sound_gen.cpp -o roll_diff
./roll_diff <path>/lab.csv        # summary + any contract violations
./roll_diff <path>/lab.csv -v     # every roll, with the fields that moved
```

`lab.csv` columns read by position: 0 = rating (1..4), 4 = seed as 8 hex
digits, 6 = archetype index, 7 = archetype name. Exit status 1 if the
contract fails. The rating data itself stays outside the repo.
