# 25 — Unclutter the perform screen: default octave, tutorial reflow, and indicator hierarchy

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. This is a `ui/` audit and redesign — `env:native` does not cover UI (CLAUDE.md: "keep the native tests green and review carefully — there's no on-device build here"), so verification is on-device.

**Goal:** Fresh units ship with a default octave one octave lower. Stabilize the
tutorial card reflow (the layout shift when a card completes makes players think
they missed info). Audit and reorganize the indicator area at the top of the
visualizer — consolidate overlapping status lines (`WAH`, `ACID [======]`, tilt
organ indicator, etc.) into a thoughtful, unified presentation that handles edge
cases (multiple features active at once, different synth modes). Use visuals
where text alone is cramped and unclear (e.g., right trigger default: wah,
latching).

**Effort:** M. **Risk:** low for the octave default; medium for the visual reflow
fix (may touch layout containers); medium for indicator consolidation (spatial
reasoning + testing multiple setups).

**UI-cost budget:** 0 new rows, 0 new gestures. Octave default is a constant
change. Tutorial card and indicator reorganization are spatial/visual audits —
no new features, only clearer presentation.

## The problems (observed on live fresh units, v3.2)

### 1. Fresh unit starts one octave too high

New players land on the default octave, which is higher than is intuitive for
entry. Evidence: direct user feedback from fresh-unit play-testing. Octave down
one step is the natural starting point for noodling and learning.

**Fix:** Change the default octave in `glide_config.h` (today likely 4 or 5;
move it to 3 or 4 — confirm current default first).

### 2. Tutorial cards reflow on completion

The quarter-screen tutorial cards display a lesson (e.g., tilt, wah, etc.). When
the player completes it, the UI reflows — the container resizes or content
visibility changes — and the text shifts. The color change on completion is good
feedback, but the text movement makes it appear as though new info appeared,
then vanished, creating a false "did I miss something?" anxiety. It feels like a
bug, not like the card simply completed.

**Root cause:** The container is likely sized dynamically around content (e.g.,
`flex-shrink`, no min-height, or a visibility toggle that collapses space). When
the "completed" state changes, the layout recalculates.

**Fix:** Pre-allocate space for the card so text never moves on completion. Or:
freeze the container size during the completion state, then animate the
dismissal if desired.

**Polish:** Also consider visuals (animated GIFs or side-by-side diagrams, like
those in `docs/readme.md`) to explain features that text alone can't convey
clearly on a tiny screen (e.g., **right trigger default: wah, latched** — show
what "latched" means with a 2-second animation, not just words).

### 3. Indicator area at visualizer top is overcrowded

The perform screen's visualizer has transient status lines at the top: `WAH` (g0
mode), `ACID [======]` (tilt spectrum bars), organ indicator (tilt organ mode),
and others. These fight for horizontal space and overlap depending on which
features are active at the moment.

**The crowding problem:** A player holding a chord while tilt is engaged and G0
is set to wah gets multiple status lines firing at once. The space is too small,
text overlaps or wraps awkwardly, and the visual hierarchy is unclear — which
indicator matters most in this moment?

**The real issue:** Not every indicator is equally useful in the moment. Some
are "nice to know" (e.g., "tilt is engaged"), others are "critical" (e.g.,
"wah is sweeping"). Today they all share the top line and fight for pixels.

**Direction:** Audit which indicators are genuinely actionable (the player needs
to know it RIGHT NOW) vs. passive status (nice context, but not urgent). Remove
or move the less-urgent ones. Consolidate the urgent ones into a unified,
edge-case-aware layout that stays legible when multiple features collide.

**Consider edge cases:**
- G0 + tilt both active
- Tilt with dual mode (both A and B routed)
- Tilt with organ indicator (visual spectrum)
- Multiple effects engaged (reverb, echo, etc.)
- Synth morph in progress

**Space reclaim:** The real win is freeing up the pixels currently taken by
less-immediately-useful static text elsewhere on the screen (setting labels,
mode names that never change during play, etc.) and thoughtfully moving urgent
transient info there instead.

## Current state

**Octave default:** Confirm in `src/storage/glide_config.h` what the default is
(likely `layout.octave` or `jamOctave`).

**Tutorial reflow:** Likely in `src/ui/perform_screen.cpp` or wherever tutorial
cards are drawn (search for `drawIntro` or similar). Check container sizing.

**Indicator area:** Produced in `perform_screen.cpp`'s screen render, top section.
Lines like `"WAH"`, tilt spectrum bars, organ mode label — all drawn in sequence
without spatial planning.

## Tasks

- [ ] Find and confirm the current default octave constant in `glide_config.h`.
      Make a note of the value and the field name (e.g., `layout.octave = 4`).
- [ ] Lower the default by one octave. Verify it survives a boot and a factory
      reset (the special-case carry-over in `glide_config.cpp` ~1772/1782).
- [ ] Test on hardware: fresh unit boots at the new octave. Hand-test that
      octave up/down (`fn`+`[`/`]`) works as expected.
- [ ] Locate the tutorial card rendering code and its container. Identify why
      the reflow happens on completion (is it a visibility toggle, a
      flex-shrink, a dynamic height?).
- [ ] Stabilize the container: either pre-allocate space (min-height, or a
      reserved area) OR freeze the size during completion animation.
- [ ] Test the reflow fix on device with all tutorial cards (tilt, wah, gate,
      etc.). Confirm text never shifts.
- [ ] Audit the indicator area at the top of the visualizer. List every status
      line that appears there (e.g., `WAH`, tilt organ spectrum, any others).
- [ ] Classify each indicator: **urgent** (player needs to act / know it right
      now) vs. **passive** (context only). Document the classification.
- [ ] Propose a new spatial layout for urgent indicators that survives edge
      cases (multiple features + tilt dual mode + organ mode + effects). Sketch
      or wireframe before coding.
- [ ] Identify static text elsewhere on the screen that could be removed or
      moved to free space for indicator consolidation.
- [ ] Implement the new indicator layout. Test on device under each edge-case
      setup (list above).
- [ ] Add a visual (animated GIF or side-by-side frames from the play screen)
      to the tutorial card explaining the right trigger default (wah, latched)
      if space allows. Otherwise, update `docs/manual.md` with a small diagram.
- [ ] `pio run`; RAM line unchanged (no new statics, no resident heap).

## Acceptance criteria

- Fresh unit boots at octave one lower than before.
- Tutorial card text does not shift when completion state changes.
- The indicator area at the visualizer top remains legible when G0 + tilt +
  organ + effects are all active simultaneously.
- No new settings rows or gestures.
- All indicators remain functional (no information lost, only reorganized).

## Risks

- The tutorial reflow may be baked into multiple card renders — don't just fix
  one, audit all of them.
- Removing indicators risks losing information players depend on (e.g., "is tilt
  on right now?"). Test heavily on hardware before committing.
- Screen space is a zero-sum game: reclaiming space for urgent indicators means
  deciding what else loses visibility. That is a judgment call.
