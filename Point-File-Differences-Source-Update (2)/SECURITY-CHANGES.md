# Security changes in this source update

- Strict, fail-closed policy parsing; protected policy/install ownership and ACL checks; mandatory Windows group authorization.
- Separate protected installation from per-user data, reject linked directories in checked paths, and encrypt supported workspace/configuration payloads before writing them.
- Clipboard authorization, sensitive-field masking, formula escaping and PAN detection, including embedded digit runs; cut only follows successful protected copy. Existing aggregate report masking now uses original field metadata.
- Atomic CSV publication, explicit I/O failures, serialized daily audit append and malformed-tail rejection. Audit chains remain locally mutable rather than tamper-proof.
- Remove broadcast-window-message import triggers; use Inbox filesystem notifications.
- CSV/XLSX only; bounded files, stricter ZIP container checks, macro-entry/encryption/traversal/oversized-container rejection and anchored HTML detection. Legitimate CSV text such as “access denied” is accepted. Structural ZIP checks do not replace a safe XML/decompression reader.
- Remove Excel automation fallback. Require the protected, signed native reader, system PowerShell path, AllSigned policy, process-tree job containment, cancellation and timeout. Reader source was absent and is not audited.
- Fetcher filename validation, URL credential restrictions, bound downloads, partial-output cleanup, no automatic WinHTTP redirects, protected schedule storage and atomic browser-to-Inbox handoff. Redirect-dependent downloads may need the approved final URL. Browser popups/permissions are denied, which may affect some sign-in workflows.
- Pin the selected WebView2 SDK version for downloads, add regression gates and an administrator deployment script. Windows execution of those gates is still required.

These address identified defects in the supplied code. They are not a complete penetration test, a proof of absence of security bugs, or a replacement for deployment review and dependency maintenance. There may be additional findings in unreviewed paths or absent project components.
