#pragma once
#include "point_compliance.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace point::startup {
inline std::filesystem::path installed_directory() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &value)))
        throw std::runtime_error("Cannot locate the Point installation folder.");
    const auto result = std::filesystem::path(value) / L"Point-Hardened";
    CoTaskMemFree(value); return result;
}
inline bool launch(const std::filesystem::path& target) {
    const auto executable = target / L"Point.exe";
    if (!std::filesystem::is_regular_file(executable)) return false;
    compliance::load_policy(target);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, target.c_str(), SW_SHOWNORMAL)) > 32;
}
inline std::wstring current_account() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot identify your Windows account.");
    struct Guard { HANDLE token; ~Guard() { CloseHandle(token); } } guard{token};
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (!size) throw std::runtime_error("Cannot inspect your Windows account.");
    std::vector<unsigned char> data(size);
    if (!GetTokenInformation(token, TokenUser, data.data(), size, &size)) throw std::runtime_error("Cannot inspect your Windows account.");
    DWORD names = 0, domains = 0; SID_NAME_USE kind{};
    PSID sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    LookupAccountSidW(nullptr, sid, nullptr, &names, nullptr, &domains, &kind);
    std::vector<wchar_t> name(names + 1), domain(domains + 1);
    if (!LookupAccountSidW(nullptr, sid, name.data(), &names, domain.data(), &domains, &kind)) throw std::runtime_error("Cannot resolve your Windows account.");
    return std::wstring(domain.data()) + L"\\" + name.data();
}
// Returns true when this process should exit after handing off or presenting recovery.
inline bool recover(const std::filesystem::path& source) {
    try {
        const auto target = installed_directory();
        if (CompareStringOrdinal(source.c_str(), -1, target.c_str(), -1, TRUE) != CSTR_EQUAL) {
            try { if (launch(target)) return true; } catch (...) {}
        }
        const auto helper = source / L"PointInstallCheck.exe";
        if (!std::filesystem::is_regular_file(helper)) {
            MessageBoxW(nullptr, L"This Point copy needs the current setup package. Run the latest Point-Setup.exe once; it will install and configure Point automatically.", L"Finish setting up Point", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        if (MessageBoxW(nullptr, L"Point needs to finish its installation. Repair it automatically now? Windows will ask for administrator approval.", L"Finish setting up Point", MB_YESNO | MB_ICONINFORMATION) != IDYES) return true;
        const auto args = L"--repair-install \"" + current_account() + L"\"";
        SHELLEXECUTEINFOW execute{}; execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS; execute.lpVerb = L"runas";
        execute.lpFile = helper.c_str(); execute.lpParameters = args.c_str(); execute.nShow = SW_HIDE;
        if (!ShellExecuteExW(&execute)) {
            if (GetLastError() != ERROR_CANCELLED)
                MessageBoxW(nullptr, L"Windows could not start the repair. Run the latest Point-Setup.exe to complete installation.", L"Point setup", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        // Keep the message queue responsive while the elevated helper completes.
        DWORD code = 1;
        if (execute.hProcess) {
            while (MsgWaitForMultipleObjects(1, &execute.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            GetExitCodeProcess(execute.hProcess, &code); CloseHandle(execute.hProcess);
        }
        if (code != 0) return true;
        const auto policy = compliance::load_policy(target);
        try { compliance::authorize_current_user(policy); }
        catch (...) {
            MessageBoxW(nullptr, L"Point is installed. Sign out of Windows and sign back in once, then open the Point-Hardened desktop shortcut. If your organization uses custom access groups, contact your Point administrator.", L"Point setup complete", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        if (!launch(target)) MessageBoxW(nullptr, L"Point is installed. Open the Point-Hardened desktop shortcut.", L"Point setup complete", MB_OK | MB_ICONINFORMATION);
    } catch (...) {
        MessageBoxW(nullptr, L"Automatic setup could not finish. Run the latest Point-Setup.exe, or ask your administrator to install it.", L"Point setup", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}
}
