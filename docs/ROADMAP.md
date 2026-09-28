# Roadmap

## Minimum (quick reference)

- [x] 0 — Repo scaffold & docs
- [ ] 1 — Infra & secrets
- [ ] 2 — Core data layer
- [ ] 3 — Capture & intent engine
- [~] 4 — App shell & control flow (5 reported bugs fixed — Session 31; 8 capture cards, 3 still placeholder: Reminder/Location/Money)
- [~] 5 — Native plugin wiring (voice recorder + permissions done, needs device confirm; OCR/noise-toggle/sound asset not)
- [~] 6 — Backup & restore engine (core logic done, UI wired, not device-tested)
- [~] 7 — Per-file actions (all five built, now attached to the main timeline)
- [~] 8 — Tags & label UX (basic tag picker wired to both main + vault capture)
- [~] 9 — Additional features (auto-lock, digest, on-this-day, storage breakdown, Your Data screen, backup-reminder banner done; rest not started)
- [~] 10 — Security hardening (biometric unlock done, needs device confirm; Privacy Screen done as a real Settings toggle — Decision 64; obfuscation/ProGuard/signature check not)
- [ ] 11 — CI/CD
- [ ] 12 — Ads integration
- [ ] 13 — Google Drive backup (optional, opt-in, core logic done, no UI, blocked on manual OAuth setup)
- [~] 14 — Private Vault (Text/Voice/Image/PDF/Files, encrypted at rest, in-memory search, own trash/auto-lock)

## Detailed

- [x] **0 — Repo scaffold & docs.** Docs and scaffold (`www/`, `capacitor.config.json`,
  `package.json`, `.github/workflows/build-android.yml`, `android-notes/native-setup.md`) both
  confirmed committed on `main`.
- [ ] **1 — Infra & secrets (manual).**
  - [x] Package name: `com.dumpzone.app` (set in `capacitor.config.json`).
  - [x] Android signing — keystore generated (alias `dumpzone`) and 4 GitHub secrets set. CI
    run against the live secrets not yet confirmed green — pending manual check.
  - [ ] AdMob account, one Rewarded ad unit (not Interstitial). **Deliberately not doing this yet
    (Decision 58)** — ads deferred to a future build, not this one.
  - [x] App icon — source artwork at `resources/icon.png`; CI generates all densities via
    `@capacitor/assets`. Confirmed on-device (Session 7): initial version rendered visibly smaller
    than sibling icons (double safe-zone padding — see Decision 24); re-cropped full-bleed and fixed.
  - [ ] Play Console account — not blocking; only if Play distribution is later decided on.
- [ ] **2 — Core data layer.** `db.js` (schema, FTS5, tags, label history, soft-delete),
  `crypto.js` (PBKDF2/AES-GCM). Committed; not device-tested. Stale pre-rebrand naming
  (`actioner.db`, header comment) fixed earlier. **Vault pivot:** `is_private` generalized from
  notes-only to any type; app-password columns now nullable (optional password); added
  `vault_auto_lock_minutes`, `restoreFromTrash`/`permanentlyDeleteEntry`/`listTrash`. Fixed a real
  bug found during the pivot: private entries' **labels** (not just bodies) were being written to
  `entries_fts` and `label_history` unconditionally — meaning a vault item's title was discoverable
  via ordinary, no-PIN search and could surface in the normal capture bar's autocomplete. Fixed in
  both `insertEntry()` here and `restoreBackup()` in backup.js (Decision 40).
- [ ] **3 — Capture & intent engine.** `intents.js` (reminder/expense detection, OCR label
  suggestion), `notifications.js`, `ads.js`. Committed; not device-tested. `notifications.js`'s
  stale pre-rebrand naming (channel id, titles) fixed earlier.
