# Roadmap

Last updated: 2026-09-11 (seed).

- [x] **Phase 0 — Repo scaffold & docs.** Seed `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/ROADMAP.md`, `docs/SESSIONS.md`; establish folder structure.
- [ ] **Phase 1 — Infra & secrets (manual, one-time).**
  - **Package name.** Decide the Android application ID (e.g. `com.yourname.matchemojisdaily`) before registering anything below — Firebase, AdMob, and the Capacitor config all reference it, and it's painful to change later.
  - **Supabase.** Create project. Enable email OTP auth provider. Wire a custom SMTP provider (e.g. Resend, Postmark, SendGrid) into Supabase Auth — the built-in email sender is rate-limited and dev-only, not suitable for real signups. Generate an access token and note the project ref, both stored as GitHub Actions secrets for `deploy-functions.yml`. Stays on the Free tier initially.
  - **Firebase (Analytics + Crashlytics only).** Create project. Register the Android app using the package name above. Download `google-services.json` — store as a GitHub secret and write it to `client/android/app/` during CI, rather than committing it directly, since the repo is public. Enable Crashlytics in the console.
  - **Google AdMob.** Create account. Register the app (needs the package name). Create ad units: rewarded video (ad-life + bonus-round entry), interstitial (capped), banner (optional). Use test ad unit IDs for development; production IDs are a Phase 12 item. Link the AdMob account to the Firebase project once both exist, for revenue-by-cohort Analytics reporting.
  - **Android signing.** Generate a release keystore. Store the keystore file (base64-encoded), keystore password, key alias, and key password as GitHub Actions secrets for `build-apk.yml`.
  - **GitHub repo settings.** Enable GitHub Pages so `privacy-policy.html` is actually served. Add all secrets above once the accounts they come from exist. (The connected GitHub integration currently lacks write access to this repo — not blocking, since the file-delivery workflow is manual-upload-only, but worth fixing separately if direct pushes are ever wanted.)
  - **Branding assets.** Placeholder app icon / adaptive icon and splash screen — needed for a real-looking Capacitor Android build even before final art exists.
  - **Google Play Console account** (not blocking, but has long lead time). One-time $25 fee plus identity verification, which has been taking Google noticeably longer in some cases recently — worth starting early even though actual submission is Phase 12.
- [ ] **Phase 2 — Database schema.** Postgres tables/migrations in Supabase for `users`, `daily_game_definitions`, `player_daily_order`, `attempts`, `daily_stats`, `weekly_stats`, `all_time_stats`, `user_year_activity` (ARCHITECTURE.md Section 6).
- [ ] **Phase 3 — Auth.** Email OTP signup/login flow; profile screen for changing name and email (email change re-verified via OTP).
- [ ] **Phase 4 — Core game client.** Port the existing HTML/CSS/JS match-3 prototype into `client/src/`; implement the 26-slot theme system, fixed per-slot move targets, shared per-attempt life pool, forced-sequential play, and bonus-round trigger.
- [ ] **Phase 5 — Score integrity.** Edge Function that accepts `{seed, moves[]}`, deterministically replays a run, and returns the authoritative score/time-bonus/lives-used/levels-reached. Wired as a single call per completed attempt.
- [ ] **Phase 6 — Daily game-definition generation.** Scheduled job producing the day's 12 game definitions (board pattern + theme shuffle per slot) and assigning each player's serving-order permutation.
- [ ] **Phase 7 — Leaderboards.** 7-tier cascade queries for daily, weekly, and all-time scopes; per-player rank-breakdown data for the "how you got this rank" UI.
- [ ] **Phase 8 — Attempt cap & forfeit tracking.** 12/day slot enforcement keyed to start timestamp; server-side heartbeat/timeout forfeit detection; attempt-history UI (start time, status, scored-day).
- [ ] **Phase 9 — All-time stats & calendar.** Per-player stats view; month/year calendar reading `user_year_activity`, drilling into per-day attempt detail.
- [ ] **Phase 10 — Ads.** AdMob rewarded-video integration for ad-life and bonus-round entry (single ad per trigger); AdMob SSV verification wired to the score-integrity Edge Function.
- [ ] **Phase 11 — CI/CD.** `build-apk.yml` and `deploy-functions.yml` wired to the Phase 1 secrets; first real APK produced via GitHub Actions and attached to a GitHub Release.
- [ ] **Phase 12 — Play Store prep.** Privacy policy page published via GitHub Pages; production AdMob ad unit IDs swapped in; store listing assets.

Phases are worked in order; a phase is not started without the prior phase's code passing CI.
