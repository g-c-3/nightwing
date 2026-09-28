# Decisions

Numbered log, no dates (see TRACK.md for dates). Terse, factual, no attribution to individuals.

**1. Core model.** Fully offline capture-and-action app. Voice, location, photo, PDF, file, and text
entries convert to searchable text where possible, indexed in one FTS5 timeline. No automated
inference, no external verification, no account or server. Monetized by one rewarded ad per day, max,
only if it loads.

**2. Voice notes carry no transcription.** Recorder only, with a noise-reduction toggle
(`VOICE_COMMUNICATION` audio source, not custom DSP) and a mandatory label. Removes the largest
technical risk (offline speech-to-text) at the cost of voice notes being searchable by label only —
stated explicitly in-app, not left implicit.

**3. Reminders are notifications with a tone, not alarms.** `local-notifications`, not `AlarmManager`
full-screen alarms. Simpler, more tolerant of Doze delays; doesn't force-take the screen. Compensated
with a battery-optimization-exemption prompt on first reminder and an "X scheduled" trust indicator.

**4. Expense follow-up: save first, ask after.** "Spent 500" with no category saves immediately as
`uncategorized`; a 15-second prompt asks what it was for. Never blocks on the answer; a late/no reply
just updates the row already saved.

**5. Three credentials, never conflated.** App-open password, private-notes PIN, backup passkey —
separately scoped, separately recoverable, separately hinted. App-open password recoverable via
backup-passkey-as-proof-of-ownership. Private-notes PIN has no recovery path, even via valid restore,
since the backup passkey only unlocks the archive, not PIN-derived note encryption — only a
destructive PIN reset exists. Backup passkey is never stored; forgetting it makes that backup
permanently unrecoverable. First two hints stored locally; backup-passkey hint lives in the backup
file's own unencrypted header, shown before the passkey prompt at restore.

**6. Normal restore only ever asks for the backup passkey.** App-open password and PIN travel inside
the restored database and resume automatically at their backed-up values — restoring can revert the
app-open password if it changed since backup, surfaced as a one-time notice.

**7. Append is the default restore mode; overwrite is the dangerous, explicit-opt-in path.** Append:
non-destructive, UUID-based dedup (exact match skipped but offer "restore anyway"; same-label/
different-UUID appended with a `(Restored)` suffix). Overwrite: destructive, always shown with its
consequence stated plainly. Safety-backup toggle offered for both; skipping it under append needs no
warning, skipping it under overwrite triggers a warning dialog requiring an explicit "I understand"
tap. If the safety backup itself can't fit, overwrite restore refuses to proceed. Incoming backup must
verify fully before any deletion starts.

**8. Selective backup/restore share one category list**, entry types plus Tags (definitions only,
`INSERT OR IGNORE`, no dedup needed) and App Settings (non-sensitive preferences only — never
credential hashes/salts/hints). Both directions show a storage sanity check (required vs. available)
before the action is enabled. "Extract to device storage" pulls a category's raw files out without
touching the database.

**9. Every entry gets five actions: Share, Download-for-append, Download (plain), Edit, Delete.**
Private notes get Copy (auto-clearing clipboard) instead of Share/Download. Download-for-append always
encrypts; the passkey is optional — skipping it falls back to a fixed, disclosed-as-weaker app-level
default key. Plain Download is always unencrypted, meant to leave the app for good. Per-file zip
sidecar schema (label, tags, timestamps, category) matches the full-backup format — one import/dedup
engine serves both.

**10. Tags and labels are picked from a shared, growing list, never retyped.** One tag-picker
component across every entry type (`listAllTags()`), same pattern as label autocomplete
(`label_history`).

**11. Batch add uses one common label with history-aware auto-numbering.** Multi-select Image/PDF/
Generic-File capture can suffix a running number onto a shared label (`Invoice 1`, `Invoice 2`...),
continuing from the label's last-used number rather than restarting at 1.

**12. App name: Dumpzone.** "Dumpstore" rejected — too close to Dumpster, an established Android
file-recovery app in an adjacent category. "Dumphere" cleared but replaced by "Dumpzone" after a
further conflict check also cleared, keeping the "dump it in" positioning without the "junk"
connotation risk. Package id: `com.dumpzone.app`.

**13. Security hardening ceiling: JS obfuscation, ProGuard/R8, startup signature check.** NDK-level
native obfuscation rejected as not worth the build complexity for a solo, GitHub-Actions-only
pipeline — client-side checks can only raise tamper cost, never eliminate it, since nothing here has
a server to validate against, including the ad-gate logic itself.

**14. Ad gate never blocks data access.** Rewarded ad attempted in background on first open per day,
3–5s timeout. Loads → full-screen, non-skippable. Fails/offline → gate skipped entirely, silently. No
optional/skippable middle state — an ad-gate that could lock out a user's own data over a network
hiccup would contradict the offline-first premise.

**15. Repo holds client + docs only — no server split.** Single repo, no backend to separate out.
Manual APK sideload; no Play Store plan decided yet.

**16. Files are delivered complete, never pushed directly, never as diffs.** Matches the mobile-only,
no-terminal workflow — every file is a full, ready-to-download replacement.

**17. Docs use numbers, not dates.** DECISIONS.md and SESSIONS.md entries are numbered only; TRACK.md
is the sole place mapping any of it to a real date/time.

**18. Dark mode: simple on/off, no auto-detection required for v1.** Stored as a local preference
(`meta.dark_mode`), applied via a `data-theme` attribute and CSS variables. Falls under the App
Settings backup category.

**19. Gradient mode: on/off, independent of dark mode, two user-pickable colors.** Same color allowed
for both — collapses to a single-tone glow rather than a two-tone gradient, by design, not a bug to
guard against. Rendered as a soft background gradient plus a blurred glow/spillover layer behind
cards — pure CSS, cosmetic only, no functional effect. Also falls under App Settings.

**20. Captured file storage path convention: `Directory.Data/files/<uuid>.<extension>`.** Not
previously pinned down; backup/restore needs a fixed convention to read and write against. Files are
named by the owning entry's own id, so a restored file and its DB row always agree.

**21. Full backup is a single AES-GCM-encrypted JSON archive, not a zip.** Keeps the "no external
library" stance (crypto.js only) for Phase 6; file bytes travel base64-encoded inside the JSON.
Per-file download-for-append (Phase 7, Decision 9) is a separate, later decision to use a zip
container — the two share the same entry-record schema but not the same container format.

**22. Overwrite-mode restore is destructive only within the selected categories**, not a full-device
wipe by default. Existing rows in unselected categories are untouched. Selecting every category and
choosing overwrite is how a full replace is done — there's no separate "wipe everything" switch.

**23. App icon generated from one source file via `@capacitor/assets` in CI**, not hand-crafted
per-density PNGs. `resources/icon.png` is the only file to touch when the icon changes; no separate
adaptive-icon foreground/background layers exist yet since only one flattened image was supplied —
revisit if launcher-mask cropping turns out to be a problem on a real device.

**24. Icon source images must be supplied full-bleed (artwork to the edges), not pre-padded.** The
adaptive-icon generator applies its own safe-zone inset; a source that already carries margin stacks
with that inset and renders visibly smaller than sibling icons on the launcher — confirmed on-device
(Session 7) and fixed by re-cropping `resources/icon.png` to ~2–6% margin. Any future icon swap
follows this same rule.

**25. Google Drive backup is opt-in and additive, never a replacement for local backup.** Local-only
stays the default and fully functional with no account, forever. Connecting a Google account adds a
second destination for the same backup file; nothing about local capture, search, or backup/restore
starts depending on it.

**26. Drive access uses the `drive.file` OAuth scope only** — the app can see/manage only files it
created itself, never the rest of the person's Drive. Chosen specifically because it's Google's
non-sensitive tier: basic verification only, not the restricted-scope security assessment full or
readonly Drive access would require.

**27. The Google Cloud OAuth consent screen must be published to Production, not left in Testing.**
Testing-mode refresh tokens for non-basic scopes expire after 7 days, which would silently break
time-based auto-backup about a week in. Production removes that limit without triggering full manual
verification, since the scope (Decision 26) stays non-sensitive.

**28. A Drive backup is byte-for-byte the same encrypted archive format as a local backup** (Decision
21) — same passphrase, same KDF, same "never stored" rule. Drive only ever holds the opaque encrypted
blob; one engine, two destinations.

**29. Auto-backup to Drive is time-based (daily/weekly, user-configurable)**, with manual "Backup now"
always available regardless of the auto-backup setting. Requires a background scheduling mechanism
not yet chosen (Phase 13) — everything in the app today is foreground-only or a local notification.
"Fully offline" language across docs/UI updates to "offline-first, optional cloud backup" once this
ships, since the old phrasing stops being accurate the moment Drive backup exists as a feature.

