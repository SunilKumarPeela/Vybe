# Build and download the correct installer

1. Extract this source ZIP. Upload the contents of point-file-differences to your repository root.
2. Replace the old Point workflow with .github/workflows/build-point.yml from this package. In particular, do not use a workflow that generates a version 8.10.4 installer or its own Inno configuration.
3. Run Build Point and wait for the test/install/reinstall gates to pass.
4. Download Point-Hardened-8.32.2-Setup from Artifacts. Extract it and run Point-Hardened-8.32.2-Setup.exe.

The included installer/Point.iss is the only installer definition used by this workflow. It installs directly into Program Files\Point-Hardened and preserves existing policy. VS C++ x64, NuGet, and Inno Setup 6 are needed on the Windows build machine. WebView2 SDK 1.0.4191.47 is pinned. Users do not need development tools. See START-HERE.md for setup and remaining verification requirements.
