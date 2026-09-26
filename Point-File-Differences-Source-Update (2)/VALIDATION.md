# Validation — 24 September 2026

## Completed in Linux using GCC C++20

Compiled the portable core and three test programs with `-O2 -Wall -Wextra -Werror -Wpedantic -pthread`. All passed:

- `reconcile_test`: synonym mapping, stable/composite keys, modified and unmatched records, duplicate/missing key review, schemas, selected/all attributes, normalization, masking, formula escaping, PAN blocking and cancellation.
- Large reconciliation: 200,000 rows on each side; 150,000 modified, one removed, one added and 49,999 unchanged. All 150,002 differences retained. Final optimized run: 319 ms comparison only. This is a synthetic benchmark, not an import, GUI or production throughput guarantee.
- `file_diff_test`: retained legacy tuple engine coverage for field pairing, duplicate counts/presence, blanks, zeros, punctuation, normalization, ragged/empty inputs and cancellation. 100,000 A / 101,000 B rows, 1,000 A-only, 2,000 B-only, 99,000 matches; 45 ms comparison only.
- `security_regression_test tests/fixtures`: strict policy value helpers, safe filenames, macro-format rejection, legitimate CSV text, HTML rejection, ZIP container fixtures, 401 serialized audit entries, and rejection of a truncated audit tail. Portable audit hashing is a test fallback; this does not validate Windows BCrypt or cross-process locking.

Recompiled the portable core and reconciliation/security tests with AddressSanitizer and UndefinedBehaviorSanitizer. Both passed, including the 200,000-row case and ZIP fixtures. Leak detection was disabled (`ASAN_OPTIONS=detect_leaks=0`) because of this traced environment; no leak-safety claim is made.

## Not executed here

No Windows compiler/runtime was available. Windows GUI compilation and interaction, red-cell rendering, build/deployment batch scripts, protected installation ACL checks, real group tokens, clipboard behavior, DPAPI encryption, process-tree containment, BCrypt audit hashing, filesystem notifications, browser/API fetchers and PowerShell signing were not run.

`test_file_differences.bat` supplies portable regressions, a real Windows DPAPI round-trip/policy-parser test and an interface compile check. It is a build gate for the updated build scripts. The policy parser unit test does not exercise production installation ACLs; manually test standard-user and administrator deployment and denied operations too.

The native XLSX reader and original installer definition were not supplied. XLSX end-to-end import and installer packaging remain unverified. CSV/XLSX ZIP checks are structural validation, not exhaustive hostile-input testing. Performance remains limited by RAM, import and snapshot costs.

Do not treat these test results as proof that every security defect has been removed. Complete Windows and deployment verification before using production employee records.

## Installation packaging correction — 25 September 2026 UTC

Verified YAML parsing, checker inclusion in each build script and artifact, policy source lookup, and archive integrity. Windows installer/checker execution remains unrun locally. The workflow includes a Windows protected-installation gate before artifact upload. This update does not alter comparison algorithms.

## Single EXE installer update

Added Inno Setup source, native account validation/provisioning and installer build/silent-test gates. YAML structure, referenced source paths, helper linkage and ZIP integrity checked locally. Inno compilation, Windows group provisioning, interactive/silent installation and uninstall are not executed here. The workflow explicitly tests installer exit status, policy checks and chosen account membership on Windows before uploading the EXE.

## Automatic account detection and shortcut correction

Added original-user detection with explicit-account fallback for elevated/unavailable original contexts. Preserved opt-in export access. Added exact-target legacy Desktop/Start Menu shortcut updates and a Windows CI regression for shortcut repair. YAML parsing, build linkage and source wiring checked locally; actual UAC/original-user detection, Inno compilation and Windows runtime checks remain unrun here.

## Startup recovery update

Added a validated installed-copy handoff and administrator-approved repair helper. Included the startup header in the Windows compile gate and added a legacy-location repair CI step. Local checks verify source wiring, helper linkage, workflow YAML and archive integrity only. Neither repair APIs nor GUI/UAC paths have been executed in this environment.

## 8.32.2 installer correction

Replaced hidden batch deployment with direct Inno Files entries so Restart Manager sees actual destination executables. Added preflight link checks, exact destination enforcement, native ACL finalization and a reinstall CI gate. Distinct versioned artifact prevents confusion with 8.10.4. Local wiring/YAML/archive checks only; Windows/Inno and in-use-file interaction remain unrun here. Negative Windows crash exit statuses now fail the native test gate.

## Security regression runner fix — 26 September 2026

Closed the audit input stream before test cleanup (required for Windows file deletion), replaced aborting assertions with checked exceptions, captured worker-thread exceptions, used unique test directories, and printed test stages and native exit codes. Recompiled core and security test with GCC C++20 -O2 -Wall -Wextra -Werror -Wpedantic -pthread. All security cases and ZIP fixtures passed locally. A deliberately missing fixture produced a readable failure and exit code 1. Windows rerun remains pending; the screenshot did not expose the original exception, so the cleanup defect is a concrete fix rather than a proven sole cause.


## Installer delivery correction
Removed the outer directory from the source ZIP. Added INSTALLER-WORKFLOW.txt, identical to the canonical workflow, to make replacing the old GitHub installer workflow straightforward. Renamed the workflow and added installer configuration preflight. No Windows execution claimed.