**30. No refresh-token/serverAuthCode store for Drive auth.** `gdrive.js` never requests
`grantOfflineAccess`; it calls `GoogleAuth.signIn()` fresh each time, relying on the native Android
SDK's own silent-consent caching. Nothing of our own to persist or secure beyond what Android's
account manager already handles.

**31. Auto-backup is a check on app open/resume, not `@capacitor/background-runner`.** That plugin's
headless JS environment has no SQLite or Filesystem access (confirmed against the Capacitor 6 docs) —
it cannot read entries or captured files, so it cannot build a backup payload, independent of any
timing/battery concerns. AlarmManager/a foreground service were also passed over, consistent with the
existing choice to keep reminders as local notifications rather than alarms. The tradeoff is explicit:
if the app isn't opened, no auto-backup runs — an honest, visible limit rather than a silent one.

**32. A due auto-backup shows a one-tap passkey prompt, never runs fully silently.** The backup
passkey is never stored (existing credentials rule), so `checkAndRunAutoBackupIfDue()` can't cache it
across sessions — when the schedule says a backup is due, the app shows a small banner asking for the
passkey once, with an explicit "Skip this time" option that leaves `last_drive_backup_at` untouched so
it comes due again next open rather than silently waiting out the full interval.

**33. Per-file zip export uses `@zip.js/zip.js`, not JSZip.** Checked both against current
maintenance data before choosing: JSZip's last release was 2022 with a maintenance score of zero;
zip.js ships regular releases, has zero dependencies, and is TypeScript-typed. Both work fine in a
plain browser/WebView context — only one of them is still actually maintained.

**34. Per-file "Download for append" reuses `backup.js`'s exact archive pipeline**
(`buildBackupPayload`/`encryptBackup`/`decryptBackupPayload`/`restoreBackup`), scoped to one entry via
an `entryIds` filter added to `buildBackupPayload`. The zip's single JSON entry carries an unencrypted
`mode` field (`passkey`/`default`/`pin`) so import knows what to prompt for without guessing. This
means a per-file export and a full backup are decrypted and deduped by the same code path — one
engine, three destinations (local restore, Drive restore, per-file import) rather than three to keep
in sync.

**35. Fixed a real bug found while building the above:** `restoreBackup`'s cross-PIN append path
referenced `entry._sourcePinSalt`, a field `buildBackupPayload` never actually set (Session 5).
`buildBackupPayload` now captures the device's one `private_pin_salt` once per payload as
`payload.privateNotesSalt` (not per-entry — one PIN, one salt, same for every private note on that
device), and `restoreBackup` reads that instead. Cross-PIN private-notes append was non-functional
until this fix; same-PIN append (the common case) was unaffected, since that path never touched the
missing field.

---

**Pivot: app-open password optional; "Private Notes" generalized into a Private Vault spanning
Text/Voice/Image/PDF/Files, with full feature parity to the main app. Decisions 36–44.**

**36. App-open password is optional ("quick access").** First-run setup offers it, blank means no
lock screen at all on open. Addable/removable anytime after via Settings (`setAppPassword`/
`removeAppPassword`). Doesn't touch the other two credentials — Vault PIN and backup passkey stay
exactly as independent as before.

**37. "Private" is no longer notes-only — Private Vault spans Text, Voice, Image, PDF, and Files**,
each independently browsable plus an "All" combined view. Expenses/Reminders stay out of vault scope
(not requested, don't obviously fit "vault" as a concept). Same vault PIN gates all five.

**38. Vault file bytes are encrypted at rest, not just gated behind a PIN in the UI.** Non-negotiable,
not asked as a question: a "private" photo sitting as a plaintext file anyone with file-manager
access could open would defeat the entire point. Implementation: no new column, no second encryption
scheme — a vault entry's `file_path` stays NULL, and its base64 file bytes (plus OCR text for
image/pdf, bundled together) travel inside the exact same `encrypted_body` column and
`encryptPrivateNote`/`decryptPrivateNote` calls a private note's text already used. A note's
plaintext is unchanged (still just its text) — zero migration needed for existing private notes.

**39. Vault search is an in-memory index built fresh on unlock, discarded on lock — never persisted,
not even encrypted.** Considered a persistent encrypted index and rejected it: SQLite FTS5 has no
native encryption, so a "persistent encrypted index" would still mean decrypting everything into an
in-memory FTS5 table on every unlock anyway — at which point most of the work is identical to the
simple option, minus a second on-disk artifact that has to be kept in sync with the vault's actual
contents on every add/edit/delete (a real, ongoing bug surface for a feature whose entire point is
"don't leak this"). At this scale (a personal vault, not an enterprise search problem) the
performance case for persistence doesn't hold either. Mirrors `privateSessionKey`'s existing
in-memory-only lifecycle, not a new pattern.

**40. Vault labels/content are excluded from the shared `entries_fts` search index and the shared
`label_history` autocomplete table entirely — not just the body.** Fixed two real bugs to make this
true: `insertEntry()` (db.js) and `restoreBackup()` (backup.js) were both indexing a private entry's
**label** into `entries_fts` even though the body was already excluded — meaning a vault note titled
something sensitive would surface in ordinary, no-PIN search with just an empty preview, leaking its
existence and title. Both also wrote every entry's label to `label_history` unconditionally, which
would have let a vault-only label surface as an autocomplete suggestion in the normal (non-vault)
capture bar. Vault's own search/autocomplete instead comes from the in-memory index (Decision 39).

**41. Share and plain Download work identically for vault and non-vault entries — reversed from this
session's earlier default.** Originally scoped vault items to a Copy-only alternative on the reasoning
that Share/Download inherently means the content leaves the encryption boundary as plaintext.
Overridden on explicit direction: user convenience — "they should have the ability to use the file
however they want" — outweighs that caution here. Content is still decrypted only into memory first,
never written to disk unencrypted until the moment Download is explicitly invoked; the UI should
carry a one-time notice that sharing/downloading a vault item means it's plaintext from that point on.
"Download for append" (still fully encrypted, still requires the vault PIN, never a passkey/default
option) is unaffected — that restriction was never about limiting convenience, it's about the format's
entire purpose being a safe round-trip back into a vault.

**42. Private Vault has its own 30-day trash bin, independent of the main app's**, plus its own
auto-lock timer (`credentials.vault_auto_lock_minutes`, separate column, separate from
`auto_lock_minutes`). No new table for the second bin — `deleted_at`+`is_private` are enough to
distinguish "which bin" a soft-deleted row belongs to; `listTrash()`/`restoreFromTrash()`/
`permanentlyDeleteEntry()` (db.js) are shared, generic functions, not duplicated per bin.

**43. Every per-file action's UI shows Share / Download / Download for append / Edit / Delete in that
order, identically, for both non-vault and vault entries** — full parity, per explicit direction
("every single thing in non private will be exactly replicated in private vault"), Share/Download
restriction lifted per Decision 41.

**44. "Select files" multi-select: category-grouped, size shown per item, selectable individually or
as a whole category.** Bulk actions cover four of the five (Share, Download, Download for append,
Delete) — Edit stays per-item only, bulk-editing arbitrary fields across mixed entry types doesn't
have a coherent meaning. One shared implementation for both main and vault (same parity principle).
Known limitation, not papered over: Capacitor's Share plugin has no multi-file share of its own, so a
bulk Share opens one native share sheet per item sequentially rather than a single combined share.

**45. Both locks — app-level password and Vault PIN — lock immediately on backgrounding, not just
after idle timeout.** `@capacitor/app`'s `appStateChange` listener triggers this; the app-level idle
timer itself resets via one delegated document-level listener (click/keydown/input) rather than
manually calling `armAppAutoLock()` from every capture/search/browse function individually — the
vault's own timer needed several follow-up patches for exactly that omission, so the app-level one
was built to not repeat it. No-op when quick access is enabled — there's nothing to lock back to.

---

**46. Added Vite as a real build step — `src/` is now the source, `www/` is build output.**

Root cause this fixes: the app has **never once been able to load, in any session**, because
browsers cannot resolve bare npm-package specifiers (`import { X } from '@capacitor/filesystem'`)
on their own — only a bundler or a hand-authored import map can, and ES module resolution happens
before any code runs, so a single unresolvable import anywhere in the dependency graph blocks the
entire script. Every file that imports a Capacitor plugin (`notifications.js`, `backup.js`,
`fileactions.js`, `gdrive.js`, and `app.js` itself) has had this problem since before any of these
sessions existed. The first device test's blank screen had two causes stacked on top of each other —
a `window.sqlitePlugin` global that nothing set (fixed the prior session) and this bare-import
problem underneath it, which the sqlitePlugin fix didn't touch since it introduced a normal ES
import for the same package, which has the identical failure mode. The second device test's still-
blank screen is what actually surfaced this.

