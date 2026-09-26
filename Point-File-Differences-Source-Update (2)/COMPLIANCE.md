# Deployment and security boundaries

Read README.md for build and protected-install steps. This is engineering hardening, not a compliance certification or guarantee that all vulnerabilities have been removed.

Installation and policy must be owned by Administrators or SYSTEM and writable only by those principals. The strict policy parser requires all six known settings, rejects duplicate/unknown settings and invalid booleans/retention, and refuses disabled group enforcement. Users need an allowed Windows group; copying and exports additionally need an export group. Policy is loaded from the installed executable directory, not report storage.

Data lives in LOCALAPPDATA/Point/SecureData with a per-user directory ACL. Workspace/configuration writes use Windows DPAPI encryption before touching disk. Imported reports, staging, exports, browser profile material and logs are not all DPAPI-encrypted application files. Windows account/device protection remains necessary. DPAPI does not protect against code already running as the same user.

Clipboard and export paths reauthorize and apply recognized-sensitive-field masking, formula escaping and a payment-card-number heuristic. These checks are not comprehensive DLP: unknown sensitive headings, alternative encodings and screenshots remain outside that boundary. Existing aggregate reports now retain field-origin masking where changed. The changed-cell UI and full-record inspection remain authorized data views.

Daily audit chains serialize writes and reject malformed tails; they are not externally anchored, immutable logs. A user controlling their own data directory can delete or rewrite history. Retention removes expired transient material; durable configuration is exempt. Removal and audit failures are surfaced.

Migration: import approved existing reports into the new per-user Inbox. Re-add fetcher schedules, because legacy plaintext fetcher configuration is not loaded. Review/sign the native XLSX reader and install it with the protected application. Do not place legacy user data inside the new Program Files installation: deployment refuses to reset ACLs over recognized legacy data directories.

Before production, run the Windows build and tests and exercise real standard-user/admin ACLs, expired files, denied export, clipboard paths, DPAPI, companion fetchers and trusted reader failures. Test adversarial workbooks using the actual reader. Neither Windows integration nor the missing reader was executable in this environment.
