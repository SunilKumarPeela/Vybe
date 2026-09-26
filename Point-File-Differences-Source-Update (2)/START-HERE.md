# Build the corrected Point installer

The screenshot showing Setup - Point version 8.10.4 is an older installer.
This package builds Point-Hardened-8.32.2-Setup.exe.

## Upload once

1. Cancel the old setup and save and close Point.
2. Extract this ZIP. Upload its CONTENTS to your GitHub repository root,
   where build.bat must appear directly. Do not upload the ZIP itself.
3. In GitHub, open your existing installer workflow under .github/workflows.
   Click Edit. Replace ALL its text with the contents of INSTALLER-WORKFLOW.txt
   from this package, then commit. This avoids keeping the old workflow that
   generates its own version 8.10.4 installer. If you have other obsolete Point
   installer workflows, disable those in Actions.
4. In Actions, select "Point - Corrected installer (8.32.2)" and run it.
5. Only after the run succeeds, download the artifact named
   Point-Hardened-8.32.2-Setup. Extract it and double-click the EXE inside.

## What users do

Double-click the new setup, approve Windows permission, and install.
Open the new Point-Hardened shortcut. Setup automatically secures the installed
files and provisions the chosen Windows account. Windows group membership may
require one sign-out/sign-in.

Setup targets Program Files\Point-Hardened, not the old AppData Point.exe.
It preserves the old application's files and does not force-kill processes.
For upgrades, Setup can ask users to close a running protected copy.

## Validation and limits

This package includes comparison tests, security tests, and Windows installation
and reinstall checks in the workflow. The updated workflow checks the installer
configuration before building. Local archive and configuration checks passed;
the Windows compiler and installer are unavailable in this environment, so a
successful Windows workflow and interactive installation remain required.

CSV works independently. XLSX requires the trusted signed reader that was not
supplied with the original source. The EXE remains unsigned unless a signing
certificate is configured. This package is source, not a prebuilt executable.
