# 24 — The intro card shows across the first 3 startups, not just one

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. This is a `storage/` + `ui/` change — `env:native` does not cover either (CLAUDE.md: "keep the native tests green and review carefully — there's no on-device build here"), so verification here is on-device boot cycles, not a test binary.

**Goal:** Today, `seenIntro` is a one-shot bool: the first grid press or a 6 s
timeout on the very first boot latches it `true` forever ([[intro-card-before-boxing]]
— a single play-test during boxing burns a new player's only look at the
gesture card). Turn it into a small counter so the card naturally reappears
for a genuinely new player's first **three** startups, while a unit that
already dismissed it under the old logic is never resurfaced.

**Effort:** S. **Risk:** low — self-contained state, every existing dismiss
path (grid press, `kIntroMs` timeout, the `coach.cpp` ritual) stays, only what
they set changes.

**UI-cost budget: 0 rows, 0 gestures.** The existing binary Settings → *Intro
card* row (`fIntro`/`aIntro`, "will show" / "hidden") is unchanged in the UI —
its two states now mean "will show for its next 3 startups" / "hidden",
instead of "will show once".

## Design

- `store::seenIntro` (`bool`) becomes a counter: `uint8_t introShown`, capped
  at `kIntroShowCap = 3`. `drawIntro()` runs whenever `introShown <
  kIntroShowCap`. Dismissing it (grid press or timeout, `perform_screen.cpp`
  ~line 1914) **increments** the counter for that boot rather than latching
  it done.
- `coach.cpp`'s guided onboarding ritual (line 236, "the ritual replaces the
  card") is a stronger signal than the passive card — finishing it should
  jump straight to `introShown = kIntroShowCap`, not make an already-taught
  player sit through two more boots of the card.
- Settings row keeps its binary wording. `aIntro` toggles between
  `introShown = 0` (force it back on for a fresh 3 startups) and
  `introShown = kIntroShowCap` (hidden). `fIntro` reads "will show" whenever
  `introShown < kIntroShowCap`, "hidden" otherwise.
- **NVS migration, the trap in this doc.** The persisted key today is a
  `bool` ("intro", `getBool`/`putBool` in `glide_config.cpp`). Reading a
  bool-typed key back with `getUChar` does not crash but silently returns
  the supplied default, not the stored value — the same class of trap this
  codebase already has a fix pattern for (`trigv2`/`trigv3` one-shot
  migration keys, `glide_config.cpp` ~1336). **Do not widen "intro" in
  place.** Add a new key (e.g. `"intron"`, ≤15 chars) and migrate once: if
  `"intron"` is absent and the old `"intro"` bool reads `true` (a unit that
  already dismissed the card under old firmware — includes every unit
  flashed before this doc lands), initialize `introShown = kIntroShowCap` so
  it is **not** resurfaced. If `"intron"` is absent and old `"intro"` is
  false/absent (a genuinely fresh device, or a factory reset), `introShown =
  0` — shows for its first 3 startups as designed.
- **Rig mirror** (`rigBuild`/`rigApply`, `glide_config.cpp` ~line 649/699):
  the `c.seenIntro ? 1 : 0` slot is already a full `int32_t` in a fixed-size
  array. Repacking it as `0..3` instead of `0/1` does **not** change the slot
  count, so `kRigVer` does **not** need to bump — just change what is packed
  and unpacked at that one slot.
- **Factory reset** (`glide_config.cpp` ~1772/1782) explicitly special-cases
  `seenIntro` to survive a reset ("don't re-show the intro on reset"). Carry
  `introShown` through that same special-case, unchanged in spirit.

**Files:** `src/storage/glide_config.h` (field), `src/storage/glide_config.cpp`
(load / persist / one-shot migration / rig mirror / factory-reset carry-over),
`src/ui/perform_screen.cpp` (draw condition + dismiss-increments), `src/ui/coach.cpp`
(ritual retires it immediately), `src/ui/settings_screen.cpp` (`fIntro`/`aIntro`
against the counter instead of the bool).

## Tasks

- [ ] Add `uint8_t introShown` to `GlideConfig` (`glide_config.h`), replacing
      `seenIntro`'s storage role; keep `kIntroShowCap = 3` next to it.
- [ ] Wire the one-shot `"intron"` migration exactly like `trigv3`'s pattern
      (a `gPrefs.getBool("intron-seen", false)` guard, or equivalent — read
      the `trigv3` block first and copy its shape, not just its idea).
- [ ] Update `rigBuild`/`rigApply` to pack/unpack the 0..3 value at the
      existing slot; confirm `kRigVer` genuinely does not need to move
      (slot count unchanged) before assuming it.
- [ ] `perform_screen.cpp`: draw while `introShown < kIntroShowCap`; on
      dismiss, `introShown++` (never a hard set to the cap) unless already
      at cap.
- [ ] `coach.cpp` line 236: on ritual completion, set `introShown =
      kIntroShowCap` directly (skip the remaining count, it already taught
      the gesture).
- [ ] `settings_screen.cpp`: `fIntro` reads `introShown < kIntroShowCap`;
      `aIntro` flips between `0` and `kIntroShowCap`.
- [ ] Confirm the factory-reset path (~1772/1782) carries `introShown`
      through the same way it carried `seenIntro`.
- [ ] `pio run`; check the RAM line moved by ~0 (a `bool` becoming a
      `uint8_t` is not a measurable delta, but CLAUDE.md rule 7 says check
      anyway, not assume).

### On-device verification (no native coverage for this doc)

- [ ] Wipe NVS (or a genuinely fresh unit). Boot, dismiss the card 3 times
      across 3 separate boots. Confirm it does **not** appear on the 4th.
- [ ] Take a unit already on old firmware (`seenIntro` = true). Update to
      this build. Confirm the card does **not** reappear (the migration
      guard, not luck).
- [ ] Settings → Intro card → toggle hidden → will show. Reboot 3 times;
      confirm it shows each time, then stops on the 4th — proves the toggle
      re-arms the full 3-boot run, not a single peek.
- [ ] Trigger the `coach.cpp` ritual on a fresh unit; confirm the card does
      not additionally show afterward.

## Acceptance criteria

- A genuinely new player sees the card on their first 3 startups, however
  they are spread out.
- No unit already past the old one-shot logic is resurfaced by the upgrade.
- The Settings row's two states and wording are unchanged.
- `kRigVer` unchanged (or bumped with a stated reason, if Task 3 finds one).

## Risks

- The shared NVS partition is already crowded (debt D1, `00-INDEX.md`) — one
  new small key is a fine trade, but don't add more than the one migration
  guard needs.
- Easy to get backwards: the counter counts **startups the card was shown
  and dismissed**, not boots in general — a unit power-cycled mid-card (no
  dismiss) must not consume one of its 3.