Why this wasn't caught by any earlier syntax check: `node --check` uses Node's module resolution,
which *can* resolve bare specifiers from `node_modules` — completely different from a browser's
native ES module loader, which has no such algorithm without an import map. Checking with the wrong
tool gave false confidence for many sessions.

**The fix, verified by actually running it, not just asserted:** added Vite (`^7.3.2` — checked
against current npm data, not assumed from memory) as a devDependency, moved `src/` to be the real
source directory, configured Vite to build `src/` → `../www` so `capacitor.config.json`'s `webDir`
never has to change, and added a `npm run build` step to CI before `cap sync`. Ran `npm install` +
`npx vite build` for real in a sandbox before delivering this — 82 modules bundled successfully, and
the output was grepped to confirm zero unresolved `@capacitor`/`@zip`/`@codetrix` imports remain
anywhere in the built JS.

This also means every existing `import` statement across every file (db.js, crypto.js, intents.js,
notifications.js, ads.js, backup.js, gdrive.js, vault.js, fileactions.js, app.js) needed **no code
changes at all** — they were always correct JavaScript, just missing the one build step that makes
them resolvable in a real browser engine. The fix is entirely in the build pipeline, not the app code.

---

**47. `gdrive.js` never calls `GoogleAuth.signIn()` until `GOOGLE_DRIVE_CONFIGURED` is manually
flipped to `true`.** Root cause of the actual "Dumpzone keeps stopping" crash, confirmed by a real
device crash log (Session 21) — not the SQLite/bundling issues from Sessions 18–19, which were both
real and correctly fixed. The plugin's native `signIn()` method has no null-check on its internal
`GoogleSignInClient` before calling it; with no client ID configured, that's a guaranteed uncaught
`NullPointerException` **inside the plugin's own compiled Java code**, which kills the whole app
process before anything can reach JavaScript — confirmed no amount of JS-side try/catch could have
caught this, since the crash happens on the native side of the bridge, before a promise resolution
or rejection is even possible. The guard is a plain JS boolean, flipped manually alongside adding a
real client ID (`android-notes/native-setup.md` §11) — simple, explicit, and impossible to forget
since both steps are documented together in one place.

---

**48. Voice recording uses `cap-voice-rec` (v6.x), not the original `tchvu3/capacitor-voice-recorder`
or its `@independo` fork.** Fixes a real, confirmed device bug (Session 24): the voice capture
button was using the same generic file-picker as image/pdf/file, meaning "record a voice memo"
actually asked the person to upload an existing audio file rather than record one.

Chose `cap-voice-rec` specifically because its major version explicitly tracks Capacitor's own major
version (named "For Capacitor 6"), avoiding the exact version-mismatch class of problem the Google
Sign-In plugin caused earlier (Session 21) — the original `tchvu3` package is on major version 7,
which by its own stated convention ("major versions of the plugin are compatible with major versions
of Capacitor") likely targets Capacitor 7, not our Capacitor 6 pin. `@independo`'s actively-maintained
fork was also considered — better long-term maintenance, but requires bumping `minSdkVersion` from
Capacitor 6's default of 22 to 24, a native config change with no clear payoff given the API need
here is simple record/stop, not the fork's more advanced continue/finalize controls.

Verified before writing any code against it, not assumed: queried the npm registry directly for the
real current version (`6.0.1`, not a guessed number), installed it for real in a sandbox, and read
its actual shipped `definitions.d.ts` to confirm `stopRecording()`'s exact return shape
(`{ value: { recordDataBase64, msDuration, mimeType } }`) — its own README had an inconsistency
about this (one section mentioned a `path` field that doesn't exist in the actual type definitions).
Also fixed two related bugs found in the same code path: `pdf` and `image` capture had no `accept`
filter on their file inputs, so either would accept literally any file type.

Not done: the noise-reduction toggle from the original plan (`android-notes/native-setup.md` §5) —
this plugin has no audio-source parameter to expose it. Still an open item, tracked separately from
basic recording, which is what was actually broken and is now fixed.

---

**49. Manifest permissions are applied by a CI script (`scripts/patch-manifest.js`), never by hand-editing
a manifest file.** Root cause this fixes, found while resolving Session 24's flagged "unverified"
question about `cap-voice-rec` and `RECORD_AUDIO`: `android/` has never actually been committed to the
repo — `build-android.yml`'s "add platform if not present" check has been true on every run across all
24 prior sessions — so `android-notes/native-setup.md` §3's permission list, despite being documented
since Session 1, had never actually been injected into any AndroidManifest.xml that reached a real build.
A hand-edit would have been discarded the next CI run regardless of whether anyone remembered to make it.

Confirmed by direct inspection, not assumption: downloaded `cap-voice-rec@6.0.1` from npm and read its
shipped `android/src/main/AndroidManifest.xml`. It declares a `<service>` with
`android:foregroundServiceType="microphone"` and zero `<uses-permission>` entries — so it does not
self-declare `RECORD_AUDIO`, resolving Session 24's open question, and its foreground service has no
permission of its own either, which is a second, previously unflagged gap. Cross-checked Capacitor's own
5→6 upgrade documentation to confirm the project's default `targetSdkVersion` is **34**: Android 14 (API
34) requires both `FOREGROUND_SERVICE` and a type-specific permission (`FOREGROUND_SERVICE_MICROPHONE`
for a `microphone`-typed service) declared in the manifest, or the OS throws a `SecurityException`/
`MissingForegroundServiceTypeException` at `startForeground()`, inside native code — unreachable by any
JS-side try/catch, the same failure class as Decision 47. Untested until now only because Session 24
built the recording UI but hadn't yet run it on a device.

Fix: `scripts/patch-manifest.js`, run by CI (`build-android.yml`) immediately after `npx cap add android`
and before `cap sync`/icon generation. Idempotent — checks for each permission before inserting, so it's
harmless to run against an already-patched manifest and safe to keep even if `android/` is committed
later. All eight permissions from `native-setup.md` §3 (the original six plus the two new
foreground-service ones) now live in one place, `REQUIRED_PERMISSIONS` in that script, rather than a doc
list with no mechanism to apply it. Verified by running the script against a representative sample
manifest in a sandbox before delivering: inserted all eight on first run, correctly no-op'd on a second
run against its own output.

Also noted, not acted on: `package.json` lists `@capawesome-team/capacitor-android-foreground-service` as
a dependency, but nothing in `src/js/` imports it — dead weight, likely a leftover from before
`cap-voice-rec` was chosen. Flagged for later cleanup, not removed this session since it isn't the cause
of anything currently broken.

---

**50. Biometric unlock is a convenience alternative to typing the app-open password or Vault PIN — never
a fourth credential, never both locks via one toggle.** Off by default; each lock's toggle requires
confirming the real password/PIN (hash-compared against what's stored) before anything reaches the
biometric layer, and changing or removing that password/PIN immediately clears the biometric secret for
it, forcing re-confirmation — a stale cached value would otherwise unlock with, or derive a vault key
from, a credential that no longer matches.

Plugin: `@capgo/capacitor-native-biometric@6.0.4`, chosen over the original (unscoped)
`capacitor-native-biometric` after checking both directly: the original's peer dependency is
`@capacitor/core@^3.4.3` and its `build.gradle` points at `jcenter()`, dead since 2021 — using it risked
breaking the CI build outright. The Capgo fork's `6.0.4` pins `@capacitor/core@^6.0.0` and uses
`google()`/`mavenCentral()` with AGP 8.2.1, compileSdk/targetSdk 34 — matches this project. No manifest
permission needed in `scripts/patch-manifest.js`: `androidx.biometric:biometric:1.1.0` (the plugin's own
dependency) self-declares `USE_BIOMETRIC` in its own manifest, standard for that library.

Security model, read from the plugin's native source rather than assumed from its README: its Keystore
key is configured with `setUnlockedDeviceRequired(true)` but not `setUserAuthenticationRequired(true)` —
so decrypting the stored secret only requires the device to be unlocked, not a fresh biometric check on
every read. The actual biometric gate is enforced by this app's own call sequence in `src/js/biometric.js`
(always `verifyIdentity()`, only `getCredentials()` after it resolves), not by the hardware on every
access. Documented plainly in `android-notes/native-setup.md` §10 and `ARCHITECTURE.md` §3 rather than
oversold — this is the same model most consumer apps' "unlock with fingerprint" uses, and manual
password/PIN entry remains the actual security boundary underneath it.

Implementation: `src/js/biometric.js` (new, thin wrapper around the plugin); two new `credentials`
columns (`biometric_app_enabled`, `biometric_vault_enabled`); `src/js/db.js` gained its first real schema
migration (`ensureColumn()`, since `CREATE TABLE IF NOT EXISTS` never retrofits a column onto an existing
on-device install — the same class of gap Decision 49 found for manifest permissions, different layer).
`unlockVault()`/`attemptUnlock()` are reused as-is for the biometric path (the retrieved secret is just
handed to the same verify function a typed entry would use) rather than duplicating "what counts as
correct" a second time.

Also removed this session: `@capawesome-team/capacitor-android-foreground-service` from `package.json`,
flagged as dead weight in Decision 49 — nothing in `src/js/` ever imported it.

---

**51. Every build now gets a real, always-different version, automatically — `versionCode` from the CI
run number, `versionName` from `package.json` plus that same number.** Same root cause as Decision 49,
one layer over: `android/app/build.gradle` comes from the Capacitor CLI's template every time `android/`
is regenerated (every run), and that template unconditionally ships `versionCode 1` / `versionName
"1.0"` — confirmed by extracting `@capacitor/cli`'s actual `android-template.tar.gz` and reading it
directly, not assumed from familiarity with Capacitor. Every build has shipped identically versioned
since Session 1, with no way to tell installed builds apart.

Fix: `scripts/patch-version.js` (new), run by CI immediately after `patch-manifest.js`.
`versionCode = github.run_number` — a strictly increasing integer GitHub Actions already provides, so
there's no manual counter to remember to bump and no risk of forgetting (the actual problem reported:
"still builds with 1.0, need to bump version for every build" is solved by removing the manual step
entirely, not by adding a reminder to do one). `versionName = "<package.json version>+<run number>"`,
so a build installed on-device can be identified from Settings > Apps without checking CI logs, and
`package.json`'s version field still means something (bump it by hand for a real release; the run-number
suffix changes regardless).

