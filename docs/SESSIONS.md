# Sessions

Most recent first. Numbered, no dates (see TRACK.md).

---

**Session 45**

Screenshot follow-up to Session 44. Vault heading's box (background, border, padding) removed; its
`h2` now matches Home's "Today" metrics so it sits on the same row as the lock/power buttons.
`--gold` brightened `#d4af37` → `#ffc61a`, lock glow doubled to two `drop-shadow` layers.
CSS-only; build clean, braces balanced (137/137). Decisions made: 67.

Next session start point: unchanged — confirm on device that the heading aligns with the buttons
and the brighter gold reads well in dark mode too.

---

**Session 44**

Follow-up screenshots after Session 43's changes went live — several restyle requests plus one
reported bug (Vault's lock button "missing").

**Restyles:** back button got a new icon (curved-return arrow, no enclosing circle, matching a
supplied reference minus its ring) and a 0.5s tap-glow (`.glow` class, force-reflowed before
re-adding so rapid taps restart the animation, removed again on `animationend`). Lock button
restyled gold (`--gold`, new CSS variable, fixed across both themes like `--danger`), thicker
stroke, persistent glow. Power button given a matching persistent red glow.

**Disabled state:** the lock button now visually disables itself on Home/Settings whenever no
app-open password is set (grayscale, dimmed, not tappable) — quick access has nothing for it to
lock into. Computed fresh on every entry into those tabs (`updateLockButtonDisabledState()`,
called from `switchMainTab()`), not cached, so it stays correct if a future session ever adds a UI
to change the app-open password after first-run (none exists today — confirmed by checking:
`setAppPassword()`/`removeAppPassword()` are only ever called from first-run-setup). Also now
hidden entirely on first-run setup, resolving the assumption flagged at the end of Decision 65.

**Vault's lock button bug:** reported as missing, actually a contrast bug, not a logic bug —
`.vault-banner`'s background was `var(--vault)` (near-black), and the icon sitting on top of it was
`var(--fg)` (dark in light theme) — present in the DOM, just nearly invisible against a near-black
backdrop. Fixed at the root: removed `.vault-banner`'s dark background entirely (now
`var(--card-bg)` with a border) and the bottom nav's matching dark active-Vault-tab background (now
`var(--vault-fg)`, a light tint, with `var(--vault)` as the legible text/icon color) — both were the
same "black bar" look, requested separately, and removing them fixes the contrast bug as a side
effect (on top of the lock icon's own restyle to gold, which would have fixed it either way).
Left the inner filter chips (`.vault-tab.active`) untouched — not named in the request, and
light-lavender text on that same dark background is genuinely legible there, unlike an icon
matching its backdrop almost exactly.

Verified with a real `npm install` + `vite build`: zero errors, CSS brace-balanced (137/137). All
ids cross-checked between `app.js` and `index.html` — clean except the same class of pre-existing
dynamically-created ids flagged every session.

Decisions made: 66.

Next session start point: this session's changes haven't been seen on the actual device yet —
worth confirming the glow effects render as expected (drop-shadow support, animation timing) and
that the Vault banner's new lighter look reads correctly in both light and dark mode.

---

**Session 43**

Screenshots showed the app actually running on-device for the first time (CI is producing installable
builds now) — three related UI requests off the back of seeing it live:

1. **Lock/unlock toggle**, fixed top-right left of the power button, on every screen except the two
   biometric/PIN unlock pages (app-open lock screen, Vault's locked gate). Context-aware at click
   time — checks whether `#vault-content` is visible to decide whether to lock the Vault or the main
   app, rather than tracking a separate flag that could drift out of sync. Vault path is the exact
   same two calls the old `#vault-lock-btn` made; main-app path reuses `showLockScreen()` wholesale
   (already handles password-set / quick-access / no-credentials-yet correctly) rather than
   re-implementing that branching. `#vault-lock-btn` removed from the Vault banner — genuinely
   redundant now.
2. **Universal Back button** — third bottom-nav slot, between Home and Vault, styled as a plain
   rounded square rather than a third tab. Backed by a deliberately simple two-slot toggle
   (`currentLocation`/`lastLocation`), not a full history stack — recorded inside `showScreen()`/
   `switchMainTab()` themselves, so every existing caller gets Back support for free without
   touching individual call sites. Lives physically inside `#bottom-nav`, so it's hidden together
   with it for free wherever the nav itself is hidden.
3. **Every dedicated "back" element removed** — `#settings-back-btn` ("← Back to Home") is gone,
   replaced by the universal button.

One real cross-closure wrinkle: `goBack()` needed to call `refreshVaultGateView()`, but that
function only ever existed inside the `DOMContentLoaded` closure, not at module top level where
`goBack()`/`showScreen()`/`switchMainTab()` live. Exposed it as `window.refreshVaultGateView`,
same pattern already used for `window.tryBiometricUnlockVault`/`window.attemptUnlock` — not a new
mechanism, just applied to a third function.

Verified with a real `npm install` + `vite build`: 264 KB main bundle, zero errors. All ids
cross-checked between `app.js` and `index.html` — clean except the same class of pre-existing
dynamically-created ids flagged every session (recording timer/stop, backup-reminder's two buttons).

Decisions made: 65.

Next session start point: whether the lock/unlock button should also show during first-run-setup
was ambiguous in the request — left visible there per the literal instruction (only two exclusions
were named), with the click handler guarding against acting on it before setup completes. Flagged,
not assumed either way — worth confirming once seen on device. Device-pass debt from Sessions
25 onward is now partially resolved (CI produces a real APK — that's new progress), but this
session's three changes haven't themselves been tapped on a real device yet.

---

**Session 42**

Second CI log upload. Progress from Session 41's fix — `src/js/privacy-screen.js` now resolves (20
modules transformed, up from 19 before it existed), but the build failed one step further in:

```
[vite]: Rollup failed to resolve import "@capacitor-community/privacy-screen" from
".../src/js/privacy-screen.js"
```

**Cause:** the npm package itself isn't in `node_modules` at build time, which only happens if
`package.json` in the actual committed repo doesn't list it — i.e. Session 39's `package.json`
(and likely `capacitor.config.json`, delivered alongside it, though a missing config wouldn't itself
break the build) never got committed, even though this session's earlier file
(`privacy-screen.js`) now has. Confirmed by the error message directly — `npm install` (the step
right before) completed without error, so it simply had nothing named `@capacitor-community/
privacy-screen` to install.

**Fix:** re-delivered `package.json` and `capacitor.config.json` unchanged, re-confirmed against
the sandbox (clean `npm install` + `vite build`, 99 modules, zero errors). **Why correct:** the
error names the exact unresolvable package, and it's the one dependency line unique to Session 39
that a prior session's `package.json` wouldn't have — every other file this build step touches
(`privacy-screen.js`, `app.js`) is already confirmed present from Session 41's diagnosis.

No code changed this session either — two sessions in a row now have been "which of Session 39's
several files actually landed," not a code defect. Worth naming as a pattern: **Session 39
delivered 6 files in one message** (2 NEW: `privacy-screen.js`; REPLACE: `package.json`,
`capacitor.config.json`, `app.js`, `index.html`) — it looks like they're landing one or two per
push rather than all at once. Flagged for the person, not assumed silently: double-check all of a
multi-file session's outputs got applied before the next push, rather than re-uploading logs
one gap at a time.

Decisions made: none.

Next session start point: unchanged — once `package.json`/`capacitor.config.json` are actually
committed, the workflow should get past step 6 for the first time. If it fails again, check whether
anything else from Session 39 (or any earlier multi-file session) is still missing before assuming
new code is at fault.

---

**Session 41**

Uploaded a GitHub Actions log bundle from a real CI run — the first actual CI feedback since the
device-pass debt started (Session 25). Build failed at step 6 (`npm run build`, i.e. the Vite build):

```
Could not resolve "./privacy-screen.js" from "src/js/app.js"
```

**Cause:** Session 39 delivered `src/js/privacy-screen.js` as a **NEW** file alongside several
**REPLACE** files — easy to miss when pasting into GitHub's web UI, since a REPLACE overwrites a
path that already exists in the file browser, while a NEW file has to be deliberately created at a
path that isn't there yet. It never made it into the commit; `app.js`'s `import { setPrivacyScreen }
from './privacy-screen.js'` (also from Session 39) had nothing to resolve against. `npm install`
(step 5, just before) succeeded fine — the new dependency itself, `@capacitor-community/privacy-screen`
in `package.json`, was committed correctly; only the wrapper module was missing.

**Fix:** re-delivered `src/js/privacy-screen.js` unchanged (confirmed against the sandbox copy,
which still builds clean — 99 modules, zero errors — with nothing else in the repo touched). **Why
correct:** the error names the exact missing path and nothing else; every other file this depends
on (`package.json`, `capacitor.config.json`, `app.js`) was already confirmed present since `npm
install` and module resolution got as far as needing this one specific file before failing.

No code changed this session — this was a missing-file diagnosis, not a bug in anything that was
written. Logged here anyway since it's exactly the kind of gap that could recur with any other NEW
file in a future session's delivery, worth remembering as a pattern, not just a one-off.

Decisions made: none.

Next session start point: unchanged — once this file is actually committed, the same workflow run
(or a fresh push) should get past step 6 for the first time and reach the actual APK build/signing
steps, which is the real first test of everything built across Sessions 25–40. Re-upload the next
log bundle (or report success) so the device-pass debt can finally start closing.

---

**Session 40**

No new request pending, so continued down the standing list of unblocked Phase 9 items (same
pattern as Sessions 16/17/35) — built the recurring backup-reminder banner (ARCHITECTURE §9).

New `backupReminderDue()` (app.js) checks whichever of `credentials.last_backup_at`/
`last_drive_backup_at` is more recent — either backup path keeps the "forgot app password"
recovery flow usable, so only the most recent of the two matters, not local specifically. Due at
30+ days, same threshold the spec named. Suppressed when there are zero entries (nothing yet worth
losing) so a brand-new install isn't nagged on day one — without that check, `last_backup_at` being
unset (`0`) would always read as "over 30 days," which is true but misleading for an install with
nothing in it yet.

Rendered as `renderBackupReminder()`, following the exact populate-then-wire shape already used for
digest/on-this-day right above it in the same DOMContentLoaded block. New `#backup-reminder-banner`
in Home's header, hidden unless due. Two buttons, built dynamically (same as the recording-timer
UI elsewhere) rather than static markup: "Back up now" jumps into Settings' Backup & Restore
section (`switchMainTab('settings')` + `.open = true` + `scrollIntoView`), "✕" dismisses — in-memory
only, not persisted, so it reappears at the next cold launch rather than going silently snoozed for
weeks if forgotten. Deliberately a plain in-app banner, not a push notification — simpler, and
"soft" in the spec only requires never blocking anything, which this doesn't either way.

Fixed in passing while updating ROADMAP.md's Phase 9 intro line: it still listed "gradient mode
toggle (done, 2 color pickers, same-color allowed)" as a current feature, though Decision 53 (Session
29) removed gradient mode entirely — a stale line left over from before that removal. Corrected to
say removed, not done.

Verified with a real `npm install` + `vite build`: builds clean. All ids cross-checked between
`app.js` and `index.html` — clean except the same two pre-existing dynamically-created ids
(`recording-stop-btn`, `recording-timer`) flagged every session, plus this session's own two new
dynamically-created ids (`backup-reminder-btn`, `backup-reminder-dismiss-btn`), same non-issue for
the same reason.

Decisions made: none — implementing an already-specified feature, not a new design call.

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward
still stands, now also covering whether the banner's `scrollIntoView` actually lands correctly and
whether the 30-day threshold feels right in practice. If picking up more unblocked work instead:
quick-capture widget, expense charts, map view, and the confidence-confirmation chip remain open
from Phase 9; label autocomplete UI/batch-add wiring remains open from Phase 8; obfuscation/
ProGuard/signature check remain open from Phase 10.

---

**Session 39**

Idea from last session's screenshot-blocking complaint: make Privacy Screen a real Settings toggle
the person can flip themselves, instead of something only adjustable by editing CI.

Session 37/38's approach (`scripts/patch-mainactivity.js` hand-patching `MainActivity.java`) was
fundamentally build-time — the flag got set once at process start, no way for JS to change it
afterward short of a rebuild, which is exactly why Session 38 could only disable it entirely rather
than offer a real switch. Replaced it outright with `@capacitor-community/privacy-screen`, after
checking npm for a real, compatible, maintained option first rather than writing more native code —
found one. Pinned to `5.2.0` specifically: its `peerDependencies` is `@capacitor/core ^6.0.0`, the
only version line of this plugin that matches this project's Capacitor 6 (its own newer major
versions, 6.x/8.x, need Capacitor 7/8 — an unrelated numbering coincidence). Verified by downloading
the actual `5.2.0` tarball and reading its shipped `PrivacyScreenPlugin.java`/`PrivacyScreen.java`
directly rather than trusting the README: `enable()`/`disable()` do exactly `window.addFlags`/
`clearFlags(FLAG_SECURE)` on the current Activity, real runtime toggles, nothing more.

