CURRENT INSTALLER: Point-Hardened-8.32.2-Setup.exe. Follow START-HERE.md and GITHUB-BUILD.md; these supersede older deployment instructions below. Inno Setup now manages destination files directly.

UPDATED USER INSTALLATION: use the single Point-Setup.exe described in START-HERE.md. The batch deployment details below are for administrators. The included workflow now builds a setup EXE and the Inno Setup definition is now supplied.

See START-HERE.md for the corrected installation steps and startup ownership fix.

# Point: database/AD reconciliation and before/after comparison

This is an updated C++20 Windows source package, not a compiled or production-certified application. The original README is retained as README-original.md; this document supersedes its setup/security instructions.

## Three-block workflow

1. Import the two CSV reports, then open **Workspace > Reconcile Database vs AD (File Differences)...**. XLSX requires the signed reader described below.
2. Block 1 selects the database source and its fields. Block 2 selects the AD source and corresponding fields. Imported worksheets are separate sources.
3. Map a stable unique identifier as a **KEY** pair. Map attributes to inspect as **COMPARE** pairs. Review suggested mappings before running.

| Database field | AD field | Role |
| --- | --- | --- |
| EmplID | EID or EmployID | KEY |
| FirstName | Firsnm | COMPARE |
| Department | Dept | COMPARE |

Use the employee ID to identify the person. Do not include FirstName in the key: a name change should produce one modified record. If one employee has several AD accounts, choose a reliable composite key or review the duplicate-key results; the app never chooses an arbitrary duplicate.

4. Run the comparison. Block 3 shows modified rows, rows only in either source, and missing/duplicate keys requiring review. Equal records are hidden. Changed value cells appear in red; review issues appear in amber. Double-click to inspect complete source records.
5. Filter by result type or changed field. Export saves a CSV under the per-user Exports folder as `point-file-changes.csv`, replacing the previous export. CSV cannot preserve red formatting; it contains explicit Before/After values, field names and difference status. Export uses one line per changed field, so one displayed record can yield several CSV lines. A field filter selects records; export includes all changes for those selected records.

## Before and after

Choose **Workspace > Compare Files (Before / After)...**, select the old source as A and the new source as B, and retain **Compare ALL fields**. This checks all unselected attributes too, pairs uniquely recognized synonymous headers and reports added/removed columns. Ambiguous header synonyms require an explicit mapping. Unique keys produce modified records; unmatched keys produce removed/added records. Changing the key itself is removed plus added, because identity cannot be inferred safely.

The reconciliation mode defaults to selected attributes only. The before/after mode defaults to all attributes. Both use the same three-block window. Existing person/group Compare remains available.

## Comparison rules and large files

Leading zeros and punctuation are preserved as imported. Keep employee IDs as text in the original workbook: the app cannot reconstruct zeros already lost by Excel. Outer whitespace is trimmed by default; case comparison is exact unless ASCII case folding is selected. Internal whitespace is preserved. Unicode case folding is not implemented. Any missing component of a key is a review issue.

The index uses complete, length-prefixed composite keys and expected linear work in input size, rather than repeated VLOOKUP scans or a Cartesian join. Results have no artificial 100,000-row cutoff. Comparison and streaming export use background workers with cancellation; results use a virtual list. Import and snapshot copies still require memory and may pause the UI. RAM holds existing imports, snapshots of the two selected sources, indexes and result references. Benchmarks in VALIDATION.md measure comparison alone, not workbook reading or the Windows UI. For datasets exceeding available RAM, an external-memory database implementation is still required.

Source row numbers are parsed row positions plus two, not guaranteed physical Excel row numbers when headers or multiline CSV records are normalized. Comparison is of imported cell values, not workbook formatting, formulas as formulas, comments or charts.

## Build and deploy on Windows

1. Merge this package's `src/`, `tests/`, build/test/deployment batch files and updated documentation into a backup or branch of your original project. Preserve any original files absent from this package.
2. Use an x64 Native Tools Command Prompt for Visual Studio with C++ tools and Windows SDK. Run `build_point_only.bat` for the main application. It runs the regression and Windows checks first. For companion fetchers, use `build.bat` with the default pinned SDK; optionally override `WEBVIEW2_SDK_VERSION`. See GITHUB-BUILD.md for the complete workflow.
3. Review the six settings in `point-security.conf`. Group enforcement is mandatory in this hardened build.
4. From an Administrator Command Prompt run `install_hardened.bat`. It installs to Program Files with protected ownership and permissions. Running directly from a user-writable build/Downloads folder intentionally fails the installation-policy ACL check.
5. Provision the intended account with `configure_compliance.bat "DOMAIN\username" export`. Omit `export` for access without clipboard/CSV export permission. If already a group member, verify membership and add any missing membership manually; the helper reports duplicate membership as an error. Sign out and in to refresh group membership.
6. Start the installed application and import the sample CSVs, then your approved reports. Data is under `%LOCALAPPDATA%\Point\SecureData`; it is separate from protected binaries and policy.

The supplied sources did not include `scripts/point_xlsx_to_csv.ps1`, the original schema-mapping test or original sample directory. A new Inno Setup definition is now included; use GITHUB-BUILD.md to build and test Point-Setup.exe. No executable was built in this Linux environment.

**XLSX dependency:** CSV works without an Excel reader. XLSX imports require your original reviewed reader at `scripts/point_xlsx_to_csv.ps1`, signed by a publisher trusted on the workstation. PowerShell runs it with AllSigned and a timeout. The automatic Excel automation fallback was removed. XLS/XLSM/XLSB are rejected; save an approved macro-free XLSX or CSV first. The absent reader has not been reviewed or tested here. Do not bypass signing to restore imports.

## Example

Import `examples/file-differences/Database.csv` and `AD.csv`. Pair EmplID/EID as KEY and FirstName/Firsnm plus Department/Dept as COMPARE. Expect one modified employee (002), one database-only employee (003), one AD-only employee (004), three duplicate-key review rows (005) and one missing-key review row. Employee 001 matches and is hidden. With duplicate-free `Before.csv` and `After.csv`, expect one modified, one removed and one added record.

## Upload the update to GitHub

Create a branch in your existing local repository and copy the package over it, preserving original missing dependencies. Run the Windows build/tests, then review the diff. Keep real employee reports, credentials, exports, user-data folders and build output out of Git.

```sh
git switch -c feature/file-reconciliation
git status
git add src tests examples README.md VALIDATION.md COMPLIANCE.md SECURITY-CHANGES.md build.bat build_debug.bat build_point_only.bat test_file_differences.bat install_hardened.bat configure_compliance.bat
git diff --cached
git commit -m "Add keyed file reconciliation and harden data handling"
git push -u origin feature/file-reconciliation
```

Open a pull request for review. If this is a new repository, first create an empty private GitHub repository, initialize locally with `git init`, and add its provided URL using `git remote add origin YOUR_REPOSITORY_URL`. Do not commit actual database/AD exports. This package has not been pushed or published.