Verified by running the script against the actual extracted Capacitor template in a sandbox: correctly
rewrote both fields, correctly overwrote them again on a second run with a different run number
(deliberately not idempotent-skip like `patch-manifest.js` — there's nothing valid to skip on a
template that's always "1.0"), and correctly refused to run (exit 1, clear error) when
`GITHUB_RUN_NUMBER` isn't set, rather than silently guessing a version.

Unrelated, noted for the record: the app icon was replaced directly on GitHub by hand this session
(`resources/icon.png`) — exactly the documented single-file swap in `native-setup.md` §2, no code or
doc change needed on this end.

---

**52. Default theme is colorful, not just the opt-in Gradient mode — one color per capture type
(Text/Voice/Image/PDF/Files), used consistently everywhere that type appears.** Requested directly:
the previous theme (flat grey/white, single blue accent) read as dated, confirmed by the attached
screenshots. Gradient mode (the existing full-screen blurred-glow, user-picked-color effect) stays
exactly as it was — untouched mechanism, still opt-in, still separate — this decision is about what
the app looks like *without* it turned on.

Palette: one brand accent (indigo/violet, `#6C5CE7`) for primary actions and the wordmark, plus five
category colors — amber (note), teal (voice), pink (image), blue (pdf), green (file) — applied to the
capture-bar buttons, every list row's left edge (`.drive-backup-row[data-type=...]`), and the Vault's
filter tabs and capture bar. The color carries real information (which type something is, scannable
down a long list) rather than sitting on top as decoration — the same five hues appear in the same
places whether you're in the main timeline or the Vault, so the mapping only has to be learned once.

`src/js/app.js` gained `data-type="..."` on six render call sites that previously only put the type in
the row's text (`renderMainTimeline`, its search-input counterpart, `renderTrash`, `renderVaultList`,
its search-input counterpart, `renderVaultTrash`) — additive only, nothing removed, verified the type
vocabulary already matched the capture buttons' `data-type`/`data-vault-type` values exactly
(`note`/`voice`/`image`/`pdf`/`file`) before relying on it. Two render sites (`renderDriveBackupList`,
`renderStorageBreakdown`) weren't touched — their rows represent whole backups or storage categories,
not a single capture type, so there's no matching color to apply.

Checked contrast before finalizing, not after a complaint: white text on the lighter category colors
(amber/teal/green) failed WCAG AA on the Vault's capture-bar button labels — fixed by giving those
buttons dark, per-color text (same values already used for the Vault tabs' active state, which were
fine). The primary-button gradient had the same problem at its lighter end in both themes — fixed by
splitting button-fill color (`--btn-1`/`--btn-2`, kept dark enough for white text in both themes) from
brand-as-text color (`--brand`, used for the wordmark, secondary-button text/border, and the focus
ring — needs to read well as text on the page background instead, a different constraint). Verified
every resulting pairing with the actual WCAG contrast formula in a sandbox, not by eye.

System font stack kept as-is, deliberately: this is an offline app with no other network calls beyond
the ads SDK, and a web font would be the first thing in the entire codebase that needs fetching
anything to render text. Hierarchy comes from weight/scale instead of a second typeface.

---

**53. Gradient mode removed entirely; layout rebuilt around a bottom nav (Home / Vault / Settings)
instead of one long scrolling page; every single-setting checkbox restyled as a switch.** Requested
directly, alongside keeping dark mode.

Gradient mode's full removal: `--gradient-1`/`--gradient-2` CSS custom properties, the
`data-gradient` attribute and its `:root[data-gradient="on"]` rules, `setGradientMode()`,
`gradientMode`/`gradientColor1`/`gradientColor2` from `getAppearance()`/`applyAppearance()`, and the
toggle + two color-picker inputs from Settings. Old `gradient_mode`/`gradient_color_1`/
`gradient_color_2` rows already written to the generic `meta` table (including inside any backup
file made before this session) are simply never read again — harmless to leave, nothing left that
reads them, no migration needed since appearance was always stored key-by-key rather than as
dedicated schema columns.

Layout: `#main-screen` split into two panels, `#home-tab` (capture bar, search, timeline — what used
to be the whole screen) and `#settings-tab` (every `<details>` section that used to sit stacked below
the timeline on the same page). A new fixed `#bottom-nav` (Home / Vault / Settings) switches between
them, plus `#vault-screen`, which was already its own screen and needed no restructuring — tapping
Vault calls the exact same `showScreen('vault-screen')` every existing unlock-success path already
called, so there's no new code path for the Vault's own lock gate to go through. `showScreen()` now
also drives the nav's visibility (hidden for first-run/lock-screen/ad-gate, which have nothing to
navigate between yet) and active-tab state, in one place, rather than every call site managing that
itself.

Every standalone on/off setting (dark mode, both biometric toggles, Drive auto-backup, the
safety-backup confirmation) is now a sliding switch, done as a pure CSS restyle of the existing
`<input type="checkbox">` (`appearance: none` + a `::before` thumb) — no change to the underlying
element, id, or `change` listener, so none of the JS that reads `.checked` needed touching. Backup/
restore's category checkboxes deliberately stay compact checkboxes, not switches — picking several
items from a list is a different kind of choice than flipping one setting on or off, and a row of
five switches for that would read wrong. Restore-mode's radio buttons got the same modern-dot
treatment.

`<details>`/`<summary>` in the new Settings tab restyled as cards with a custom rotating chevron
(`::-webkit-details-marker` hidden, `::after` chevron rotated via `[open]`) instead of the browser's
default disclosure triangle — the single biggest contributor to the "1990s" complaint, by nature of
being what a plain unstyled `<details>` list always looks like regardless of anything else on the
page.

Checked WCAG contrast for every new color pairing before finalizing, same discipline as Decision 52:
found and fixed three real failures — the bottom nav's active-tab label (used `--brand` directly at
first, landed at 4.06:1/4.34:1 in light/dark, just under the 4.5:1 small-text threshold; added a
dedicated `--nav-active` token per theme instead, both now ≥5.4:1) and the inactive tab label (fg at
55% opacity blended to 3.87:1 in light mode; raised to 70% opacity, ≥4.5:1 in both themes). Also
swapped two newer CSS features (`color-mix()`, `:has()`) for explicit per-theme tokens and an added
HTML class respectively — both are fine on current Chrome but this app's minSdk 22 (Decision — see
Session 20) means some real devices may carry an older system WebView than whatever's on the
development machine that would have "just worked" during a quick look; explicit fallback-free CSS
costs nothing here and removes the question entirely rather than assuming.

---

**54. Fixed a real security bug: the Vault's PIN gate was never actually connected to `vault-screen`,
so the bottom-nav Vault button opened straight into vault content with no gate at all. Also: both
locks now default to attempting biometric immediately rather than waiting for a button tap.**