`capacitor.config.json`'s `PrivacyScreen.enable` set to `false` so native startup never turns it on
by itself. New `src/js/privacy-screen.js` (mirrors `biometric.js`'s shape) applies whatever's
actually stored, called at the same point in `bootstrap()` as `applyAppearance()` — before the lock
screen renders, so the setting also covers the lock screen when on. Persisted via `metaGet`/
`metaSet` under `privacy_screen_enabled`, same mechanism as `dark_mode`/`auto_backup_enabled` — off
by default. New `#privacy-screen-settings` card in Settings, same populate-then-wire pattern as the
dark-mode toggle right below it.

Retired the old approach rather than leaving it as dead weight: `scripts/patch-mainactivity.js`
deleted, `build-android.yml`'s already-commented-out step calling it removed outright,
`android-notes/native-setup.md` §13 rewritten to describe the new plugin instead of the old patch.

One known gap stated plainly: a cold launch has one or two frames between the WebView painting and
`setPrivacyScreen()`'s call resolving, during which an "on" setting isn't in effect yet — no fix
without native code reading a persisted native-side flag before `onCreate()` finishes, out of scope
for what's otherwise a plain web-layer setting.

Verified with a real `npm install` + `vite build`: 99 modules (plus the new dependency), zero
errors, same class of pre-existing benign warnings as always. All ids cross-checked between `app.js`
and `index.html` — clean except the same two pre-existing dynamically-created ids flagged every
session. `capacitor.config.json`, `package.json`, and the YAML workflow all parse cleanly.

Decisions made: 64 (replaces 62/63's approach entirely, doesn't just amend it).

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward
still stands, now also covering whether this plugin's `enable()`/`disable()` actually work as
expected on a real device (the sandbox could verify the source and the build, not runtime behavior
on Android). If picking up more unblocked work instead: quick-capture widget, expense charts,
backup-reminder nudge, map view, and the confidence-confirmation chip remain open from Phase 9;
label autocomplete UI/batch-add wiring remains open from Phase 8; obfuscation/ProGuard/signature
check remain open from Phase 10.

---

**Session 38**

Reported: `FLAG_SECURE` (Decision 62) was getting in the way of taking screenshots during active
development — expected, since screenshot-blocking is an inherent side effect of the same flag that
blanks the recents preview, not a separate switch. Asked whether deleting `scripts/patch-mainactivity.js`
was the right way to turn it off — no: `build-android.yml` still calls it, so deleting the file
would have turned a clean disable into a CI failure (missing file) instead.

Disabled by commenting out the one CI step that calls the script, not deleting anything — the
script stays, untouched and still correct, so re-enabling later is a one-line uncomment rather than
rebuilding the feature. Updated ARCHITECTURE.md, `android-notes/native-setup.md` §13, and ROADMAP.md
to say clearly that this is built-but-disabled, not built-and-live, so a future session doesn't
assume recents actually blanks on a real device right now.

Decisions made: 63 (disable, not delete — reasoning above).

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward
still stands. When development is far enough along that screenshots aren't needed as often,
re-enabling Decision 62 is uncommenting the step in `build-android.yml` — worth doing before any
build meant to be used for real, since the feature was requested for exactly that use case.

---

**Session 37**

Two requests: recents-preview should go dark/blank like Opera Incognito's task-switcher behavior;
the power button should be restyled to a transparent-background red-outline icon (matching a
supplied reference image) instead of a solid red circle, with its confirmation dialog removed and
the button itself removed from the app-open lock screen.

**Recents-preview blanking** needs Android's `FLAG_SECURE` on `MainActivity`'s window — no
Capacitor-layer or JS equivalent exists. Since `android/` is never committed (Decision 49), a hand
edit to a real `MainActivity.java` would be discarded the next CI run, so this follows
`patch-manifest.js`'s exact pattern: a new `scripts/patch-mainactivity.js`, run in CI right after the
manifest patch, that finds the stock override-free `MainActivity.java` Capacitor's template
generates and inserts an `onCreate()` setting the flag — idempotent (skips if already patched),
aborts loudly rather than guessing if the stock file's shape doesn't match what it expects. Tested
in this session's sandbox against a reconstructed stock file (real `android/` doesn't exist outside
actual CI): correct output on first run, clean no-op on a second run, braces balanced. One flag, two
effects, not separable — the recents preview goes blank (the actual ask) and screenshots/screen
recording are blocked system-wide as a side effect; documented in ARCHITECTURE.md and
`android-notes/native-setup.md` §13 as a feature of the same piece, not a bug, so it isn't mistaken
for one later. This specific native behavior hasn't run on a device yet — same standing gap as
everything since Session 25, called out again here since it's a new native code path, not just UI.

**Power button restyle:** replaced the emoji-on-solid-circle button with an inline SVG (line + open
arc, the standard power-icon shape), transparent background, `stroke="currentColor"` tied to
`color: var(--danger)` — reads correctly in both themes with no dark-mode override needed, since
`--danger` doesn't change between themes the way `--bg`/`--fg`/`--card-bg` do. Confirmation dialog
removed — tapping now calls `exitApp()` immediately (still guarded by the existing
`Capacitor.isNativePlatform()` check for the browser-preview case). Hidden specifically on
`#lock-screen` via one line in `showScreen()`; every other screen, including first-run and the
Vault's own PIN gate, keeps it — "the unlock screen" wasn't fully unambiguous (the Vault's gate says
"Unlock Vault" too), read as `#lock-screen` since that's the one whose actual purpose is unlocking
the app itself; flagged in Decision 62 as an assumption in case that's not what was meant.

Verified with a real `npm install` + `vite build`: 99 modules, zero errors. All ids cross-checked
between `app.js` and `index.html` — clean except the same two pre-existing dynamically-created ids
(`recording-stop-btn`, `recording-timer`) flagged in every prior session's check.

Decisions made: 62 (all four changes this session, one decision, all UI/native-config, no
schema/credential/backup-format change).

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward
still stands, now also covering whether `FLAG_SECURE` actually blanks recents and doesn't break
anything else (camera preview, screenshots for support requests, etc. — worth knowing about before
relying on it). If picking up more unblocked work instead: quick-capture widget, expense charts,
backup-reminder nudge, map view, and the confidence-confirmation chip remain open from Phase 9;
label autocomplete UI/batch-add wiring remains open from Phase 8; obfuscation/ProGuard/signature
check remain open from Phase 10.

---

**Session 36**

Requested UI restructure: bottom nav reduced from three tabs to two (Home, Vault); the main app's
Settings moved from a nav destination to an "App Settings" tile inside Home; the Vault's three
scattered settings sections (biometric toggle, auto-lock, Vault Trash) consolidated into one
"Vault Settings" tile inside the Vault screen; a small red circular power button added, fixed
top-right, present on every screen, force-closing the app.

`#settings-tab` itself is untouched — same accordion of settings sections, same lazy-render-on-open
listeners — only its entry point changed, from the nav's third button to the new tile plus a
"← Back to Home" link at the top of the panel. `switchMainTab()` picked up one line: viewing
Settings now keeps the Home nav icon highlighted (Settings is conceptually part of Home, not a
destination of its own) instead of leaving nothing highlighted.

Vault's three sections moved into one `<details id="vault-settings-card">`: the biometric row and
the auto-lock select flattened to plain subsections (no id changes, so no JS wiring needed beyond
what already existed); Vault Trash stayed its own nested `<details>` since `renderVaultTrash()` is
lazily triggered by its own `toggle` event, and flattening it would mean it re-renders every time
Vault Settings opens rather than only when Trash itself does. Extended the existing `#settings-tab`
chevron-card CSS (Decision 53) to also cover `#vault-settings-card` and its nested details, fixing
a pre-existing, never-flagged gap where Vault's settings had always rendered as plain unstyled
`<details>` — only Home's had ever gotten the card treatment.

Power button: `#power-close-btn`, fixed `top`/`right`, living directly under `<body>` rather than
inside any `.screen` div, so it's present regardless of which screen `showScreen()` shows — no
duplication needed across first-run/lock/main/vault/ad-gate. Confirms before acting, then calls
`@capacitor/app`'s `App.exitApp()` (already a dependency, already used for `appStateChange` —
Decision 45), guarded by `@capacitor/core`'s `Capacitor.isNativePlatform()` so a browser preview
gets a plain message instead of a silent no-op. z-index sits above the bottom nav but below modal
overlays, so an open modal keeps visual priority and the button can't be tapped through it.