- [~] **4 — App shell & control flow.** `app.js`, `index.html`, `style.css`. **Foundational gap
  found and fixed during the Vault pivot:** nothing anywhere ever toggled screen visibility —
  `unlock-btn` had no click listener, `main-screen` was never shown after unlock. Every UI section
  built across every prior session (backup/restore, Drive, import) was technically unreachable
  until `showScreen()` was added. Now wired: first-run setup (optional app-password +
  optional Vault PIN), the lock screen, quick-access skip path, main screen, and the Vault
  screen. **Second gap, found and fixed the session after:** the main capture bar's five buttons
  had no click listeners at all — only the vault's got wired during the pivot. `captureText`/
  `saveNote` only ever handled typed text; there was no equivalent of `captureToVault()` for
  voice/image/pdf/file on the non-vault side. Built `captureFile()` to fill that gap (mirrors
  `captureToVault`, unencrypted, writes to `Directory.Data/files/<uuid>.<ext>` per the existing
  storage convention). Also: `showMainTimeline()` returned rows but nothing ever rendered them or
  attached the five per-file actions (built in Phase 7, sitting unused ever since) to anything —
  that's now done too, with tags shown per entry (one bulk `GROUP_CONCAT` query, not N+1).
  OCR for image/pdf still isn't implemented (Phase 5), so captured images/PDFs have no `body_text`
  yet — labels/tags still work for search. **Visual theme modernized (Session 28, Decision 52)** —
  colorful by default now (per-capture-type color coding across buttons/rows/tabs, one brand
  accent, gradient masthead accent). **Layout rebuilt (Session 29, Decision 53)**: bottom nav
  (Home/Vault/Settings) instead of one long scrolling page; Gradient mode removed entirely (dark
  mode kept); every on/off setting is a switch, not a checkbox. **Nav reduced to two tabs (Session
  36, Decision 61):** Settings dropped as a bottom-nav destination — Home now has an "App Settings"
  tile that opens the same `#settings-tab` panel, and the Vault's scattered biometric/auto-lock/
  Trash settings are consolidated into one `#vault-settings-card` tile inside the Vault screen. A
  small red power button, fixed top-right on every screen, force-closes the app
  (`@capacitor/app`'s `App.exitApp()`). **Restyled (Session 37, Decision 62):** transparent
  background, only the icon stroke red (was a solid filled circle); confirmation dialog removed;
  hidden on the app-open lock screen specifically (present everywhere else, including the Vault's
  own PIN gate). **Lock/unlock toggle + universal Back button added (Session 43, Decision 65):**
  a second small icon button, left of the power button, locks whichever of the app or the Vault is
  currently open (context-aware, checked at click time); the Vault's own former "Lock" button is
  gone, replaced by it. A third bottom-nav slot between Home and Vault is a universal Back button
  (a simple two-slot last-location toggle, not a full history stack) — every dedicated "back to
  home" element from earlier sessions is removed in favor of it. **Restyled + Vault contrast bug
  fixed (Session 44, Decision 66):** lock button now golden, thicker, persistent glow, and disabled
  (grayed out, not tappable) on Home/Settings whenever no app-open password is set; power button
  given a matching persistent red glow; back button given a new icon and a 0.5s tap-glow animation;
  lock button also now hidden on first-run setup. The Vault's lock button had been reported missing
  — actually a contrast bug (a near-black icon on the Vault's own near-black title bar), fixed by
  removing that dark bar and the bottom nav's matching dark active-Vault-tab background entirely. **Session 45, Decision 67:** the title box removed too, heading aligned with the lock/power row, lock gold brightened.
  **Session 30 (Decisions 54/55):**
  fixed a real bug where the Vault's PIN gate had been left disconnected from `vault-screen` by the
  Session 29 refactor (bottom-nav Vault button bypassed it entirely — see Decision 54); both locks
  now default to biometric on open; capture bars redesigned as a 6-card grid; Money added as a
  sixth capture type (card only — its actual capture flow, and whether it reuses the existing
  Expense machinery or stays separate, is still open for a future session). **Session 31 (Decisions
  56/57):** fixed five reported bugs — biometric double-tap on cold start (mitigated with a delay;
  Session 32 found the real cause and fixed it properly, see below), bottom-nav Home/Settings not
  working from the Vault screen, the Vault's Lock button showing while already locked, the
  biometric toggle showing on the locked gate instead of only once unlocked, and Lock not returning
  Home immediately. Added Reminder and Location as two more cards (eight total:
  Text/Voice/Image/PDF/Reminder/Location/Money/Files) — Reminder reuses the existing auto-detected
  type, Location is new; both placeholder like Money. **Session 32 (Decision 58):** Session 31's
  delay-based biometric mitigation was itself the cause of a worse bug (reported freeze, full
  second unlock needed) — the delay raced against `DOMContentLoaded`'s own setup. Fixed by moving
  the trigger to run only after that setup fully completes. Also removed the ad gate from the
  unlock path entirely (ads deferred to a future build — see Phase 12). **Session 33 (Decision
  59):** found the real cause of a double biometric prompt on open and the Vault sometimes
  appearing already unlocked — an unconditional, redundant `refreshVaultGateView()` call at cold
  start that also fired the Vault's own auto-biometric attempt before the app itself was even
  unlocked. Removed; the nav handler already covers it correctly.
  **First real device test (Session 18): blank screen on launch.** `bootstrap()` read
  `window.sqlitePlugin`, a global nothing ever set — a leftover from the original pre-existing
  scaffold that predates every session in this log, never caught because nothing had run the app on
  a device until now. Fixed: import `CapacitorSQLite`/`SQLiteConnection` directly from
  `@capacitor-community/sqlite` instead. Audit afterward found a second bug of the same shape: six
  button handlers called bare `window.X()` for functions that only exist under `window.Dumpzone.X` —
  fixed all six.
  **Second device test: still blank — root cause was much deeper (Decision 46).** The app has never
  been able to load in any session: browsers can't resolve bare npm-package imports
  (`'@capacitor/filesystem'`) without a bundler, and every plugin-importing file has always had this.
  `node --check` never caught it because Node's module resolution isn't the same as a browser's.
  Fixed by adding Vite as a real build step — `src/` is now the actual source, `www/` is build
  output (§11/§12, ARCHITECTURE.md). Verified by actually running `npm install` + `vite build` in a
  sandbox before delivering, not just asserted: 82 modules bundled, zero unresolved imports remained
  in the output.
  **Third device test: native crash instead of blank screen — confirmed the above fix worked.**
  Got a real crash log via ADB this time (Session 21) rather than guessing further:
  `GoogleAuth.signIn()`'s native code has no null-check on its internal sign-in client, so calling
  it with no client ID configured is a guaranteed uncaught `NullPointerException` inside the
  plugin's own compiled Java code — kills the whole process before JS ever sees it (Decision 47).
  Fixed with a `GOOGLE_DRIVE_CONFIGURED` guard in `gdrive.js` that prevents the app from ever calling
  that native method until a real client ID exists.
  **Fourth device test (Session 22): the app actually loads and renders.** First real success after
  three sessions of crashes. Confirmed working: first-run setup screen (both optional-credential
  fields), the main screen with digest/on-this-day/timeline/capture bar all present, "Select files"
  opening correctly with proper empty-state handling ("Nothing to select yet" rather than an error
  on empty data), and all eight settings sections rendering (Storage breakdown, Trash, App lock,
  Private Vault, Appearance, Backup & Restore, Google Drive Backup, Append files from download).
  This confirms all three fixes (SQLite import, Vite bundling, GoogleAuth guard) actually work
  together, not just in isolation. Real functional testing (capture, search, tags, edit/delete,
  backup/restore, Vault) starts now.
- [~] **5 — Native plugin wiring.** **Voice recorder done** (Session 24, `cap-voice-rec`, real
  device bug fix — see Decision 48) — record/stop UI wired into both capture bars. Required
  permissions (including two newly-found foreground-service ones for this plugin's own recording
  service) now applied automatically by CI via `scripts/patch-manifest.js`, not just documented —
  see Decision 49; not yet confirmed against a real build/device, that's next session's first task.
  Noise-reduction toggle still not done, this plugin has no audio-source parameter
  (`android-notes/native-setup.md` §5). OCR (ML Kit or Tesseract), notification sound asset:
  documented, not implemented.
- [~] **6 — Backup & restore engine.** `backup.js` built: create/encrypt/write, open/decrypt/restore
  (append + overwrite, UUID dedup, `(Restored)` label-collision suffix), selective categories with
  storage sanity checks (`navigator.storage.estimate()` — an estimate, no device free-space API
  exists in Capacitor core), safety-backup-before-overwrite with its explicit-confirmation gate,
  extract-to-storage (no DB import). UI now wired in `index.html`/`app.js`: category checkboxes,
  mode selector with both descriptions always shown, the overwrite confirmation dialog (safety-backup
  checkbox defaulted on). Not run on a device — flag as risk. **Vault pivot:** the notes-only
  `private_notes` category is now a unified `private_vault` category spanning all five vault types
  (Decision 37). Cross-PIN append path (renamed from cross-PIN-private-notes to cross-PIN-vault) is
  written but especially untested (no way to exercise it without two real devices or a manually
  crafted second-PIN backup). Still plain/unstyled — matches Phase 4's current bare-skeleton look,
  not a finished visual design.
- [~] **7 — Per-file actions.** `fileactions.js` built, all five: Share, Download-for-append
  (encrypted zip, reuses `backup.js`'s exact
  payload/archive pipeline scoped to one entry — Decision 34, zip library is `@zip.js/zip.js` not
  JSZip — Decision 33), plain Download, Edit (reminders
  reschedule via a caller-supplied callback), Delete (re-exports `db.js`'s existing `softDelete`).
  The "Append files from download" multi-select import screen is built and wired
  (`index.html`/`app.js`, `importAppendZips()`). **Not run on a device.**
  **Vault pivot — Share/Download reversed (Decision 41):** originally scoped Vault entries to
  Copy-only; overridden on explicit direction that user convenience outweighs that caution — all
  five actions now work identically for Vault and non-Vault entries, decrypting into memory first.
  Added the "Select files" multi-select screen (category-grouped, per-item size, bulk Share/
  Download/Download-for-append/Delete — Decision 44), shared between main and Vault.
  **Known rough edge:** the import screen's and bulk-download-for-append's per-file passphrase/PIN
  prompts use `prompt()`, same category of shortcut as the Drive flows' rough edge (Phase 13) —
  spec's "reuse last passkey for a session" convenience isn't wired in yet.
  Two real bugs caught and fixed while wiring the UI: bulk "download for append" would have thrown
  for every Vault item (no PIN was ever collected for that flow); deleting a Vault item never
  rebuilt the in-memory search index, so it would keep appearing in Vault search/browse until the
  next unlock. Both fixed before this was presented, not after.
  Earlier fix, still standing: Session 5's `restoreBackup` cross-PIN append bug
  (`entry._sourcePinSalt` never actually set — Decision 35).
  Now attached to real UI (Phase 4): both the main timeline and vault list render all five actions
  — Share/Download/Download for append/Edit/Delete — per entry. Wiring Edit surfaced two more real
  bugs in `editEntry()` itself, fixed before wiring anything to it: it expected `fields.text` for
  vault entries but `fields.body_text` for non-vault notes, so calling it consistently from one UI
  would have silently no-op'd whichever side didn't match; and expense/reminder fields
  (`amount`/`expense_category`/`fire_at`/`repeat_rule`) were never actually written to the row at
  all — only checked, to decide whether to call the reminder-reschedule callback — meaning editing a
  reminder's time or an expense's amount would have silently done nothing.
- [~] **8 — Tags & label UX.** `promptForTags()` built and wired into both main and vault capture —
  shows existing tags (`listAllTags()`) as a hint, free-text creates new ones (`applyTags()`'s
  `INSERT OR IGNORE` already handled "new tag" with no changes needed). Tags now display per entry
  in both the main timeline and vault list. Same `prompt()`-based rough edge as other one-offs
  flagged elsewhere in this doc — real chip UI and live autocomplete-as-you-type are still Phase 4's
  design pass, not built here. Label autocomplete (`label_history`) and batch add with
  auto-numbering (`batchAddWithCommonLabel()`) are drafted but not wired into any UI yet.
- [~] **9 — Additional features.** Digest, on-this-day, storage breakdown, data-transparency screen,
  recurring backup reminder (done — see detail below), quick-capture widget, expense charts, map view,
  confidence-confirmation chip, dark mode toggle (done, gradient mode removed — Decision 53). **Auto-lock timeout done** — `credentials.auto_lock_minutes` has existed since
  Session 1 but was never actually enforced anywhere until now (Decision 45). Also fixed while
  building it: `privateSessionKey`'s own comment has said "cleared on vault lock/background" since
  the Vault pivot, but nothing ever listened for backgrounding — the vault would stay unlocked
  indefinitely across app-switches, relying only on its idle timer. Both the app-level password lock
  and the vault now lock immediately on backgrounding (`@capacitor/app`'s `appStateChange`), not just
  after idle timeout.
  **Digest, on-this-day, and storage breakdown done.** `showDigest()` had backend logic since early
  on but was never rendered anywhere — fixed, and while fixing it, found it also never excluded
  vault entries from its counts: a "3 notes today" digest with 1 public + 2 vault notes would have
  hinted at vault activity outside the vault, the same class of leak Decision 40 already closed for
  search/autocomplete. Fixed the same way — `is_private=0` added to the query. `onThisDay()` and
  `storageBreakdown()` are new, both excluding vault entries for the same reason; storage breakdown
  reuses `fileactions.js`'s `getSelectableEntries()` for sizes rather than computing them a second
  way, and its "clear items older than 30 days" action reuses the existing bulk-delete (soft-delete,
  30-day trash, not permanent). **"Your Data" transparency screen done (Session 35):** total entry
  count (vault excluded, same reasoning as digest/on-this-day above — this screen sits behind the
  app-open password, not the Vault PIN) plus an on-device storage estimate
  (`navigator.storage.estimate()`, same estimate-only caveat as the backup/restore storage checks),
  and an Export Now button that opens the existing Backup & Restore section rather than a second
  export path. **Recurring backup-reminder banner done (Session 40):** a soft, dismissible nudge on
  Home when 30+ days have passed since the more recent of the local/Google Drive backup timestamps
  (`credentials.last_backup_at`/`last_drive_backup_at`) — suppressed on an essentially-empty
  install so a fresh setup isn't nagged immediately; dismiss is in-memory only, reappears next cold
  launch. Remaining items (quick-capture widget, expense charts, map view, confidence-confirmation
  chip) not started.
- [~] **10 — Security hardening.** **Biometric unlock done** (Session 26, `@capgo/capacitor-native-biometric`,
  Decision 50) — fingerprint/face as an alternative to the app-open password or Vault PIN, off by
  default, independent per lock; not yet confirmed on a real device. **Privacy Screen done** (Session
  39, Decision 64) — a real Settings toggle (off by default), `@capacitor-community/privacy-screen`
  (pinned `5.2.0` for Capacitor 6 compatibility) blanking the recents preview and blocking screenshots
  together, replacing Session 37/38's build-time-only `MainActivity` patch (Decisions 62/63) entirely
  — that approach is gone, not just superseded in docs; `scripts/patch-mainactivity.js` deleted. JS
  obfuscation, ProGuard/R8, startup signature check: still documented, not implemented.
- [ ] **11 — CI/CD.** `build-android.yml` committed; signing-path and fail-fast fixes applied
  (Session 3). Runs `npm run build` (Vite) before `cap sync` (Decision 46, Session 18) — without
  it, the bundled app can never load in any WebView, verified as the actual root cause of two
  consecutive blank-screen device tests. **Now also patches the freshly-generated `android/` platform
  on every run** — required manifest permissions (Decision 49) and a real, always-incrementing
  version (Decision 51), both via small idempotent-where-appropriate Node scripts in `scripts/`,
  since `android/` itself is never committed and starts from the same unmodified template every
  time. (A third such script, for `FLAG_SECURE`, existed briefly in Sessions 37–38 and is gone as of
  Session 39 — Privacy Screen is a real plugin now, not a native patch; nothing left to run in CI
  for it.) **First real CI run attempted, Session 41** — failed at the Vite build step
  (`Could not resolve "./privacy-screen.js"`), root cause a missing file from Session 39's delivery,
  not a workflow or code problem; re-delivered. **Second attempt, Session 42** — progressed past
  that (file now resolves), failed one step further in because `package.json`/`capacitor.config.json`
  from the same Session 39 delivery also hadn't been committed; re-delivered both. Live green run
  still not confirmed — manual check pending.
- [ ] **12 — Ads integration. Deliberately deferred (Decision 58) — not part of this build.** Logic
  drafted in `ads.js`, never wired to a real AdMob plugin, no plugin ever added to `package.json`.
  Session 32 removed the one call site that invoked it (`onUnlocked()`) — it was calling
  `runDailyAdGateIfDue()` with `window.adSdk` always undefined (nothing had ever set it), adding
  needless work to the unlock path for a gate with nothing behind it to open. `ads.js` itself is
  untouched, left as scaffolding for whenever this phase is actually picked up.
- [~] **13 — Google Drive backup (optional, opt-in).** Additive to Phase 6, never a replacement
  (Decision 25). Sub-items, roughly in dependency order:
  - [ ] Manual: Google Cloud project, enable Drive API, OAuth consent screen scoped to `drive.file`
    only (Decision 26), **published to Production** before relying on auto-backup (Decision 27) —
    walkthrough in `android-notes/native-setup.md` §11. **Still blocking** — nothing below can be
    device-tested until the real OAuth client ID replaces the placeholder in `capacitor.config.json`.
  - [ ] Manual: Android-type OAuth client (package `com.dumpzone.app` + release keystore's SHA-1).
  - [x] Google Sign-In plugin wired (`@codetrix-studio/capacitor-google-auth`, Capacitor 6
    compatible). No `grantOfflineAccess`/token store of our own (Decision 30) — relies on the native
    SDK's silent cached-consent sign-in instead.
  - [x] `gdrive.js` built: auth, find/create the app's backup folder, upload reusing `backup.js`'s
    exact archive format (Decision 28), storage check via Drive's `about.get` quota, restore
    delegates to `backup.js`'s existing `restoreBackup()` rather than duplicating it, plus a
    read-only preview function. Not run against a real account — no OAuth client exists yet.
  - [x] Auto-backup mechanism decided: a check on app open/resume (`checkAndRunAutoBackupIfDue()`),
    not OS-level background scheduling — `@capacitor/background-runner` confirmed incapable of
    SQLite/Filesystem access, so it can't build a payload at all (Decision 31).
  - [x] Settings UI: connect/disconnect account, manual "Backup now to Drive", auto-backup
    enabled/frequency picker (writes `meta.auto_backup_enabled`/`auto_backup_frequency`),
    storage-check display (same visual pattern as local backup's). Also the call site that invokes
    `checkAndRunAutoBackupIfDue()` on app open and supplies it a passphrase getter. **Known rough
    edge:** Drive restore/backup-now use plain `prompt()`/`confirm()` for the one-off passphrase and
    mode choice rather than the nicer modal built for local restore's overwrite confirmation and the
    auto-backup-due banner — functional, inconsistent, worth revisiting once Phase 4's real UI exists.
  - [ ] "Fully offline" language in docs/UI updated to "offline-first, optional cloud backup"
    (Decision 29) — done in ARCHITECTURE.md this session; still needs doing in any in-app copy once
    that copy exists (Phase 4's UI is still a bare skeleton).
- [~] **14 — Private Vault** (pivot; work spans Phases 2/4/6/7 above — this entry cross-references
  rather than duplicates). Text/Voice/Image/PDF/Files, unified under `is_private` (was notes-only),
  gated by the Vault PIN. See ARCHITECTURE.md §3b for the full design. Built this session:
  - [x] `vault.js`: content bundling (note text unchanged from pre-pivot; file types get a small
    JSON envelope of base64 bytes + OCR text, encrypted together via the same
    `encryptPrivateNote`/`decryptPrivateNote` calls private notes always used), save/load, and the
    in-memory-only search index (Decision 39 — built fresh on unlock, discarded on lock, never
    persisted even encrypted).
  - [x] Two real privacy leaks fixed (Decision 40): private entries' labels — not just bodies —
    were hitting `entries_fts` and `label_history` unconditionally in both `insertEntry()` and
    `restoreBackup()`. Vault-only label autocomplete now comes from the in-memory index instead.
  - [x] File encryption at rest (Decision 38) — vault entries never write file bytes to disk
    unencrypted; `file_path` stays NULL, content lives in `encrypted_body`.
  - [x] Optional app-open password (Decision 36) — first-run setup screen built (didn't exist
    before at all; only the check-against-it path did), `setAppPassword`/`removeAppPassword` for
    Settings-time changes.
  - [x] Vault's own trash bin and own auto-lock timer (Decision 42) — `vault_auto_lock_minutes`
    column, `listTrash`/`restoreFromTrash`/`permanentlyDeleteEntry` shared with the main bin,
    distinguished by `is_private` rather than a second table.
  - [x] Share/Download parity with the main app (Decision 41) — reversed an earlier, more
    restrictive default on explicit direction.
  - [x] "Select files" multi-select (Decision 44), shared between main and Vault.
  - [ ] **Not run on a device or through CI.** Same standing risk as everything else — flagged, not
    resolved, since that requires the manual Google Cloud/CI steps from earlier phases regardless.
  - [ ] Vault UI is functional but plain (prompt()/confirm() for capture and some actions) —
    matches Phase 4's overall bare-skeleton state, not a finished design.

Phases with a real dependency (e.g. 7 needs 6) are worked in order. Phases without one (e.g. 9's
individual items) can be picked up in any order once prerequisites are met.