Root cause, found by reading the actual current HTML rather than assuming Session 29's refactor had
kept things wired correctly: Session 29 moved `vault-pin-setup-view`/`vault-locked-view` (and the
Session 26 biometric toggle) into Settings' `#vault-settings` accordion, while `#vault-content`
(capture bar, search, tabs, list, trash) stayed in `#vault-screen` with nothing above it to gate on.
The bottom nav's Vault button always called `showScreen('vault-screen')` directly — a screen that,
after that refactor, had never had a lock gate of its own to begin with, only content. A second,
compounding bug in the same area: `lockVault()` cleared the in-memory session key but never cleared
`#vault-list`/`#vault-trash-list`'s already-rendered HTML, so even reaching the gate correctly
wouldn't have hidden content a previous unlock had already rendered into the page.

Fixed by moving the gate elements back into `vault-screen` itself (before `#vault-content`, where
they were originally before Session 29), and rewriting `refreshVaultGateView()` to be the one
function that decides all three states — needs-setup / locked / unlocked — based on whether a PIN
exists and whether `privateSessionKey` is currently set, toggling `#vault-content`'s visibility as
part of that instead of leaving it permanently visible. Every unlock success path (PIN, biometric)
and the Lock button now route through this function rather than assuming the caller already knows
which view should be showing. `lockVault()` now also clears both list containers' `innerHTML`
directly — defense in depth on top of the container being hidden, not a substitute for it.

Biometric defaults: both the app-open lock screen and the Vault gate now call their respective
`tryBiometricUnlock*()` automatically as soon as they're shown, when enabled — fire-and-forget,
reusing the exact same success/failure paths a manual button tap already used, so cancelling or
failing just leaves the password/PIN field available same as before. The Vault's version is guarded
by a module-level flag (`vaultBioAutoPrompted`, reset in `lockVault()`) so it fires once per fresh
"locked" state rather than re-prompting on every re-render while still locked — the app lock screen
needs no equivalent guard since `showLockScreen()` itself only ever runs when freshly (re)locking.

---

**55. Added Money as a sixth capture type (card only — its actual capture flow is intentionally not
built yet, by request); redesigned both capture bars as a 6-card grid; renamed backup/append
filenames to `<prefix>_<letters>_<timestamp>.dz`.**