Verified with a real `npm install` + `vite build` before delivering: 99 modules, zero errors, same
class of pre-existing benign dynamic-import warnings as prior sessions (one new one for
`@capacitor/core`, harmless for the same reason the others are — it's already statically imported
by nearly every Capacitor plugin in the project regardless of this change).

Decisions made: 61 (this session's three changes, one decision — all UI-only, no schema/credential/
backup-format change). Also fixed in passing: DECISIONS.md had Decision 53's entry sitting out of
chronological order (after 59 instead of after 52) from an earlier edit — moved back into place,
content unchanged.

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward
still stands, now covering this session's changes too. Flagged but not resolved: whether the power
button should appear inside modal overlays wasn't specified — left underneath them (visible, not
tappable-through) rather than assuming either way. If picking up more unblocked work instead:
quick-capture widget, expense charts, backup-reminder nudge, map view, and the
confidence-confirmation chip remain open from Phase 9; label autocomplete UI/batch-add wiring
remains open from Phase 8.

---

**Session 35**

Picked up an unblocked Phase 9 item rather than waiting further on the standing device-pass debt
(Sessions 25–34), same pattern as Sessions 16/17 — nothing here needed a device to build or verify.

Built the "Your Data" transparency screen (ARCHITECTURE §7), specified but not yet coded: a Settings
section showing total entry count and an on-device storage estimate, plus an Export Now button.
`yourDataSummary()` (app.js) counts non-deleted, non-vault entries — vault entries excluded for the
same reason `showDigest()`/`onThisDay()` already exclude them (Decision 40): this screen sits behind
the app-open password, not the Vault PIN, so a count that included vault items would leak the vault's
size to anyone with app access but not the PIN. Storage size has no such split available —
`navigator.storage.estimate()` reports the whole origin's usage, not per-category — so it's shown as
one on-device total (files + vault content + the database itself), labeled as an estimate, same
caveat already used for the backup/restore storage checks. Export Now opens the existing Backup &
Restore settings section and scrolls it into view rather than building a second export path.

New `<details id="your-data-settings">` added to Settings, positioned above Storage breakdown (the
higher-level, "is my data actually local" summary, vs. the per-category breakdown below it). Verified
with a real `npm install` + `vite build` before delivering, not just syntax-checked: 99 modules
transformed, zero errors, only the same pre-existing benign dynamic-import warnings prior sessions
have already seen. Also fixed a stale line in ARCHITECTURE.md §13 while touching it — the Status
section still listed the on-this-day/storage-breakdown screens as "not yet coded," though Session 17
built both; corrected to reflect what's actually implemented.

Decisions made: none — implementing an already-specified, already-scoped feature, not a new design
call.

Next session start point: unchanged in substance — the device-pass debt from Sessions 25 onward still
stands, now with one more built-but-unverified-on-device screen added to it (low risk: read-only
display plus a settings-open call, no data mutation). If picking up more Phase 9 work instead of the
device pass: quick-capture widget, expense charts, backup-reminder nudge, map view, and the
confidence-confirmation chip are all unblocked and unstarted.

---

**Session 34**

Fixed the Select files screen's scattered layout, reported against a screenshot — checkboxes
floating disconnected from their labels. Root cause: those checkboxes (`.cat-select-all`,
`.item-select`) had never been given a rule of their own, so they fell through to the general
`input { width: 100% }` rule and stretched, scattering their flex row. Every checkbox styled so far
had been scoped to a specific class (`.switch-row`/`.category-checkboxes`) — anything outside those
was unprotected.

Fixed at the root instead of adding a third class-scoped patch: the compact checkbox is now the
default for any plain checkbox, with `.switch-row` overriding it to the sliding-switch look where
that's wanted. Closes this category of bug for any checkbox added later, not just this one.

Also found and fixed while reviewing this file: Decision 53's entry had ended up out of
chronological order in `DECISIONS.md`, sitting after Decision 59 instead of after 52 — an artifact
of an earlier edit anchoring to non-unique text. Moved back into place, no content changed.

Decisions made: 60.

Next session start point: unchanged — the device-pass debt from Sessions 25 onward still stands.
This session's fix is CSS-only and low-risk by nature, but still unconfirmed visually on a device.

---

**Session 33**

Two bugs reported together, both traced to the same single line: a double biometric prompt on every
app open (with the app's own unlock behaving inconsistently depending on which prompt got completed
or ignored), and the Vault sometimes showing up already unlocked with no prompt at all.

Cause: `DOMContentLoaded`'s initial setup called `refreshVaultGateView()` once, unconditionally,
immediately after wiring the Vault's buttons — before the person had reached or tapped the Vault tab
at all. That function auto-triggers a Vault biometric attempt when enabled (Decision 56), so this
fired a second prompt on cold start racing against the app lock screen's own one, and could silently
set the Vault's unlock state in the background while the person was still on the app's lock screen —
so by the time they actually opened the Vault tab, it was already unlocked with nothing asked.

This call had been redundant since Session 30 (the bottom-nav Vault button already calls it fresh on
every real visit) but harmless on its own; it only became actively wrong once Decision 56 gave the
function it was calling a side effect. Removed the line entirely.

Decisions made: 59.

Next session start point: unchanged — still the same device pass owed since Session 25, now with
one more thing this session should make easier to verify (only one biometric prompt should ever
appear at app open; the Vault should never be reachable without its own prompt first).

---

**Session 32**

Product decision, not a bug: ads are deliberately not part of this build — no AdMob account, no
plugin, no gate, no ad format of any kind, notifications included. Phase 12 marked as an explicit
deferral in ROADMAP.md rather than left looking like ordinary unfinished work.

Removing the one place that called into `ads.js` turned out to matter for two bugs reported in the
same message: a full app freeze after entering, and a worse version of Session 31's biometric-timing
issue (needing a whole second unlock, not just a second tap). Two things found together:

`onUnlocked()` called the ad gate with `window.adSdk`, which has never been set anywhere (confirmed
by checking `package.json` and `capacitor.config.json` directly — no AdMob plugin has ever been
added). `ads.js`'s own try/catch already handled that safely, so this wasn't a confirmed crash by
itself, but it was real, pointless work sitting on the unlock path.

More likely the actual cause: Session 31's biometric-timing fix used a bare `setTimeout` fired from
inside `showLockScreen()`, independent of the rest of `DOMContentLoaded`'s own setup — which does
its own sequence of awaited database calls. On a slower device, the timeout could land mid-setup,
racing two chains of SQLite calls against the same connection. Reasoned from the code, not confirmed
via device logging. Fixed by moving the actual biometric trigger to the very last line of
`DOMContentLoaded`, after everything else has finished — nothing left to race against. The
window-focus delay from Session 31 stayed, just layered on top of that instead of standing alone.

Decisions made: 58.

Next session start point: unchanged in substance — a real device pass is owed for all of Sessions
25 through this one. This session specifically should make that pass easier, not harder: fewer
things happening on the unlock path, one less untested integration point (ads) removed from the
critical path entirely.

---

**Session 31**

Five bugs reported directly against real device screenshots, all fixed:

1. Auto-biometric on the app lock screen needed two attempts (worked, stayed on the same page,
   worked again on a second explicit tap). Reasoned cause: triggering the OS prompt immediately on
   cold start, before the Activity has window focus, is a known timing issue. Added a 400ms delay
   before the automatic attempt — an estimate, not a measured fix, worth revisiting after a real
   device check.
2. Bottom-nav Home/Settings did nothing while viewing the Vault — the handler changed which
   sub-tab main-screen would show without ever actually showing main-screen. Gave `showScreen()` an
   optional tab parameter so navigating there from anywhere can land on a specific tab.
3. The Vault's Lock button stayed visible even when already locked. Now hidden unless genuinely
   unlocked.
4. The biometric enable/disable toggle showed on the locked landing page, not just once inside —
   requested directly. Moved it into the unlocked content area; the actual unlock button correctly
   stays on the locked gate.
5. Locking the Vault didn't return Home immediately — the handler awaited an unnecessary async
   refresh before navigating. Now synchronous, right after lock.

Also added two more capture cards, Reminder and Location, positioned before Money — eight cards now
(Text/Voice/Image/PDF/Reminder/Location/Money/Files). Reminder reuses the existing auto-detected
`reminder` type (a reminder is a reminder, unlike Money's deliberately-kept-separate relationship to
Expense); Location is entirely new, same placeholder treatment as Money. Extended the backup
filename letters from `tvipmf` to `tviprlmf` to match — flagged as a judgment call, not something
explicitly re-specified, since the request added cards to the same set the letters were drawn from.
Capture-card grid moved from 3 to 4 columns for a clean 4x2 layout. Two new type colors (orange,
cyan) picked and contrast-checked the same way as every color since Decision 52.

Decisions made: 56, 57.

Next session start point: still the same real-device pass owed since Session 30, now covering five
more fixes on top — none of tonight's changes have been confirmed on a device either, only reasoned
through and checked for internal consistency (syntax, no duplicate IDs, contrast math). Priority if
only some of it can be tested: the Vault gate states (setup/locked/unlocked, and the Lock button's
visibility in each) and the nav fix, since those are the two that were reported as actually broken
rather than requested-and-built-fresh.

---

**Session 30**