Money: added everywhere the existing five types are threaded through, deliberately as its own new
type rather than reusing the existing auto-detected `expense` type (which stays exactly as it was,
untouched, still populated only via `intents.js`'s text parsing, not manual capture) — the two are
different mechanisms and conflating them without being asked to felt like the wrong call; easy to
unify later if that turns out to be wanted instead. Touched: `VAULT_TYPES` (vault.js), `buildBackup-
Plaintext`/`parseVaultPlaintext` (given a placeholder text-only shape, same as 'note', since no real
shape has been specified yet), `CATEGORY_LABELS`/`VAULT_TYPE_LABELS` (app.js), `ENTRY_CATEGORIES` in
backup.js (a `money` backup category, letter `m`), and a new `--type-money` color (red — the only
one of the six hues not already in use, and a deliberate, not incidental, choice for a money
category). Tapping the Money card shows a plain "coming soon" message rather than falling into the
generic file-picker branch the other non-special-cased types use, which would have been actively
wrong for a type with no defined shape yet.

Capture UI: both `#capture-bar` and `#vault-capture-bar` are now a 3-column grid of cards (icon
above a text label, larger tap target) instead of a thin row of icon-only buttons — Home's capture
bar gains labels it never had before ("Text", "Voice", etc.), matching what the Vault's bar already
showed. Checked contrast again before finalizing: Home's cards now carry real label text on the
category colors for the first time, so they needed the same dark-text treatment already computed for
the Vault's equivalents (Decision 52) — applied to both via one shared selector rather than
duplicating the color values.

Filenames: `backup_<letters>_<yyyymmdd_hhmmss>.dz` for a full Backup & Restore export,
`append_<letters>_<yyyymmdd_hhmmss>.dz` for a single entry's "Download for append" export — same
shape, different leading word, exactly as specified. `<letters>` is a fixed-order subset of `tvipmf`
(Text/Voice/Image/PDF/Money/Files, only the ones actually included, in that order regardless of
selection order); categories with no letter of their own (Private Vault, Expenses, Reminders, Tags,
App Settings) simply don't contribute one, and `x` is used if none of the six lettered categories
are present at all, so the filename is never left with an empty segment — neither case was specified,
both are reasoned-through defaults, flagged rather than silently assumed. Read the timestamp format
in the request as a likely typo (`yyyymmddh_hhmmss` — an extra "h" before the underscore) and built
the standard `yyyymmdd_hhmmss` instead; flagged rather than guessed silently. Extension changed from
`.dzbackup`/`.zip` to `.dz` for both — the two file types remain internally different (one's a plain
encrypted JSON blob, the other's an actual zip archive), which stays fine since nothing in the code
ever branched on file extension to begin with, only on which `<input>` the file came through; file
pickers still accept the old extensions so previously-made backups keep working. One accepted
tradeoff: the per-entry append filename lost its old label-based naming (which made two exports
distinguishable at a glance even with identical timestamps) in favor of the uniform format — two
exports of the same category within the same second would now collide on name. Not solved, since the
spec was explicit about the format and this felt like a real but rare edge case worth flagging rather
than deviating to handle unasked.

Found and cleaned up in passing: `APP_SETTINGS_KEYS` in backup.js still listed the three
`gradient_mode`/`gradient_color_1`/`gradient_color_2` meta keys removed in Decision 53 — a stale
leftover from that removal, not something new. Old backups containing those keys still restore fine
(harmless, unread rows); just stopped being an active part of what a new backup writes.

---

**56. Fixed five real bugs reported directly against a device, all in the areas Sessions 29/30
touched — the app lock screen's auto-biometric, the bottom nav, and the Vault gate.**

*Auto-biometric required two attempts on cold start.* Reasoned root cause (not confirmed by
instrumented logging, since that's not available here): triggering the OS BiometricPrompt
immediately on cold start, before the Android Activity has actually settled/gained window focus, is
a known class of timing issue — the prompt can visually appear and accept the fingerprint without
the result reliably reaching the app, so a second, later attempt (once focus has settled) works.
Mitigated with a 400ms delay before the automatic attempt in `showLockScreen()` — a reasoned
estimate, not a measured value; worth tightening, or replacing with an actual focus/resume signal,
if a real device check shows it's still flaky. The Vault gate's equivalent auto-attempt wasn't given
the same delay — it's reached by navigating there, not by a cold start, so Activity focus should
already be established by the time it fires.

*Bottom-nav Home/Settings did nothing while on the Vault screen.* Root cause: the nav handler called
`switchMainTab(tab)` directly, which only toggles `#home-tab`/`#settings-tab` inside `#main-screen` —
it never actually showed `#main-screen` itself. If `#vault-screen` was the currently visible screen,
tapping Home or Settings just silently changed which sub-tab main-screen would show *the next time
it became visible*, with no visible effect. Fixed by giving `showScreen()` an optional `tab`
parameter (defaulting to Home, preserving every existing `showScreen('main-screen')` call site's
behavior unchanged) and having the nav handler call `showScreen('main-screen', { tab })` instead of
touching the sub-tab directly.

*The Lock button stayed visible on the Vault's locked/setup gate, where there's nothing to lock.*
Fixed: `refreshVaultGateView()` now hides `#vault-lock-btn` unless genuinely unlocked.

*The biometric enable/disable toggle showed on the locked landing gate, not just once inside.*
Requested directly: the toggle (and its confirm-PIN sub-form) is a Vault *setting*, not part of
getting into the Vault — the "Unlock with fingerprint/face" *button* correctly stays on the locked
gate (that's the actual unlock mechanism), but the toggle now only renders once truly unlocked.
Moved the markup from beside `vault-locked-view` into `#vault-content`, and changed
`refreshVaultGateView()`'s visibility condition on the row from "a PIN exists" to "currently
unlocked."

*Locking the Vault didn't return to Home immediately.* The handler was awaiting an async gate
refresh before navigating away. Reordered: `showScreen('main-screen')` now runs synchronously,
right after `lockVault()`, with no await in between — the gate refresh that used to happen here
isn't needed anyway, since the bottom nav's Vault handler already calls `refreshVaultGateView()`
fresh on every visit.

---

**57. Added Reminder and Location as two more capture cards (placeholder, same as Money in Decision
55), positioned before Money — Text/Voice/Image/PDF/Reminder/Location/Money/Files, eight cards now.
Backup filename letters extended from `tvipmf` to `tviprlmf` to match.**

Reminder reuses the *existing* `type: 'reminder'` / `reminders` category rather than introducing a
parallel type, unlike Money's relationship to Expense — the two situations aren't actually
analogous. `intents.js` already auto-detects reminders from typed text ("remind me..."), and a
reminder is a reminder regardless of how it was created; there's no real ambiguity the way "money"
could mean several different things, which is what motivated keeping Money and Expense apart. One
real consequence of this reuse: Reminder is now vault-capable (it's one of the eight manual capture
cards, all of which support Private Vault), so `reminders` in `ENTRY_CATEGORIES` gained the same
`&& !e.is_private` guard every other vault-capable category already has — a no-op for any reminder
that exists today (all auto-detected, never private), only relevant once a private one becomes
possible. Location is genuinely new — no prior type, schema, or auto-detection to reuse — added
exactly like Money: its own category (`locations`), its own color, placeholder "coming soon" on tap,
no defined content shape yet.

Colors: red was already taken by Money, so Reminder is orange (`#F97316`) and Location is cyan
(`#06B6D4`) — the two remaining hues distinct from all six already in use. Checked contrast the same
way as every color added since Decision 52: both need dark text over their background (white fails
AA at this saturation/lightness, same as five of the other six type colors already did).

Filenames: `tvipmf` → `tviprlmf`, in card order (Text/Voice/Image/PDF/Reminder/Location/Money/Files).
`CATEGORY_LETTERS`/`LETTER_ORDER` in backup.js updated together, plus the design-plan comment at the
top of `style.css` and the filename convention section in `ARCHITECTURE.md` §4 — all three needed to
stay in sync or the documentation would have drifted from the code the moment this shipped. This
extension wasn't explicitly requested in so many words — the request added two cards to the same
visual set the filename letters were built from, and extending the acronym to match felt like the
faithful reading of "these letters represent the cards," but it's a judgment call, flagged rather
than silently assumed, same as the other filename defaults in Decision 55.

Capture-card grid: 3 columns → 4, for a clean 4x2 layout instead of 3+3+2.

---

**58. Ads deferred to a future build — explicit product decision, not a technical default.** No
AdMob account, no plugin, no gate shown, not even a notification-based ad format — nothing ad-related
ships in this build. Phase 12 in ROADMAP.md stays unstarted and now says so explicitly rather than
just sitting unchecked; `ads.js`'s gate-decision logic is untouched and stays in the repo as
scaffolding for whenever this is actually picked up, since deleting working, self-contained logic
that isn't the cause of anything broken isn't warranted here — only its one caller was removed.

That caller turned out to matter more than expected. Investigating a reported freeze ("once entering
the app completely froze, nothing works") plus a worse version of Session 31's biometric-timing bug
(now needing a full second unlock, not just a second tap) surfaced two compounding issues in the same
area, both fixed together:

1. `onUnlocked()` called `runDailyAdGateIfDue(window.adSdk, metaStore)` every single unlock.
   `window.adSdk` has never been set anywhere — confirmed by checking `package.json` and
   `capacitor.config.json` directly, neither has ever referenced an AdMob plugin, matching what
   ROADMAP.md's Phase 12 already said. `ads.js`'s own `try/catch` handled the resulting
   `TypeError` safely on its own — this was extra, pointless async work on the unlock path, not a
   confirmed hang by itself, but real complexity with nothing behind it, removed along with the ads
   decision above rather than left in place calling into nothing.

2. The actual likely cause of the freeze/double-unlock: Session 31's fix for the biometric
   cold-start timing bug used a bare `setTimeout(..., 400)` called from inside `showLockScreen()`,
   independent of the rest of `DOMContentLoaded`'s own setup. That setup does its own sequence of
   awaited database calls (populating Settings' toggles, biometric availability, etc.), and the
   timeout's 400ms could land in the middle of it on a slower device — two separate chains of
   SQLite calls running concurrently against the single connection, which is a plausible, common
   cause of exactly this symptom (an unlock that appears to succeed but the app never actually
   finishes initializing). Reasoned from the code and the timing involved, not confirmed via
   device-side logging, which isn't available here.

   Fixed by moving the actual trigger to the last line of the `DOMContentLoaded` handler, after
   every other setup step has fully run — there's nothing left for it to race against at that
   point. `showLockScreen()` still computes `window.biometricUnlockAvailable`, it just no longer
   schedules anything itself. Kept a smaller delay (300ms) on top for the original window-focus
   concern Session 31 was trying to address, now layered on top of "wait for our own setup to
   finish" rather than substituting for it.

---

**59. Fixed the actual cause of two more reported bugs: two biometric prompts firing on every app
open, and the Vault sometimes ending up already unlocked with no PIN/biometric prompt at all.**

Root cause: `DOMContentLoaded`'s initial setup called `refreshVaultGateView()` once, unconditionally,
right after wiring the Vault's buttons — before the person had done anything, let alone reached or
tapped the Vault tab. `refreshVaultGateView()` auto-triggers a biometric attempt for the Vault when
it's enabled (Decision 56), so this fired a **second** biometric prompt on cold start, independent of
and racing against the app lock screen's own auto-attempt (Decision 56/58) — matching exactly what
was reported: two prompts, and which one "wins" (or whether both are needed) depending on timing.
Worse: if that Vault-side attempt succeeded, `privateSessionKey` got set and the Vault's content
silently became available — while the person was still looking at the *app's* lock screen, having
authenticated for the app, not the Vault. Reaching the Vault tab afterward would then show it already
unlocked, no prompt at all, because it already was.

This line had been redundant since the moment the bottom-nav Vault button was written (Session 30) —
that handler already calls `refreshVaultGateView()` fresh on every actual visit, and vault-screen
starts hidden regardless, so nothing needed the state pre-computed at cold start. It went unnoticed
because on its own it was harmless (just redundant); it only became actively wrong once Decision 56
added an auto-biometric side effect to the function it was redundantly calling. Removed entirely,
with the reasoning left in a comment at the call site it used to occupy, since "this looks like it
should be here" is exactly the kind of assumption that put it there in the first place.

---

**60. Fixed the Select files screen's scattered layout — a checkbox sizing bug, not a one-off
styling gap.** Reported directly against a screenshot: category and item checkboxes floated
disconnected from their labels, labels pushed to odd positions. Root cause: `.cat-select-all` and
`.item-select` (the Select files screen's checkboxes) had never been given a rule of their own —
every checkbox styled so far (Decisions 52/53) was scoped to a specific class
(`.switch-row`/`.category-checkboxes`), so anything outside those fell through to the general
`input, button, select { width: 100% }` rule, stretching the checkbox and scattering whatever sat
next to it in its flex row.

Fixed at the root rather than adding a third one-off class-scoped rule (the same gap that caused
this): the compact square-checkmark checkbox is now the *default* for any plain `input[type=
checkbox]`, and `.switch-row` overrides it to the sliding-switch look for the handful of single
on/off settings that want that instead. A checkbox with no specific class now still gets sane
sizing rather than silently inheriting `width: 100%` — closes this category of bug for any checkbox
added later, not just the one reported. Also caught while consolidating: the switch needed an
explicit `content: none` on its `:checked::after` to suppress the new default checkmark glyph,
which would otherwise have shown up inside the sliding thumb.

Also fixed while in this file, unrelated to the report but found during review: Decision 53's own
entry had ended up out of chronological order, sitting after Decision 59 instead of after Decision
52 — an artifact of an earlier edit anchoring to non-unique matching text. Moved back into place;
no content was lost or changed, only its position in the file.

---

**61. Bottom nav reduced from three tabs to two — Home and Vault only. Settings (both the main app's
and the Vault's) moved from being a nav destination to being a card/tile reached from within the
screen it belongs to. A small red circular power button, present on every screen, force-closes the
app.**

Requested directly. Three changes, all UI-only — no schema, no credential, no backup-format change:

1. **Home:** `#settings-tab` itself is unchanged (same accordion of Your Data/Storage breakdown/
   Trash/App lock/Appearance/Backup & Restore/Google Drive/Append-from-download) — only its entry
   point moved, from the bottom nav's third button to an "⚙️ App Settings" tile inside Home, below
   the capture cards. `switchMainTab()` (already the function that shows/hides `#home-tab`/
   `#settings-tab`) is unchanged in what it does, just no longer driven by a nav button for the
   `'settings'` case — the tile and a new "← Back to Home" link at the top of `#settings-tab` call
   it directly. Settings is treated as part of Home for nav-highlighting purposes: viewing it keeps
   the Home icon active in the (now two-button) nav, rather than nothing being highlighted.
2. **Vault:** the three previously scattered settings surfaces — the biometric toggle row, the
   auto-lock timeout select (previously its own `<details id="vault-lock-settings">`, now flattened
   to a plain subsection since nothing needed its own collapse/expand), and Vault Trash — are now
   nested inside one `<details id="vault-settings-card">`, matching the "one settings card/tile"
   request the same way Home's tile does. Vault Trash stays its own nested `<details>` (unlike the
   auto-lock select) because `renderVaultTrash()` is lazily triggered by its own `toggle` event —
   collapsing it into a flat subsection would mean it renders every time Vault Settings opens,
   whether or not Trash is what's being checked. No element id changed, so no JS wiring needed
   touching beyond the two new entry-point listeners above — everything else was a pure markup
   relocation. Extended the existing `#settings-tab details` chevron-card CSS treatment (Decision
   53) to `#vault-settings-card` and its nested `#vault-trash-settings`, so Vault's settings read as
   the same design language Home's already do, rather than the plain unstyled `<details>` they were
   using before (a pre-existing, never-previously-flagged gap — only `#settings-tab` had ever gotten
   that treatment).
3. **Power button:** a single `#power-close-btn` element, fixed top-right, living directly under
   `<body>` rather than inside any `.screen` div — so it's present regardless of which screen
   `showScreen()` currently shows, without duplicating it into first-run/lock/main/vault/ad-gate
   individually. Confirms (`confirm('Close Dumpzone?')`) before acting, since this is a hard process
   kill, not the existing lock-on-background behavior (Decision 45) — that's unaffected and still
   covers what the person sees on next open regardless of whether they used this button or the OS's
   own back/recents gesture. Calls `@capacitor/app`'s `App.exitApp()` (already a project dependency,
   already used for `appStateChange` — Decision 45), guarded by `@capacitor/core`'s
   `Capacitor.isNativePlatform()` so a browser preview shows a plain message instead of a silent
   no-op or a thrown error — `exitApp()` has no meaning outside a native Android/iOS shell. Both
   imported dynamically, matching how the rest of the codebase already brings in plugins only where
   used rather than at module load.

Not done, flagged rather than assumed: whether the power button should also appear inside modal
overlays (Select files, the overwrite-confirm dialog, the auto-backup-due banner) wasn't specified —
left visible underneath them (z-index below `.modal-overlay`'s 10, above the bottom nav's 5) rather
than layered on top, so an open modal still has full visual priority and the button can't be tapped
through it by accident.

---

**62. Recents-preview blanking to match Opera Incognito. Power icon restyled to a
transparent-background red-outline glyph. Confirmation dialog removed from the power button. Power
button removed from the app-open lock screen.**

Requested directly, all four in one session:

1. **Recents-preview blanking:** `MainActivity` needs `FLAG_SECURE` set on its window — no
   JS/Capacitor-layer equivalent exists; this is native-only. Since `android/` is never committed
   (Decision 49), a hand-edited `MainActivity.java` would be silently discarded the next CI run —
   same root problem `patch-manifest.js` solves for the manifest. `scripts/patch-mainactivity.js`
   follows the identical pattern: finds the stock, override-free `MainActivity.java` Capacitor's
   template generates, inserts an `onCreate()` that sets the flag, aborts loudly (rather than
   silently no-op'ing) if the stock file's shape doesn't match what it expects to edit, and skips
   cleanly if already patched. Wired into `build-android.yml` right after the manifest patch step.
   Tested directly in this session's sandbox against a reconstructed stock `MainActivity.java` —
   confirmed correct output on first run and a clean idempotent no-op on a second run — since there's
   no real `android/` folder to test against outside actual CI. **One flag, two effects, not
   separable:** the recents/task-switcher preview goes blank instead of showing a live screenshot
   (the actual ask), and screenshots/screen recording of the app are blocked system-wide as an
   inherent side effect of the same flag. Documented as a feature of the same piece, not a bug, so
   it isn't mistaken for one later — a legitimate build/test risk regardless, since this specific
   native behavior has never run on a device (same standing gap as everything since Session 25).
2. **Power icon restyle:** replaced the solid red circle + white "⏻" glyph (Decision 61) with an
   inline SVG power glyph (line + open-top arc, the standard "power" icon shape) — transparent
   background, `stroke="currentColor"`, button `color: var(--danger)`. Reads correctly in both
   light and dark mode without a separate dark-mode override, since `--danger` (unlike `--bg`/`--fg`/
   `--card-bg`) is the same red in both themes already — nothing new needed there.
3. **Confirmation removed:** the `confirm('Close Dumpzone?')` gate is gone — tapping the button now
   calls `exitApp()` immediately (still guarded by the same `Capacitor.isNativePlatform()` check as
   before, for the browser-preview case). Removed on request; flagged as the accepted tradeoff — a
   mistaken tap now closes the app with no chance to cancel, same as a mistaken tap of the device's
   own back/recents gesture would.
4. **Hidden on the lock screen:** `showScreen()` now toggles `#power-close-btn`'s `.hidden` class
   based on which screen id is being shown — hidden only for `'lock-screen'` (the app-open password
   gate), present everywhere else including first-run and the Vault's own PIN gate. "Unlock screen"
   in the request read as this one specifically, since it's the one screen whose actual purpose and
   button label is "unlock" the app itself — the Vault's separate PIN gate wasn't named and keeps
   the button. Flagged as an assumption, not confirmed; easy to extend to the Vault gate too if
   that's what was meant.

---

**63. Recents-preview blanking (Decision 62) disabled for now.**

`FLAG_SECURE`'s screenshot-blocking side effect got in the way of taking screenshots during active
development — reported directly. The CI step calling `scripts/patch-mainactivity.js` in
`build-android.yml` is commented out, not deleted; the script itself is untouched and still correct
(confirmed working in Decision 62's own sandbox test). Re-enabling later is a one-line uncomment,
not a rebuild of the feature. Deleting the script file instead, as briefly considered, would have
left the workflow calling a file that no longer exists — a build failure, not a clean disable.

---

**64. Privacy Screen made a real, in-app, user-toggleable setting — replacing Decisions 62/63's
native MainActivity patch entirely.**

Requested directly: rather than a build-time flag that could only be turned off by editing CI
(Decision 63's disable), make it a Settings switch the person can flip themselves, any time.

Decision 62's approach (`scripts/patch-mainactivity.js` hand-inserting `FLAG_SECURE` into
`MainActivity.java`'s `onCreate()`) was fundamentally build-time — the flag got set once, at
process start, with no way for JS to change it afterward short of a full rebuild. That's why
Decision 63 could only ever *disable it entirely* for development, not offer a real toggle.

Replaced with `@capacitor-community/privacy-screen`, pinned to **`5.2.0`** specifically — its
`peerDependencies` is `@capacitor/core ^6.0.0`, the only version line of this plugin that matches
this project's Capacitor 6 (the plugin's own newer major versions, 6.x and 8.x, require Capacitor
7/8; unrelated numbering coincidence that its own version happens to also say "6"). Verified before
adopting by downloading the actual `5.2.0` tarball and reading its shipped Android source directly,
not just its README (same discipline as Decisions 49/50) — `PrivacyScreen.java`'s `enable()`/
`disable()` do exactly `window.addFlags`/`clearFlags(WindowManager.LayoutParams.FLAG_SECURE)` on
the current Activity, nothing more, callable from JS at any time. `capacitor.config.json`'s
`PrivacyScreen.enable` is set to `false` so native startup itself never turns it on — a new
`src/js/privacy-screen.js` wrapper (mirroring `biometric.js`'s shape) applies whatever's actually
stored, at the same point in `bootstrap()` that `applyAppearance()` already runs (before the lock
screen even renders — so the setting, when on, also covers the lock screen, not just the unlocked
app). Persisted via `metaGet`/`metaSet` under `privacy_screen_enabled`, same mechanism as
`dark_mode`/`auto_backup_enabled` — off by default. New `#privacy-screen-settings` card in Settings,
same populate-then-wire pattern as the dark-mode toggle right below it.

One known gap, stated rather than glossed over: on a cold launch there are one or two frames
between the WebView first painting and this module's `enable()` call resolving, during which the
setting (if on) isn't in effect yet. Fixing that would need native code reading a persisted
native-side flag before `onCreate()` finishes — out of scope for what's otherwise a plain web-layer
toggle, and a narrower gap than Decision 62's version had at cold start too (that one had the same
kind of native/JS timing gap, just for a different reason — Capacitor's own bridge init time).

`scripts/patch-mainactivity.js` is superseded, not merely paused this time — delete it from the
repo (see this session's file list). `build-android.yml`'s already-commented-out step calling it is
removed outright rather than left commented, since there's nothing left to re-enable that way.
`android-notes/native-setup.md` §13 is removed for the same reason — it was the last section in
the file, so nothing needs renumbering.

---

**65. Universal lock/unlock button (fixed top-right, left of the power button); vault's own
"Lock" button removed as redundant; universal Back button added as the middle slot of the bottom
nav; every dedicated "back to home" element removed in favor of it.**

Requested directly, three related changes in one session:

1. **Lock/unlock toggle:** new `#lock-toggle-btn`, same fixed-position pattern as the power
   button (Decision 61) — outside every `.screen` element, same z-index layering — positioned
   immediately to its left. Neutral foreground color (`var(--fg)`, not red or brand), since it's
   neither destructive (the power button) nor a primary action, just a status/utility icon.
   Context-aware at click time rather than tracking a separate "which context" flag: checks whether
   `#vault-content` is currently visible to decide whether to lock the Vault or the main app, so it
   can never drift out of sync with what's actually on screen. Locking the Vault is the exact same
   two calls the old `#vault-lock-btn` made (`lockVault()` then a synchronous `showScreen('main-
   screen')`, preserving the Session 31 fix that made that navigation synchronous rather than
   awaiting an async gate refresh first); locking the main app reuses `showLockScreen()` wholesale
   rather than re-implementing its branching — it already correctly handles "password set" (shows
   the real lock screen), "quick access" (no-ops back to Home, since there's nothing to lock), and
   would even handle "no credentials row yet" safely, though that path is additionally guarded by
   checking `#first-run-setup` isn't currently showing, so tapping it mid-setup does nothing rather
   than something undefined.

   Always rendered in its "unlocked" appearance, never "locked" — both places it's shown are
   themselves already-unlocked states by construction (the two exclusions below), so there's no
   case where a "locked" appearance would ever be needed. Hidden on the app-open lock screen (same
   condition as the power button, in `showScreen()`) and on the Vault's own locked/setup gate
   (a second, separate exclusion in `refreshVaultGateView()`, since `vault-screen` doesn't change
   screen id when its internal gate/content substate toggles — the same reason the old
   `#vault-lock-btn`'s visibility rule lived in that same function). `#vault-lock-btn` itself is
   removed from the Vault banner entirely — genuinely redundant now, not kept as a second way to do
   the same thing.

2. **Universal Back button:** middle slot of the three-slot bottom nav (`#nav-back-btn`, between
   Home and Vault), styled as a plain rounded square rather than a third tab — no label, no active
   state, since it isn't a destination. Backed by a deliberately simple two-slot toggle, not a full
   history stack: `currentLocation`/`lastLocation` (one of `'home'`/`'settings'`/`'vault'`),
   updated by `recordLocation()` calls placed inside `showScreen()`/`switchMainTab()` themselves —
   every existing caller of those two functions gets Back-button support for free, without needing
   to touch each call site individually. Pressing Back swaps which location is "current" vs "last"
   the same way any other navigation does, so pressing it again toggles right back to where you
   just left, rather than getting stuck unable to return. A deeper stack was deliberately not built:
   this app's actual navigation depth is only ever one level (Home ⇄ Settings ⇄ Vault), so a stack
   would add complexity with no case that could ever use the extra depth.

   Lives physically inside `#bottom-nav`, so it's hidden together with it for free on screens where
   the nav itself is hidden (lock screen, first-run, ad gate) — no separate visibility rule needed,
   unlike the power/lock buttons which live outside any screen and need their own.
   `refreshVaultGateView` (Vault's gate-render function) had to be exposed as `window.
   refreshVaultGateView` for `goBack()` to call it — `goBack()` lives at module top level alongside
   `showScreen()`/`switchMainTab()`, but `refreshVaultGateView` is itself only ever defined inside
   the `DOMContentLoaded` closure; same cross-closure pattern already used for `window.
   tryBiometricUnlockVault`/`window.attemptUnlock`, not a new one.

3. **Every dedicated back element removed:** `#settings-back-btn` ("← Back to Home", added in
   Decision 61) is gone — the universal Back button replaces it. Nothing else in the app had a
   comparable element.

Not done, flagged rather than assumed: whether the lock/unlock button should also show during
first-run-setup was ambiguous in the request (only two exclusions were named: the two biometric
unlock pages) — left visible there per the literal instruction, with the click handler itself
guarding against acting on it before setup completes, rather than silently adding a third exclusion
that wasn't asked for.

---

**66. Back button restyled (new icon, tap-glow); lock button restyled (golden, thicker, persistent
glow) and given a disabled state; power button given a persistent red glow; the Vault's lock button
— reportedly missing — traced to a contrast bug and fixed by removing the Vault's dark title bar and
the bottom nav's dark active-Vault-tab background; lock button hidden on first-run setup.**

Six related changes, requested together after seeing the app running on-device for the first time:

1. **Back button icon** replaced with a "curved return arrow" (chevron + a hooking curve, no
   enclosing circle), matching a supplied reference image minus its outer ring. Soft glow on tap:
   a `.glow` class triggers a 0.5s `box-shadow` keyframe animation in `var(--brand)`, added and force-
   reflowed before re-adding (so a rapid second tap restarts the animation rather than no-op'ing
   since the class is already present) and removed again on `animationend` so it doesn't linger as
   dead state on the element.
2. **Lock button restyled:** color changed from neutral `var(--fg)` to a new `--gold` (`#d4af37`,
   fixed across both themes, same reasoning as `--danger` already being theme-invariant), stroke
   width increased (2 → 2.6, "thickish"), and a persistent `drop-shadow` glow added — unlike the
   back button's tap-triggered animation, this one is always on, describing the button's resting
   state rather than a feedback gesture.
3. **Power button given a persistent red glow** too (`drop-shadow` in `var(--danger)`) — requested
   as permissive ("can"), added for visual parity with the lock button now that it has one.
4. **Disabled state:** on the main-app screens (Home/Settings), the lock button now visually
   disables itself (`filter: grayscale(1); opacity: 0.35; pointer-events: none`) whenever no
   app-open password is set — quick access has nothing for it to lock into, so this makes that
   fact visible on the button itself rather than only in Settings' own copy ("quick access has
   nothing to lock back to"). Computed fresh on every entry into Home/Settings
   (`updateLockButtonDisabledState()`, called from `switchMainTab()`) rather than cached, so it
   stays correct if a future session adds a UI to actually change the app-open password after
   first-run (no such UI exists yet — `setAppPassword()`/`removeAppPassword()` are only ever called
   from first-run-setup today). Never disabled in the Vault context — the Vault always has a PIN by
   the time this button is shown for it at all (hidden otherwise, on its own locked/setup gate).
5. **Vault's lock button, reportedly missing:** it was never actually missing from the DOM — the
   real cause was a contrast bug. `.vault-banner`'s background was `var(--vault)`, a near-black
   deep violet (`#1a1030`), and the fixed lock-toggle-btn icon sitting visually on top of it used
   `var(--fg)` — dark in light theme, nearly the same near-black tone as the banner behind it, so
   the icon was there but effectively invisible. Restyling the icon gold (change 2, this session)
   already fixes the contrast on its own, but the banner's dark background is also removed outright
   (see next point), which was the direct, separately requested fix.
6. **Vault's dark title bar removed** (`.vault-banner` background changed from `var(--vault)` to
   `var(--card-bg)`, with a plain border instead), and the bottom nav's active-Vault-tab background
   changed from `var(--vault)` (dark) to `var(--vault-fg)` (a light lavender tint), with its text/
   icon color swapped to `var(--vault)` so it stays legible — the same "black bar" look, just in the
   bottom nav instead of the top. The Vault's own visual identity (deliberately distinct from the
   rest of the app, per Decision 52/original design notes) now comes from the lock emoji, the
   letter-spaced heading, and the nav tab's lavender tint, rather than a solid dark panel. The inner
   filter chips (`.vault-tab.active` — All/Text/Voice/etc.) were **not** touched — they weren't
   named in the request, and unlike the icon-on-banner case, light-lavender text on that same dark
   background there is genuinely readable, not a contrast bug.
7. **Lock button hidden on first-run setup** (`showScreen()`'s hide condition extended from just
   `'lock-screen'` to `'lock-screen' || 'first-run-setup'`) — there's no password to lock yet at
   that point, so showing it would be actively misleading, not just redundant. This replaces the
   assumption flagged at the end of Decision 65 (left visible there at the time, pending
   confirmation) — now resolved by this direct instruction.

---

**67. Vault title box removed and heading aligned to the lock/power row; lock gold brightened.**

Requested after seeing Decision 66 on-device. (1) `.vault-banner` lost its card background, border,
and padding — the "🔒 PRIVATE VAULT" heading now sits bare on the page. Its `h2` uses the same
20px/1.45 metrics as Home's "Today" heading inside the same 20px `.screen` padding, so its vertical
center (~34px) lands on the fixed lock/power buttons' center (14px top + 20px half-height) by
construction rather than via a magic offset; Home already aligned this way. (2) `--gold` changed
`#d4af37` → `#ffc61a` (the metallic gold read dull on the light background), and the lock's glow
is now two stacked `drop-shadow` layers (3px + 7px) instead of one, so the brighter color reads as
lit rather than flat. Still one fixed value across both themes.