A reported bug turned out to be a real one: tapping the bottom-nav Vault button opened straight into
Vault content with no PIN prompt. Traced it to Session 29's refactor — the actual gate elements
(`vault-pin-setup-view`/`vault-locked-view`, plus Session 26's biometric toggle) had been moved into
Settings' Private Vault accordion, disconnected entirely from `vault-screen`, which was left holding
only the content with nothing above it to gate on. A second bug in the same area: `lockVault()`
cleared the in-memory key but never cleared the vault list's already-rendered HTML, so even a
reconnected gate wouldn't have hidden content from a previous unlock.

Fixed by moving the gate back into `vault-screen` itself and rewriting `refreshVaultGateView()` as
the single function deciding all three states (needs-setup/locked/unlocked) and toggling the
content's visibility as part of that, rather than the content being permanently shown. Every unlock
path now routes through it. `lockVault()` now also clears the rendered lists directly.

Also: both the app lock screen and the Vault gate now auto-attempt biometric unlock as soon as they
show, when enabled, instead of waiting for a button tap — requested directly. Fire-and-forget, same
success/failure paths a manual tap already used; the Vault's version is guarded against re-prompting
on every re-render while still locked.

Added Money as a sixth capture type — card only, its real capture flow deliberately not built yet.
Kept it separate from the existing auto-detected `expense` type rather than reusing that machinery
without being asked to. Threaded through everywhere the other five types are: vault support, backup
category (letter `m`), labels, a new red accent color (the one hue not already in use). Tapping it
shows a plain "coming soon" rather than falling into the generic file-picker path, which would have
been wrong for an undefined type.

Redesigned both capture bars as a 3-column card grid (icon + label) instead of a thin icon-only row —
Home's bar gains text labels it never had. Checked contrast again for the newly-visible labels on
the category colors, same discipline as Sessions 28–29.

Renamed backup/append output files to `backup_<letters>_<timestamp>.dz` /
`append_<letters>_<timestamp>.dz`, exactly as specified, with two flagged (not silently assumed)
defaults: an "x" placeholder if none of the six lettered categories are selected, and the standard
`yyyymmdd_hhmmss` timestamp shape (read the request's extra "h" as a likely typo rather than building
it literally). Old `.dzbackup`/`.zip` extensions still accepted on import so existing backups keep
working; nothing is written with them going forward. Found and cleaned up a stale leftover from
Decision 53 while in this file: backup.js's App Settings category still listed the removed gradient
meta keys.

Decisions made: 54, 55.

Next session start point: unchanged in spirit, more urgent in practice — this session touched the
Vault's actual security gate, which makes an on-device pass non-optional before trusting any of
Sessions 25 through this one further. Priority order if only some of it can be tested: (1) the Vault
gate fix — confirm tapping the Vault tab always asks for the PIN/biometric when locked and never
shows stale content, (2) biometric auto-prompt on both locks, (3) everything else already queued.

---

**Session 29**

Second visual pass, requested directly: drop Gradient mode entirely (keep dark mode), replace the
single long scrolling page with a proper modern layout, switches instead of checkboxes.

Gradient mode: removed end to end — CSS custom properties and rules, the JS that wrote them, the
toggle and two color pickers in Settings. Nothing to migrate; appearance was always stored as loose
key-value rows in the generic `meta` table, not schema columns, so the old rows just sit there
unread from now on, harmless, including inside any backup made before this session.

Layout: main-screen split into a Home panel (capture bar, search, timeline) and a Settings panel
(everything that used to be a wall of stacked `<details>` below the timeline on the same page), switched
by a new fixed bottom nav — Home / Vault / Settings. Vault needed no restructuring, it was already
its own screen; the nav button just calls the same `showScreen('vault-screen')` every existing
unlock path already called. `showScreen()` itself now also drives the nav's visibility and active
state, centrally, rather than leaving that to every call site.

Every standalone on/off setting is now a sliding switch — a pure CSS restyle of the existing
checkbox inputs (`appearance: none` + a `::before` thumb), no id or JS listener touched. Backup/
restore's multi-select category checkboxes deliberately stayed compact checkboxes rather than
switches — a different kind of choice (pick-several vs one on/off), and five switches in a row for
that would have read wrong. `<details>` sections in Settings got a custom rotating chevron and card
treatment instead of the browser's bare disclosure triangle, which was doing a lot of the "dated"
work by itself.

Checked contrast again before shipping, same as last time: found and fixed the bottom nav's active
and inactive tab-label colors, both slightly under AA at small text size — added a dedicated
per-theme active-color token and raised the inactive label's opacity rather than leaving either as a
near-miss. Also swapped two newer CSS features (`color-mix()`, `:has()`) that would render fine on a
recent Chrome for explicit per-theme tokens and an HTML class instead, given this app's minSdk 22
means some real device could plausibly carry an older system WebView.

Decisions made: 53.

Next session start point: unchanged — still need a real CI build and device pass covering
everything from Sessions 25 through this one before any of it moves from "reasoned through" to
"confirmed."

---

**Session 28**

Visual redesign, requested directly against two screenshots showing the actual on-device look — flat
grey/white, one blue accent, described accurately as dated. Rebuilt `style.css` around a colorful
default theme, kept fully separate from the existing opt-in Gradient mode (untouched, still exactly
what it was).

Design: one brand accent (indigo/violet) for primary actions, plus five category colors — one per
capture type (Text/Voice/Image/PDF/Files) — applied everywhere that type shows up: capture-bar
buttons, every list row's left edge, the Vault's filter tabs. Same five hues, same meaning, everywhere
they appear, so the color coding only has to be learned once. `app.js` gained `data-type="..."` on six
row-render call sites that previously only put the type in text, so the new CSS selectors have
something to match against — additive only, checked the type vocabulary already lined up with the
capture buttons' own `data-type` values before relying on it.

Checked accessibility before shipping, not after: computed real WCAG contrast ratios for every
text-on-color pairing in a sandbox rather than eyeballing it. Found and fixed two real failures —
white button-label text on the lighter category colors (amber/teal/green) in the Vault's capture bar,
and the primary-button gradient's lighter end in both light and dark theme. Fixed by separating
button-fill colors from brand-as-text colors (different contrast requirements: white-on-fill vs
text-on-page-background) rather than trying to satisfy both with one token.

Decisions made: 52.

Next session start point: unchanged — still need a real CI build and device pass covering Sessions
25–28 together (voice-recording permissions, biometric unlock, real per-build versioning, and now
this visual pass) before any of it can be called confirmed rather than reasoned-through.

---

**Session 27**

Two things this session. First, informational only: the app icon was replaced directly on GitHub by
hand (`resources/icon.png`) — no code or doc change needed, that's exactly the one-file swap
`native-setup.md` §2 already documents.

Second, a real bug: every build still installed as version 1.0 no matter what changed. Same root cause
as Decision 49, one layer over — extracted `@capacitor/cli`'s actual android template and confirmed
`android/app/build.gradle` unconditionally ships `versionCode 1` / `versionName "1.0"`, and since
`android/` is never committed (regenerated fresh every run), nothing had ever overridden that. Every
build since Session 1 has been identically versioned.

Fixed with the same pattern as `patch-manifest.js`: `scripts/patch-version.js` (new), run by CI right
after it. `versionCode` comes from GitHub's own run number — always increasing, no manual step, nothing
to forget. `versionName` is `package.json`'s version plus that same run number, so an installed build
can be identified from Settings > Apps without checking CI logs. Tested against the real extracted
Capacitor template in a sandbox: correct rewrite, correct overwrite on a second run with a different
number, correct refusal (not a silent guess) when the run number isn't set.

Decisions made: 51.

Next session start point: unchanged from Session 26 — get a build through CI and onto the device. This
build will be the first to carry a real version string, which should make it obvious at a glance
whether the device actually picked up the new APK before testing anything else in it (voice recording
permissions, biometric unlock, both locks independently).

---

**Session 26**

Built biometric (fingerprint/face) unlock as an alternative to typing the app-open password or Vault
PIN, requested explicitly — off by default, toggled independently per lock, never a fourth credential.

Plugin choice took real checking, not just picking the first match: the obvious package
(`capacitor-native-biometric`) turned out to target Capacitor 3 and depend on `jcenter()`, dead since
2021 — would likely have broken the CI build. Found and verified `@capgo/capacitor-native-biometric@6.0.4`
instead by downloading both packages and reading their manifests/gradle directly: peer dep
`@capacitor/core@^6.0.0`, modern `google()`/`mavenCentral()`-only gradle, matches this project exactly.

Also read the chosen plugin's native Java source before trusting its security model: its Keystore key
requires the device to be unlocked but does not require a fresh biometric check to decrypt — the actual
gate is enforced in `src/js/biometric.js`'s own call order (verify, then read), not by hardware on every
access. Documented this plainly rather than overselling it (`native-setup.md` §10, `ARCHITECTURE.md` §3).

Built: `src/js/biometric.js` (new — thin plugin wrapper); two new `credentials` columns plus `db.js`'s
first real migration helper (`ensureColumn()`, needed since the existing on-device test install's DB
predates these columns and `CREATE TABLE IF NOT EXISTS` doesn't retrofit them — same class of gap as
Decision 49, different layer); enable/disable/unlock wiring in `app.js`, reusing `attemptUnlock()` and
`unlockVault()` as-is rather than a second copy of "what counts as correct"; settings UI (toggle +
confirm-password/PIN sub-form) in both the App lock and Vault sections, plus a biometric button on the
lock screen and the Vault gate. Changing or removing either password/PIN now also clears its stored
biometric secret automatically, so a stale cached value can never unlock (or derive a vault key from)
a credential that's no longer correct.

Also removed `@capawesome-team/capacitor-android-foreground-service` from `package.json` — the dead
dependency flagged in Session 25, confirmed unused anywhere in `src/js/`.

Decisions made: 50. Fixed three stale `native-setup.md §10` cross-references (ARCHITECTURE.md,
DECISIONS.md, ROADMAP.md) left over from inserting the new §10 ahead of the old Google Drive section,
which shifted to §11.

Not done: no device test yet — this is new code on top of Session 25's not-yet-built permission fix, so
neither has been run through CI or a real device at this point. Native biometric prompts in particular
can't be meaningfully verified any other way (no emulated fingerprint sensor in this sandbox).

Next session start point: get a build through CI and onto the device. Three things need confirming
together, in order — (1) Session 25's manifest-permission fix (voice recording end-to-end), (2) this
session's biometric enable/unlock flow for both locks, (3) that enabling biometric for one lock doesn't
interfere with the other (they use separate `server` keys in the plugin's storage, but that's reasoning
from the code, not something confirmed on a real device yet). If biometric enrollment isn't available on
the test device, the settings rows should simply stay hidden (`isBiometricAvailable()` returning false) —
worth checking that path too, not just the happy one.

---

**Session 25**

Followed up on Session 24's flagged-but-unverified question: does `cap-voice-rec` self-declare
`RECORD_AUDIO`, or does the app's manifest need it added by hand? Answered by downloading the actual
package and reading its shipped `AndroidManifest.xml` rather than inferring from its README — it
self-declares neither `RECORD_AUDIO` nor any permission for the foreground service it does declare
(`foregroundServiceType="microphone"`).

That second half turned into the bigger finding. Capacitor 6's default `targetSdkVersion` is 34
(checked against Capacitor's own upgrade docs), and Android 14 requires `FOREGROUND_SERVICE` +
`FOREGROUND_SERVICE_MICROPHONE` declared for a microphone-typed foreground service or the OS throws a
`SecurityException` at `startForeground()` — a native-side crash no JS try/catch can catch, same shape
as Decision 47's GoogleAuth crash. Untested until now since Session 24 only built the recording UI.

Checking where to actually add these permissions surfaced a deeper, structural gap: **`android/` has
never been committed to the repo**, across all 24 prior sessions — `build-android.yml`'s "add platform
if missing" branch has been true every single run. `android-notes/native-setup.md` §3's permission list
has existed since Session 1 but had no mechanism to ever reach a real manifest; any hand-edit would be
silently thrown away the next CI run. This means every permission in that list — not just the two new
ones — has been documentation only, never actually built into any APK, including the ones already
confirmed working on-device (Sessions 22–24 only exercised file/note capture, which doesn't need any of
them; voice recording, reminders, and location would have been the first real test, and hadn't happened
yet).

Fix: `scripts/patch-manifest.js` (new), run by CI right after `npx cap add android` — inserts any of the
eight required permissions not already present in the freshly-generated manifest, idempotently. Verified
by running it against a representative sample manifest in a sandbox: correctly inserted all eight, then
correctly no-op'd on a second run. `build-android.yml` updated with one new step calling it.

Also noted, not removed: `package.json`'s `@capawesome-team/capacitor-android-foreground-service`
dependency is unused anywhere in `src/js/` — likely left over from before `cap-voice-rec` was chosen.
Flagged for a future cleanup pass, not urgent.

Decisions made: 49.

Next session start point: get a build through CI with this change and back on the device — this is the
first real test of voice recording, and the first time *any* of the documented manifest permissions will
have actually shipped in a built APK. If recording works end-to-end (permission prompt, record, stop,
save, playback), continue the standing test checklist: search, edit, local backup/restore, Vault. Also
worth a quick sanity check once on-device: confirm the permission *prompts* (not just presence in the
manifest) actually appear for RECORD_AUDIO/location/notifications at the expected first-use moments,
since a declared-but-never-requested permission is a different bug than what was just fixed here.

---

**Session 24**

Two real bugs reported from continued testing:

1. **PDF/image capture accepted literally any file type.** The file `<input>` for both had no
   `accept` attribute at all — same generic picker used for every non-text type. Fixed with a
   per-type `acceptForType()` helper (`image/*`, `application/pdf,.pdf`; `file` stays intentionally
   unrestricted).

2. **"Record voice" actually asked to upload an existing audio file instead of recording one.**
   Bigger gap: voice capture was routed through the exact same generic file-picker as image/pdf/file
   — there was never any actual recording UI built, matching Phase 5's already-documented
   "not implemented" status, but worth fixing now that it's blocking real testing. Added
   `cap-voice-rec` (Decision 48) after real verification, not assumption: queried npm directly for
   the actual current version (`6.0.1`, chosen specifically because its major version tracks
   Capacitor's own — the same version-mismatch mistake that caused the Google Sign-In crash last
   session was worth actively avoiding here), installed it in a sandbox, and read its actual shipped
   type definitions rather than trust its README, which had a real inconsistency about its own
   return shape (a `path` field mentioned in one section that doesn't exist in the actual types).
   Built a record/stop UI with a live timer, wired into both the main and vault capture bars.

Not done: the noise-reduction toggle from the original Phase 5 plan — `cap-voice-rec` has no
audio-source parameter to expose it. Tracked separately in `android-notes/native-setup.md` §5;
basic recording (what was actually broken) is fixed.

Verified the same way as every session since the crash saga began: ran a real `npm install` +
`vite build` after adding the new dependency, not just a syntax check — confirmed the plugin bundled
correctly with zero unresolved imports.

Decisions made: 48.

Next session start point: continue the test checklist — confirm voice recording actually works
end-to-end on-device (permission prompt, record, stop, save, playback), then search, edit, local
backup/restore, Vault. Also worth confirming on the next device test: whether `cap-voice-rec` needs
a manual `RECORD_AUDIO` manifest permission or self-declares it — flagged as unverified in
`android-notes/native-setup.md` §5.

---

**Session 23**

Real functional testing began — file, image, and note capture all confirmed working, tags display
correctly, and all five per-file actions render per entry. First real bug from actual usage rather
than a crash: every action button rendered full-width and stacked instead of sitting compactly in a
row. Cause: `.drive-backup-row`'s button-sizing CSS rule was scoped to `#drive-backup-list .drive-backup-row`
from when it was first written (Session 11), before the same class got reused generically across the
main timeline, vault list, and trash bins. Everywhere except the Drive list fell back to the
universal `input, button { width: 100% }` default. Fixed by removing the `#drive-backup-list` scope
so the rule applies everywhere the class is used, plus `flex-wrap` so a row of five buttons wraps
onto multiple lines on narrow screens instead of overflowing.

Verified the same way as the last several sessions: ran the actual Vite build after the fix, not
just eyeballed the CSS — bundles clean.

Decisions made: none — a scoping bug, not a design call.

Next session start point: continue the test checklist — search, edit, local backup/restore, Vault.

---

**Session 22**

**First successful device load, after three straight sessions of crashes (18–21).** Screenshots
confirm: first-run setup renders correctly (both optional-credential fields, matching the spec
exactly), the main screen shows digest/on-this-day/timeline/capture bar, "Select files" opens with
correct empty-state text rather than erroring on empty data, and all eight settings sections render
(Storage breakdown, Trash, App lock, Private Vault, Appearance, Backup & Restore, Google Drive
Backup, Append files from download).

This confirms the three fixes from the last three sessions weren't just individually correct but
actually work together in a real build: the SQLite import fix (Session 18), the Vite bundler
addition (Session 19), and the GoogleAuth null-check guard (Session 21). Nothing new built or fixed
this session — just recording the milestone, since it's the first time in this project's history
that anything has been confirmed running on an actual device.

Decisions made: none.

Next session start point: real functional testing, not crash-hunting. Priority order, cheapest and
most foundational first: capture one of each type and confirm it appears with all five actions;
search; tags; edit; local backup then restore (the one place a bug would be worst to discover late);
Vault setup, capture, search, its own trash; auto-lock (idle and backgrounding). Skip Drive and ads
for now — both still need manual setup that hasn't happened (OAuth client, AdMob unit), so failures
there are expected, not bugs to report.

---

**Session 21**

Got the real crash log via ADB (`adb logcat -d`, after some detours — a third-party crash-viewer app
couldn't read another app's logs at all, since that's an OS-level restriction on Android since 4.1,
not a tool problem). Root cause, confirmed in the actual stack trace, not inferred:

```
Caused by: java.lang.NullPointerException: Attempt to invoke virtual method
'android.content.Intent com.google.android.gms.auth.api.signin.GoogleSignInClient.getSignInIntent()'
on a null object reference
  at com.codetrixstudio.capacitor.GoogleAuth.GoogleAuth.signIn(GoogleAuth.java:81)
```

This is a narrower, later crash than the two from Sessions 18–19 — it only fires when
`GoogleAuth.signIn()` is actually called (tapping "Connect Google Drive"), and confirms both earlier
fixes (the SQLite import, the Vite bundling) were real and correct — the app is loading and running
fine otherwise. The plugin's native `signIn()` has no null-check on its internal sign-in client;
with no client ID configured (removed as a hedge last session), calling it throws an uncaught
`NullPointerException` **inside the plugin's own compiled Java code**, which kills the whole app
process before anything can reach JavaScript. Confirmed via the stack trace that no JS-side
try/catch could have caught this regardless of how it was written — the crash happens on Capacitor's
own "CapacitorPlugins" native thread, before a promise resolution/rejection is even possible.

Fixed with a `GOOGLE_DRIVE_CONFIGURED` guard in `gdrive.js` (currently `false`) that stops the app
from ever calling `GoogleAuth.signIn()` until it's manually flipped — documented alongside the
real-client-ID setup step in `android-notes/native-setup.md` §10 so both are done together, not one
forgotten. Also added try/catch around the two UI call sites (`drive-connect-btn`,
`handleDriveBackupNow`) that previously had none, for clean error display generally, though the
guard itself is what actually prevents the crash — a JS-side catch alone would not have been enough.

Verified the same way as last session, not just asserted: ran the real Vite build again after these
changes — still bundles cleanly, same benign warnings, zero errors.

Decisions made: 47.

Next session start point: back on the device for a fourth attempt. If this clears — three
distinct, confirmed root causes fixed across three sessions (SQLite import, missing bundler, Google
Auth null-check) — that's real, substantial forward motion after a long stretch of untested code.
Standing items otherwise unchanged: CI status, Phase 13's actual OAuth setup (still not done, by
design — the guard means that's fine to defer).

---

**Session 20**

Third device test — no longer a blank screen (confirms Session 19's Vite fix actually worked), but
a native "Dumpzone keeps stopping" crash instead. This is a different failure class entirely: JS
module loading now succeeds, and something is throwing at the native Android layer.

**No confirmed root cause this session — being explicit about that rather than shipping another
guess as if it were certain.** Investigated several plausible candidates against real documentation
rather than assuming:
- `@capacitor-community/sqlite`'s documented Android minimums (`minSdkVersion 22+`,
  `compileSdkVersion 33+`) — checked Capacitor 6's actual default `variables.gradle` values and
  confirmed they already satisfy this. Ruled out with reasonable confidence.
- `@codetrix-studio/capacitor-google-auth` reads its client ID from `capacitor.config.json` at
  native startup, and that file held a literal placeholder (`REPLACE_WITH_ANDROID_OAUTH_CLIENT_ID...`)
  since the Google Cloud setup isn't done yet. Couldn't confirm whether this crashes at plugin load
  or only fails later at sign-in time — but since Drive backup is unused until the OAuth setup is
  complete regardless, removed the `GoogleAuth` config block entirely as a no-regret hedge rather
  than carrying a guaranteed-invalid value for no benefit. Documented in
  `android-notes/native-setup.md` §10 to re-add once real credentials exist.

Recommended getting an actual stack trace via a free Play Store logcat-reader app (no root/computer
needed) rather than continuing to guess through plugin documentation one at a time — a real
exception trace will confirm which candidate (if either) is responsible, or reveal something not yet
considered.

Decisions made: none — investigation and one hedge, not a confirmed fix or a new design call.

Next session start point: get the actual crash log. Everything else stands until then — confirming
CI status, and now this native crash, are both blocking real verification of six-plus sessions of
accumulated app logic that's never actually run.

---

**Session 19**

Second device test, same blank screen. This time the cause went much deeper than a single fixable
line: **the app has never been able to load, in any session, on any real device or browser** —
`www/` had no bundler and no import map, and every file that imports a Capacitor plugin
(`notifications.js`, `backup.js`, `fileactions.js`, `gdrive.js`, and `app.js` after last session's
fix) uses `import { X } from '@capacitor/...'`, a bare specifier a browser's native ES module loader
cannot resolve on its own. ES module resolution happens before any code runs, so one unresolvable
import anywhere in the dependency graph blocks the whole script — exactly the blank-page symptom,
both times. Last session's `window.sqlitePlugin` fix was a real, necessary fix for a real bug, but
it replaced one crash with a normal `import` of the same package, which has the identical failure
mode underneath. The reason 18 sessions of `node --check` never caught this: Node's module
resolution *can* resolve bare specifiers from `node_modules`; a browser's cannot, without a bundler
or an import map. Checking with the wrong tool gave false confidence the whole time.

Fixed by adding Vite — the standard choice for exactly this situation. `src/` is now the real
source directory (every file that used to live in `www/` moved there, unchanged); `www/` becomes
Vite's build output (`vite.config.js`: `root: 'src'`, `outDir: '../www'`, so
`capacitor.config.json`'s `webDir` needed no change at all). `build-android.yml` now runs `npm run
build` before `cap sync`. Checked Vite's actual current version against npm data rather than
assuming from memory — it's on major version 7 now (`^7.3.2`), not the 5.x a stale assumption would
have produced.

**Verified this actually works, rather than asserting it and handing back a third blank screen:**
ran `npm install` and `npx vite build` for real in a sandbox before delivering anything. 82 modules
transformed, build succeeded, and the output was grepped for any remaining unresolved
`@capacitor`/`@zip`/`@codetrix` import statements — zero found. This is the first time in the
project's history that the actual bundled output has been confirmed loadable rather than assumed to
be, because it's the first time anything was actually built end-to-end outside of a GitHub Actions
runner I can't inspect.

No application code changed as part of this fix — every import statement across every file was
always correct JavaScript; the only thing missing was the build step that makes bare specifiers
resolvable in a real browser engine. Also added: `.gitignore` (didn't exist before — `node_modules/`
was never excluded, though nothing had committed it yet since nothing had ever run `npm install`
outside CI).

Decisions made: 46.

Next session start point: get this rebuilt and back on the device — third attempt, but the first one
built on a verified, actually-tested fix rather than a fix that only looked correct on paper. If a
screen finally renders, that's the real first signal after seven sessions of accumulated surface. Old
`www/*` files remain committed in the repo but are now stale/vestigial — safe to delete whenever
convenient, not urgent, since CI regenerates them fresh every run regardless of what's committed.

---

**Session 18**

First real device test, first real device bug — screenshot showed a completely blank screen on
launch. Cause: `bootstrap()` read `window.sqlitePlugin`, a global nothing anywhere ever set. It's a
leftover from the original pre-existing scaffold (predates every session in this log), never caught
because nothing had actually run the app on a device until this test. `SQLiteConnection` ended up
`undefined`, `new SQLiteConnection(...)` threw immediately inside `bootstrap()`, and since
`DOMContentLoaded` awaits `bootstrap()`, the whole script halted before any `showScreen()` call ever
ran — every screen stays `hidden` by default, so the result is exactly a blank page in the
background color. Fixed: import `CapacitorSQLite`/`SQLiteConnection` directly from
`@capacitor-community/sqlite` (already a real dependency) instead of reading a global.

Audited for the same bug shape before handing this back, rather than fixing only the one reported
symptom: found a second, separate bug of the identical class. Every `window.Dumpzone.X` function is
nested under that object, but several button handlers (Share/Download/Download-for-append/Edit,
restore-from-trash, permanently-delete) called bare `window.X()` — none of which exist at that path.
These would have thrown the moment anyone clicked past the (now-fixed) blank screen. Fixed all six
call sites to go through `window.Dumpzone.X`.

Also: `crypto.js` and `intents.js` had never been staged into the working sandbox across any prior
session (only ever read, never modified, so never copied in) — meaning no session's syntax checks or
audits had ever actually covered them. Staged and checked now: both clean, both have zero imports and
zero `window.` references, so neither is at risk of this bug class at all.

Checked `ads.js`'s `window.adSdk` reference too, since it's the same shape — confirmed safe, already
wrapped in try/catch with the correct no-fill fallback (Decision 14), unlike the two that broke.

Decisions made: none — bug fixes, not new design calls.

Next session start point: get this rebuilt and back on the device. If the blank screen is gone and
first-run setup renders, that's real progress — six sessions of untested surface just got its first
actual signal. Standing items unchanged otherwise: CI status, Phase 13's OAuth setup.

---

**Session 17**

Built the digest, on-this-day, and storage breakdown (ARCHITECTURE §6). `showDigest()` has had real
query logic since early on but nothing ever rendered it to `#digest` — fixed, and while fixing it,
found it also never excluded vault entries from its counts. A "3 notes today" digest counting 1
public + 2 vault notes would have hinted that vault activity happened, without revealing what — the
same class of leak Decision 40 (Session 13) already closed for search and autocomplete, just not
checked against this surface at the time. Fixed the same way: `is_private=0` added to the query.

`onThisDay()` and `storageBreakdown()` are new. Both exclude vault entries for the same reason as
the digest fix above — neither needed a new decision, just applying Decision 40 somewhere it hadn't
been checked yet. Storage breakdown reuses `fileactions.js`'s `getSelectableEntries()` for sizes
rather than computing them a second way; its "clear items older than 30 days" action reuses the
existing bulk-delete path (soft-delete into the 30-day trash, not permanent).

Also cleaned up: `onUnlocked()` was calling `showDigest()`/`showMainTimeline()` and discarding the
results — actual rendering has always happened via separate functions in `DOMContentLoaded`, so
those calls did nothing. Removed rather than left as confusing dead code.

Decisions made: none — applying Decision 40 to two surfaces it hadn't been checked against yet, not
a new design call.

Next session start point: same standing items — confirm the CI run is green, Phase 13 still blocked
on manual OAuth setup, nothing run on an actual device across six sessions of accumulated surface
now. Remaining Phase 9 items (data-transparency screen, quick-capture widget, expense charts,
backup-reminder nudge, map view, confidence-confirmation chip) are unblocked whenever picked up.

---

**Session 16**

Checked whether `credentials.auto_lock_minutes` — a column that's existed since Session 1 — was
actually enforced anywhere. It wasn't. Built it: `armAppAutoLock()`/`disarmAppAutoLock()`, reset via
one delegated document-level listener (click/keydown/input) rather than manually calling it from
every capture/search/browse function — the vault's own auto-lock (Session 13) needed several
follow-up patches specifically because it relied on remembering to call `armVaultAutoLock()` at every
touchpoint; this one is built to not repeat that mistake. No-op when quick access is enabled, since
there's nothing to lock back to.

Also found, while working in this area: `privateSessionKey`'s own comment has said "cleared on vault
lock/background" since the Vault pivot (Session 13), but nothing ever actually listened for the app
backgrounding — the vault would stay unlocked indefinitely across app-switches if someone left and
returned within the idle window. Added `@capacitor/app`'s `appStateChange` listener
(`registerBackgroundLock()`, called once in `bootstrap()`), which now immediately locks both the
vault and the app-level password screen on backgrounding, not just after idle timeout. New
dependency: `@capacitor/app`.

Added a Settings control (`#app-auto-lock-select`) mirroring the vault's existing one.

Decisions made: 45.

Next session start point: same standing items — confirm the CI run is green, Phase 13 still blocked
on manual OAuth setup, nothing run on an actual device yet. Phase 9's remaining items (digest,
on-this-day, storage breakdown, expense charts, map view, quick-capture widget,
confidence-confirmation chip, backup-reminder nudge) are all unblocked, unstarted work for whenever
device-testing priority (flagged last session) isn't the more pressing choice.

---

**Session 15**

Closed the one item left open from last session: Edit had no UI button anywhere. Wiring it exposed
two real bugs already sitting in `editEntry()` (fileactions.js), fixed before anything called it:

1. It expected `fields.text` for vault entries but `fields.body_text` for non-vault notes — a
   mismatch that meant calling it consistently from one UI (which is what building the Edit button
   required) would have silently no-op'd whichever side didn't match the field name it happened to
   check. Normalized on `fields.text` for both; non-vault notes map it to `body_text` internally.
2. Expense/reminder-specific fields (`amount`, `expense_category`, `fire_at`, `repeat_rule`) were
   checked — to decide whether to call the reminder-reschedule callback — but never actually written
   to the `updates` object, so editing a reminder's time or an expense's amount would have silently
   done nothing to the row at all. Fixed by copying any of those four present in `fields` into
   `updates`, same as `label` already was.

Built the shared `editEntryUI()` prompt flow (label always; text for notes, prefilled from
`body_text` or, for vault notes, from the in-memory index's `searchableText` — which is already the
note's own decrypted text; amount/category for expenses; date/time for reminders, parsed and handed
to `scheduleReminder` via the existing `rescheduleReminder` callback hook). Wired to both the main
timeline and vault list.

Decisions made: none — bug fixes and UI wiring for an already-flagged gap, not new design calls.

Next session start point: nothing specific left flagged from Phase 4/7/8's UI wiring. Standing items
unchanged: confirm the CI run is green, Phase 13 still blocked on manual OAuth setup, nothing run on
an actual device yet — that last one is worth prioritizing once the OAuth setup is done, since a
meaningful amount of untested surface has accumulated across several sessions now.

---

**Session 14**

Follow-on from the Vault pivot, checking for the same class of gap on the non-vault side and
finding it: the main app's capture bar had no click listeners at all (only the vault's got wired
during the pivot), and there was no equivalent of `captureToVault()` for the main app —
`captureText`/`saveNote` only ever handled typed text, never voice/image/pdf/file.

Built: `captureFile()` (mirrors `captureToVault`, unencrypted, writes to
`Directory.Data/files/<uuid>.<ext>`, the existing storage convention from Decision 20), wired to the
main capture bar. Rendered the main timeline for the first time — `showMainTimeline()` has returned
rows since early on, but nothing ever displayed them or attached the five per-file actions (built in
Phase 7, unused since) to anything. Now both the main timeline and vault list show Share/Download/
Download-for-append/Delete per entry, plus tags (one bulk `GROUP_CONCAT` query for the main
timeline, matching the bulk approach `vault.js`'s index already used for its own tags). Built a
shared `promptForTags()` — same `prompt()`-based rough edge as other one-offs already flagged
elsewhere, wired into both main and vault capture (vault capture previously collected no tags at
all).

Noted, not fixed: captured images/PDFs have no OCR text yet — that's genuinely Phase 5's job (native
plugin wiring), not something to fake here. Labels/tags still make everything findable meanwhile.

Decisions made: none — this was closing gaps already flagged in ROADMAP (Phase 4/7's missing
timeline UI), not new design calls.

Next session start point: Edit still has no UI button anywhere (function exists, nothing calls it).
Same standing items beyond that: confirm the CI run is green, Phase 13 still blocked on manual OAuth
setup, nothing in the app run on an actual device yet.

---

**Session 13**

Pivot, spanning a large chunk of the app: app-open password made optional; "Private Notes"
generalized into a full Private Vault covering Text/Voice/Image/PDF/Files, with the explicit goal
of full functional parity with the main app (same capture, edit, tags, search, trash, five per-file
actions). Decisions 36–44. New file: `vault.js`. Changed: `db.js`, `backup.js`, `fileactions.js`,
`app.js`, `index.html`, `style.css`.

Built: `vault.js` (content bundling — note text unchanged, file types get a small JSON envelope of
base64 bytes + OCR text, encrypted together via the exact same `encryptPrivateNote`/
`decryptPrivateNote` calls private notes always used; save/load; the in-memory-only search index,
chosen over a persistent encrypted index specifically to avoid a second on-disk artifact that has
to stay in sync with the vault's actual contents). Optional app-password flow, including a
first-run setup screen that — it turned out — never existed before at all, only the check-against-it
path did. The vault's own trash bin and its own auto-lock timer, independent of the main app's.
Share/Download reversed to work identically for vault and non-vault entries, on explicit direction
overriding an earlier, more cautious default. The "Select files" multi-select screen, shared between
main and vault, category-grouped with per-item size.

Bugs found and fixed, in order of how serious they were:
1. **Two real privacy leaks**, both pre-existing, surfaced while designing how vault content should
   be indexed: `insertEntry()` (db.js) and `restoreBackup()` (backup.js) were writing every entry's
   **label** — not just its body — into `entries_fts` and `label_history` unconditionally, meaning
   a vault item's title was discoverable via ordinary, no-PIN search and could surface as an
   autocomplete suggestion in the normal capture bar. Fixed in both places.
2. **A foundational gap**: nothing anywhere ever toggled screen visibility. `unlock-btn` had no
   click listener; `main-screen` was never actually shown after a successful unlock. Every UI
   section built across every session since backup/restore first got a UI has been technically
   unreachable until `showScreen()` was added this session while wiring the optional-password flow.
3. Bulk "download for append" would have thrown for every vault item in a multi-select — no PIN was
   ever collected for that flow. Fixed by prompting once upfront when the action needs it.
4. Deleting a vault item (single or bulk) never rebuilt the in-memory search index, so a deleted
   item would keep appearing in vault search/browse until the next unlock. Fixed both call sites.
5. `saveNote()`'s pre-pivot private-notes path duplicated logic that now belongs to `vault.js` and
   bypassed it entirely (no index rebuild, no shared save path). Redirected to delegate to
   `captureToVault()` instead of maintaining two ways to create a private text entry.

Verified before presenting, not just written: every JS file syntax-checked (`node --check`), and
every `getElementById()` call in `app.js` cross-referenced against `index.html`'s actual ids —
zero missing.

Decisions made: 36–44.

Next session start point: same standing items — confirm the CI run is green, Phase 13 (Drive)
still blocked on manual OAuth setup. Nothing in this pivot has been run on an actual device yet,
same flagged risk as everything else. Vault UI is functional but plain (`prompt()`/`confirm()` for
capture and some per-item actions) — a real design pass is still Phase 4's job, not done here.

---

**Session 12**

Built: `www/js/fileactions.js` — Phase 7, all five per-file actions. The substantial new piece is
Download-for-append: rather than inventing a separate sidecar/encryption format, it wraps one entry
in the exact same payload shape `buildBackupPayload()` already produces for full backups (added an
`entryIds` filter to that function), encrypts it the same way `encryptBackup()` always has, and zips
that single JSON file. A `mode` field (`passkey`/`default`/`pin`) travels unencrypted alongside it so
import knows what to prompt for. The "Append files from download" multi-select screen
(`importAppendZips()`) is built and wired into `index.html`/`app.js`; it hands each decrypted file
straight to `backup.js`'s existing `restoreBackup()`, so import shares one dedup engine with local and
Drive restore rather than a fourth implementation of the same logic.

Resolved the standing zip-library choice with evidence, not habit: checked JSZip against `@zip.js/zip.js`
on current maintenance data before picking — JSZip's last release was 2022, maintenance score zero;
zip.js ships regularly, zero dependencies, TypeScript-typed. Added `@zip.js/zip.js` and
`@capacitor/share` to `package.json`.

Bug fixed, found while designing the private-notes export path (not by hunting for bugs — it fell out
of actually tracing through the cross-PIN restore code to decide how private-note exports should
work): Session 5's `restoreBackup` cross-PIN append branch referenced `entry._sourcePinSalt`, a field
`buildBackupPayload` never set anywhere. Fixed by having `buildBackupPayload` capture the device's one
`private_pin_salt` once per payload (`payload.privateNotesSalt` — one PIN, one salt, not per-entry)
and having `restoreBackup` read that instead. Cross-PIN private-notes append was silently broken until
this fix; ordinary same-PIN append was unaffected.

Share/Download(plain)/Edit/Delete are implemented and callable but have no UI buttons — flagged as a
Phase 4 gap (no rendered entry list exists yet to attach them to), not left ambiguous as a Phase 7 one.

Decisions made: 33–35.

Next session start point: same standing items — confirm the CI run is green; Phase 13 still blocked
on the manual Google Cloud Console steps. Phase 7's per-file zip flow and Phase 6/13's UI are all
testable together once a build succeeds and Phase 4 gets enough of a real timeline to attach
Share/Download/Edit/Delete buttons to.

---

**Session 11**

Built: the actual UI for both Phase 6 (local backup/restore) and Phase 13's settings (Drive connect,
auto-backup config, Drive backup list). `index.html` gets a Backup & Restore section and a Google
Drive section (both plain `<details>` blocks, matching Appearance's existing bare style — no visual
design pass, Phase 4 still owns that); `style.css` gets minimal supporting styles (checkbox groups,
a modal-overlay pattern, a warning-text color); `app.js` wires all of it to `backup.js`/`gdrive.js`.

Real design point resolved while wiring, not left implicit: **a due auto-backup can't run fully
silently**, because the backup passkey is never stored (existing credentials rule) and
`checkAndRunAutoBackupIfDue()`'s `passphraseGetter` hook has to come from somewhere real. Built a
small one-tap banner that asks for the passkey only when a backup is actually due, with an explicit
skip option that leaves the due-date untouched rather than silently deferring a full cycle. Recorded
as Decision 32 — this is a real behavior a future session could otherwise "simplify" back into an
unsafe passphrase cache.

Small cleanup along the way: removed `index.html`'s five separate `<script type="module">` tags for
db.js/crypto.js/intents.js/notifications.js/ads.js — `app.js` already `import`s all of them, so those
were redundant (harmless, since ES modules execute once per URL regardless, but pure clutter).

Known rough edge, flagged rather than hidden: Drive's restore and manual-backup flows use plain
`prompt()`/`confirm()` for the passkey and mode choice, while local restore and the auto-backup-due
check use the nicer purpose-built dialogs. Inconsistent, functional, worth revisiting once Phase 4
does a real design pass rather than this bare-skeleton styling.

Decisions made: 32.

Next session start point: same standing blocker on Phase 13 — none of the Drive UI can be
device-tested until the manual Google Cloud Console steps are done and a real OAuth client ID
replaces the placeholder in `capacitor.config.json`. Phase 6's local backup/restore UI has no such
blocker and can be tested as soon as a build succeeds. Also still standing: confirm the CI run is
green, and Phase 7's zip-library choice.

---

**Session 10**

Built: `www/js/gdrive.js` — the actual Drive backup engine (Session 9 was design + manual-setup docs
only). Auth via silent `GoogleAuth.signIn()`, folder find-or-create, storage check against Drive's
`about.get` quota, multipart upload of the exact same encrypted archive `backup.js` produces, a
read-only preview function, and restore that delegates to `backup.js`'s existing `restoreBackup()`
rather than duplicating its append/overwrite/dedup logic. Small schema addition: `credentials` gets a
`last_drive_backup_at` column alongside the existing local `last_backup_at`.

Resolved, with evidence rather than assumption, the two things Session 9 left open:
- **No refresh-token store of our own** (Decision 30) — dropped `grantOfflineAccess` from
  `capacitor.config.json`; the native SDK's own cached-consent sign-in is enough since every Drive
  call happens in the foreground.
- **Auto-backup scheduling mechanism** (Decision 31) — checked `@capacitor/background-runner`
  against its actual Capacitor 6 documentation before deciding, rather than assuming it would work:
  its headless environment has no SQLite or Filesystem access, so it literally cannot read entries or
  build a backup payload. Chose a check-on-app-open/resume design instead
  (`checkAndRunAutoBackupIfDue()`), consistent with the project's existing preference for local
  notifications over AlarmManager. Tradeoff stated plainly in the decision: no app open means no
  auto-backup that cycle — visible and honest rather than a silent background promise that might not
  hold.

Decisions made: 30–31.

Next session start point: unchanged blocker — none of `gdrive.js` can be device-tested until the
manual Google Cloud Console steps (`android-notes/native-setup.md` §10) are done and the real OAuth
client ID replaces the placeholder in `capacitor.config.json`. Once that's done: the Settings UI
(connect/disconnect, manual "Backup now to Drive", frequency picker) and wiring
`checkAndRunAutoBackupIfDue()` into app startup. Also still standing: confirm the CI run is green,
and Phase 7's zip-library choice.

---

**Session 9**

Built: planning + scaffolding for Phase 13 (Google Drive backup), no runtime code yet — this phase
has real prerequisites (an OAuth client that doesn't exist) the same way Phase 1's signing did.
`ARCHITECTURE.md` gets a new §4b design section; `capacitor.config.json` gets the `GoogleAuth` plugin
config block with a placeholder client ID; `package.json` gets
`@codetrix-studio/capacitor-google-auth` (chosen over the newer Capawesome Google Sign-In plugin
specifically because it supports Capacitor 6 — the project's current pin — while Capawesome's needs
Capacitor 8, a separate, larger upgrade not undertaken for this); `android-notes/native-setup.md`
gets §10, the Google Cloud Console walkthrough (project, Drive API, OAuth consent screen, Android
OAuth client, SHA-1 from the existing release keystore).

Researched before committing to any of it: confirmed `drive.file` is Google's non-sensitive scope
tier (basic verification only, not the restricted-scope security assessment full/readonly Drive
access needs) and confirmed that a Testing-status consent screen gets 7-day refresh token expiry for
any non-basic scope — both facts drove Decisions 26–27 and the explicit "publish to Production" step
in the setup walkthrough. Also confirmed the current recommended Capacitor Google Sign-In plugin
requires Capacitor 8 before picking the older, Capacitor-6-compatible alternative instead.

"Fully offline" language in `ARCHITECTURE.md` §1/header changed to "offline-first" per this session's
confirmed direction (Decision 29) — done now, ahead of the feature shipping, since the docs already
needed to describe an opt-in exception (Decision 25) regardless of when the code lands.

Decisions made: 25–29 (Drive backup is opt-in/additive; `drive.file` scope only; consent screen must
be Production, not Testing; same encrypted-archive format as local; auto-backup is time-based with
manual backup always available, plus the offline-language update).

Next session start point: same standing items (confirm the Actions run is green; Phase 6 UI or
Phase 7's zip-library choice) plus, whenever you've done the manual Google Cloud Console steps in
`android-notes/native-setup.md` §10: `gdrive.js` (mirrors `backup.js`'s create/restore shape against
Drive's API instead of the filesystem) and the background-scheduling choice for auto-backup, still
open per ARCHITECTURE.md §4b.

---

**Session 8**

Built: `resources/icon.png` background changed from dark navy to white. Un-mixed the flat, uniform
original background (`RGB(11,13,27)`, confirmed uniform by sampling) against white per-pixel rather
than a flat color swap, so soft anti-aliased edges around the folder/shield don't leave a dark fringe.
Same artwork footprint as Session 7's full-bleed crop — only color changed, not size/position — so no
regression on the double-padding fix from Decision 24.

Note: corners are pure white, but pixels nearer the artwork sit slightly off-white (~231–239) since
the original art's own soft drop-shadow, previously invisible against the dark canvas, is now a faint
visible vignette. Left as-is — reads as intentional depth, not a defect — but flagging in case a
perfectly flat white is wanted instead.

Decisions made: none (cosmetic asset change, not a new rule).

Next session start point: unchanged — confirm the Actions run is green, then Phase 6 UI or Phase 7's
zip-library choice. Re-confirm the icon on-device once built, same caveat as Session 7.

---

**Session 7**

Bugs fixed: app icon rendered visibly smaller than sibling dock icons on-device (user-reported,
confirmed via screenshot comparison). Cause: the source `resources/icon.png` already carried ~17–20%
margin (flagged as a risk in Session 6, not yet wrong at that point); `@capacitor/assets` applies its
own adaptive-icon safe-zone inset on top of whatever source it's given, so the two paddings stacked
into a double shrink. Fix: re-cropped the same artwork to its actual content bounding box and
re-centered it full-bleed (~2–6% margin) before handing it to the generator. Why correct: removing
the redundant margin leaves only the generator's own inset, matching how sibling icons are padded.

Decisions made: 24 (icon source images must be full-bleed; a rule for any future icon swap, not just
this one).

Next session start point: same as before this detour — confirm the Actions run is green, then either
the backup/restore UI (Phase 6) or a zip-library choice for Phase 7. Re-confirm the icon visually
once a build with the new crop is installed; the fix is based on measurement + the known cause, not a
second on-device screenshot.

---

**Session 6**

Built: app icon pipeline. Source artwork saved as `resources/icon.png` (1254×1254, no transparency).
Added `@capacitor/assets` as a devDependency and a `generate-icons` script; `build-android.yml` now
runs `npx @capacitor/assets generate --android` right after the `android/` platform exists (fresh or
committed) and before `cap sync`, so every mipmap density and the adaptive-icon layer are generated
from that one file on every CI run — no per-density PNGs to hand-produce or commit.

Checked (not just assumed): measured the artwork's actual margin against Android's adaptive-icon safe
zone — ~16–20% on every side, against a ~17% recommended minimum. Close enough that it should survive
a circular/squircle launcher mask, but this is a measurement against a spec, not a device screenshot;
flagged as unconfirmed in `android-notes/native-setup.md` §2 and `ROADMAP.md`.

Decisions made: 23 (icon generated from one source file via CI, not hand-crafted per density).

Next session start point: same as before — confirm the Actions run is green (still can't check this
from here), then either the backup/restore UI screens (Phase 6) or a zip-library choice for Phase 7.
The app icon can be visually confirmed once any CI build succeeds and the APK is installed.

---

**Session 5**

Built: `www/js/backup.js` — Phase 6 core engine. Create/encrypt/write a full or selective backup
(`createBackup`), open/decrypt without touching the DB (`openBackupFile`/`decryptBackupPayload`),
restore in append or overwrite mode with UUID dedup and storage sanity checks (`restoreBackup`), and
extract a category's raw files + JSON sidecar to device storage with no DB import
(`extractCategoryToStorage`). No UI wiring yet; not run on a device.

Bugs fixed (caught during this session's own build, before commit): `createBackup` initially wrote
`last_backup_at` to the `meta` table — wrong; `db.js`'s schema keeps it on the single-row
`credentials` table. Fixed to `UPDATE credentials SET last_backup_at=? WHERE id=1`. Separately, the
first draft of `buildBackupPayload`/`restoreBackup` ignored that tags are a many-to-many join
(`entry_tags`), not a column on `entries` — payload now collects each entry's tag names via a join
query, and restore re-creates the `tags` rows and `entry_tags` links (and updates `label_history`,
matching what a normal capture does via `insertEntry`, since restore bypasses that helper to preserve
original `created_at`).

Also fixed, pre-existing (Phase 2, not previously flagged): `entry_tags` has `ON DELETE CASCADE`
foreign keys, but nothing in `db.js` ever sets `PRAGMA foreign_keys = ON`, so hard deletes were
leaving orphaned `entry_tags` rows. Surfaced by writing overwrite-mode restore's delete step; the
same gap already existed in `purgeOldTrash`. Fixed both with an explicit `DELETE FROM entry_tags`
before the parent row delete, rather than relying on a pragma that capacitor-community/sqlite may not
carry across every connection.

Decisions made: 20–22 (file storage path convention; full backup is a single encrypted JSON, not a
zip; overwrite restore is destructive only within selected categories, not a full wipe by default).

Next session start point: Phase 6 has no UI yet — build the backup/restore screens (category
checkboxes, mode selector with the two always-shown descriptions, storage-check display, the
overwrite confirmation dialog). After that, or in parallel if CI is confirmed green by then: Phase 7
(per-file actions), which needs a zip library decision first (Decision 21 explicitly left this open).

---

**Session 4**

Bugs fixed: pre-rebrand name "Actioner" (see Decision 12) survived in four places after the app was
renamed to Dumpzone — `db.js` (header comment, SQLite filename `actioner.db`), `app.js` (global
`window.Actioner`), `notifications.js` (channel id, channel description, notification title), and
`android-notes/native-setup.md` (keystore-generation example used alias `actioner`, but the actual
release keystore generated in Session 3 uses alias `dumpzone`). Cause: rebrand (Decision 12) was
applied to `index.html`, `package.json`, and `capacitor.config.json` but not swept across `www/js/`
or `android-notes/`. Fix: renamed all four to `dumpzone`/`Dumpzone` equivalents; `native-setup.md`'s
keystore section also notes the live keystore already exists under alias `dumpzone`. Why correct:
matches the app id (`com.dumpzone.app`) and the actually-generated keystore; no live installs exist
yet so the DB filename and channel id changes have no migration cost.

Also corrected: ROADMAP.md's Phase 0 checkbox was still unchecked despite both docs and scaffold
being confirmed committed on `main` — closed it. Phase 1's package-name and signing sub-items
checked off to match Session 3's completed work.

Built: nothing new — this was a verification + correction session.

Decisions made: none (bug fixes and a stale-doc correction, not new product/architecture decisions).

Next session start point: confirm (manually, via the GitHub Actions tab — not verifiable from here,
API rate-limited) that the workflow run succeeds end-to-end with the four signing secrets set; this
finally closes Phase 1's signing sub-item and Phase 11's "not yet confirmed" caveat. Then AdMob
account + app icon (Phase 1, manual) or start Phase 6 (backup & restore engine), the largest unbuilt
piece and fully specced already in ARCHITECTURE.md.

---

**Session 3**

Bugs fixed: CI failed at `:app:validateSigningRelease` — cause: the four signing secrets
(`KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`) were never set in the repo (log
showed them decoding/resolving as empty); separately, the workflow passed the keystore as a bare
relative filename, which Gradle resolved against the wrong working directory even independent of the
missing-secrets issue. Fix: `build-android.yml` now passes an absolute path
(`"$(pwd)/app/release.keystore"`) and fails fast with a clear message if the keystore is empty or any
of the three password/alias secrets are unset. Why correct: absolute path removes Gradle's ambiguous
resolution; fail-fast turns a cryptic downstream error into an immediate, actionable one.

Built: release keystore generated (alias `dumpzone`, 2048-bit RSA, 10000-day validity) and provided
to the account holder for safekeeping outside the repo; corresponding values set as the four GitHub
secrets above.

Decisions made: none (bug fix + one-time infra step, not a product/architecture decision).

Next session start point: confirm the workflow run succeeds end-to-end now that secrets are set;
closes Phase 1's signing sub-item.

---

**Session 2**

Fixed: `build-android.yml` was missing from the delivered zip — the zip command's exclude pattern
wrongly matched the `.github` folder itself, dropping it entirely. Cause: `-x ".*"` matches any
top-level path starting with a dot, including directories, not just stray dotfiles. Fix: rebuilt the
zip without that pattern. Why correct: the workflow file was present on disk the whole time; only the
packaging step was wrong.

Built: dark mode + gradient mode. `www/css/style.css` rewritten with CSS custom-property theme
tokens (`data-theme`, `data-gradient` attributes) and a blurred glow/spillover background layer.
`www/js/app.js` adds `getAppearance()`/`setDarkMode()`/`setGradientMode()`, reading/writing the
existing `meta` table (no schema change). `www/index.html` gets a basic Appearance settings block
wired to these functions.

Decisions made: 18–19 (see DECISIONS.md).

Next session start point: Phase 1 (keystore, AdMob account) — manual, needs direct account action.
Appearance settings still need real UI polish (this session only wired a bare `<details>` block).

---

**Session 1**

Built: `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/ROADMAP.md`, `docs/SESSIONS.md`,
`docs/TRACK.md`. Repo confirmed empty before this session.

Existing local scaffold not yet uploaded: `www/`, `capacitor.config.json`, `package.json`,
`.github/workflows/build-android.yml`, `android-notes/native-setup.md`.

Bugs fixed: none (seed session).

Decisions made: 1–17 (see DECISIONS.md).

Next session start point: upload the scaffold — Phase 0 isn't closed until docs and scaffold are
both committed. Then Phase 1 (keystore, AdMob account) — manual, needs direct account action.
