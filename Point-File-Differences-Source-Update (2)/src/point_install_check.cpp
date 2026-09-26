#include "point_compliance.h"
#define NOMINMAX
#include <windows.h>
#include <lm.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <fstream>
#include <iostream>
#include <exception>
#include <vector>
#include <string>

static std::vector<unsigned char> account_sid(const wchar_t* account) {
    DWORD size = 0, domain_size = 0; SID_NAME_USE type{};
    LookupAccountNameW(nullptr, account, nullptr, &size, nullptr, &domain_size, &type);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
        throw std::runtime_error("Windows account was not found. Enter COMPUTER\\username or DOMAIN\\username.");
    std::vector<unsigned char> sid(size); std::vector<wchar_t> domain(domain_size + 1);
    if (!LookupAccountNameW(nullptr, account, sid.data(), &size, domain.data(), &domain_size, &type) || type != SidTypeUser)
        throw std::runtime_error("Choose one existing Windows user account, not a group.");
    return sid;
}
static void write_current_account(const wchar_t* destination) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot identify the original Windows user.");
    struct Guard { HANDLE h; ~Guard() { CloseHandle(h); } } guard{token};
    TOKEN_ELEVATION elevation{}; DWORD count = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &count) || elevation.TokenIsElevated)
        throw std::runtime_error("Setup started elevated; confirm the intended Windows user manually.");
    GetTokenInformation(token, TokenUser, nullptr, 0, &count);
    if (!count) throw std::runtime_error("Cannot read original user SID.");
    std::vector<unsigned char> data(count);
    if (!GetTokenInformation(token, TokenUser, data.data(), count, &count)) throw std::runtime_error("Cannot read user token.");
    const auto sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    DWORD name_size = 0, domain_size = 0; SID_NAME_USE type{};
    LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size, &type);
    std::vector<wchar_t> name(name_size + 1), domain(domain_size + 1);
    if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(), &domain_size, &type) || type != SidTypeUser)
        throw std::runtime_error("Cannot resolve the original user.");
    const std::wstring account = std::wstring(domain.data()) + L"\\" + name.data();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, account.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) throw std::runtime_error("Cannot encode account name.");
    std::vector<char> utf8(static_cast<size_t>(bytes));
    WideCharToMultiByte(CP_UTF8, 0, account.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(destination, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot return detected account to setup.");
    Guard output{file}; DWORD written = 0;
    if (!WriteFile(file, utf8.data(), static_cast<DWORD>(bytes - 1), &written, nullptr) || written != static_cast<DWORD>(bytes - 1))
        throw std::runtime_error("Cannot finish writing detected account.");
}
static std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value))) throw std::runtime_error("Cannot locate user shortcuts.");
    const std::filesystem::path result(value); CoTaskMemFree(value); return result;
}
static void refresh_shortcuts(const std::filesystem::path& root) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) throw std::runtime_error("Cannot initialize shortcut update.");
    struct ComGuard { ~ComGuard() { CoUninitialize(); } } com_guard;
    const auto old_target = known_folder(FOLDERID_LocalAppData) / L"Programs" / L"Point" / L"Point.exe";
    const auto new_target = root / L"Point.exe";
    for (const auto& directory : {known_folder(FOLDERID_Desktop), known_folder(FOLDERID_Programs)}) {
        if (!std::filesystem::exists(directory)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
            const auto attrs = GetFileAttributesW(entry.path().c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) || !entry.is_regular_file()) continue;
            const auto extension = entry.path().extension().wstring();
            if (_wcsicmp(extension.c_str(), L".lnk") != 0) continue;
            IShellLinkW* link = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) continue;
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                wchar_t target[32768]{};
                if (SUCCEEDED(file->Load(entry.path().c_str(), STGM_READWRITE)) &&
                    SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)) &&
                    CompareStringOrdinal(target, -1, old_target.c_str(), -1, TRUE) == CSTR_EQUAL) {
                    link->SetPath(new_target.c_str()); link->SetWorkingDirectory(root.c_str());
                    link->SetIconLocation(new_target.c_str(), 0);
                    if (FAILED(file->Save(entry.path().c_str(), TRUE))) std::cerr << "One old shortcut could not be updated. Use Point-Hardened.\n";
                }
                file->Release();
            }
            link->Release();
        }
    }
}
static void provision(const wchar_t* account, bool exports) {
    const auto sid = account_sid(account);
    for (const auto* name : {L"Point Users", L"Point Exporters", L"Point Administrators"}) {
        LOCALGROUP_INFO_0 group{}; group.lgrpi0_name = const_cast<LPWSTR>(name);
        const auto status = NetLocalGroupAdd(nullptr, 0, reinterpret_cast<LPBYTE>(&group), nullptr);
        if (status != NERR_Success && status != NERR_GroupExists && status != ERROR_ALIAS_EXISTS)
            throw std::runtime_error("Could not create Point access group. Windows error " + std::to_string(status));
    }
    for (const auto* name : {L"Point Users", L"Point Exporters"}) {
        if (!exports && std::wstring(name) == L"Point Exporters") continue;
        LOCALGROUP_MEMBERS_INFO_0 member{}; member.lgrmi0_sid = const_cast<unsigned char*>(sid.data());
        const auto status = NetLocalGroupAddMembers(nullptr, name, 0, reinterpret_cast<LPBYTE>(&member), 1);
        if (status != NERR_Success && status != ERROR_MEMBER_IN_ALIAS)
            throw std::runtime_error("Could not grant Point access. Windows error " + std::to_string(status));
    }
}
static void protect_install_path(const std::filesystem::path& path, bool directory) {
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Repair cannot use missing or linked installation paths.");
    PSECURITY_DESCRIPTOR sd = nullptr;
    const wchar_t* text = directory ?
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)" :
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &sd, nullptr))
        throw std::runtime_error("Cannot prepare protected installation permissions.");
    PSID owner = nullptr; PACL acl = nullptr; BOOL defaulted = FALSE, present = FALSE;
    GetSecurityDescriptorOwner(sd, &owner, &defaulted);
    GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    const DWORD result = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        owner, nullptr, acl, nullptr);
    LocalFree(sd);
    if (result != ERROR_SUCCESS) throw std::runtime_error("Windows could not set installation permissions. Error " + std::to_string(result));
}
static void repair_install(const wchar_t* account) {
    account_sid(account);
    const auto source = point::compliance::application_directory();
    const auto target = known_folder(FOLDERID_ProgramFilesX64) / L"Point-Hardened";
    if (!std::filesystem::exists(target)) std::filesystem::create_directory(target);
    protect_install_path(target, true);
    auto copy = [&](const std::filesystem::path& relative, bool required, bool preserve) {
        const auto from = source / relative, to = target / relative;
        if (preserve && std::filesystem::exists(to)) { protect_install_path(to, false); return; }
        if (!std::filesystem::exists(from)) {
            if (required) throw std::runtime_error("Repair files are incomplete. Run the latest Point-Setup.exe.");
            return;
        }
        const DWORD attrs = GetFileAttributesW(from.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
            throw std::runtime_error("Invalid repair source file.");
        if (std::filesystem::exists(to)) protect_install_path(to, false);
        if (CompareStringOrdinal(from.c_str(), -1, to.c_str(), -1, TRUE) != CSTR_EQUAL)
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
        protect_install_path(to, false);
    };
    copy(L"point-security.conf", true, true);
    copy(L"Point.exe", true, false); copy(L"PointInstallCheck.exe", true, false);
    copy(L"PointFetcher.exe", false, false); copy(L"PointBrowserFetcher.exe", false, false);
    if (std::filesystem::exists(source / L"scripts" / L"point_xlsx_to_csv.ps1")) {
        if (!std::filesystem::exists(target / L"scripts")) std::filesystem::create_directory(target / L"scripts");
        protect_install_path(target / L"scripts", true);
        copy(L"scripts/point_xlsx_to_csv.ps1", true, false);
    }
    point::compliance::load_policy(target);
    provision(account, false);
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(result)) {
        IShellLinkW* link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
            const auto executable = target / L"Point.exe";
            link->SetPath(executable.c_str()); link->SetWorkingDirectory(target.c_str()); link->SetIconLocation(executable.c_str(), 0);
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                const auto shortcut = known_folder(FOLDERID_PublicDesktop) / L"Point-Hardened.lnk";
                file->Save(shortcut.c_str(), TRUE); file->Release();
            }
            link->Release();
        }
        CoUninitialize();
    }
}
static void preflight_install() {
    const auto target = known_folder(FOLDERID_ProgramFilesX64) / L"Point-Hardened";
    if (!std::filesystem::exists(target)) return;
    const auto check = [](const std::filesystem::path& item) {
        const DWORD attributes = GetFileAttributesW(item.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Linked or unreadable installation path. Ask your administrator to repair it.");
    };
    check(target);
    for (const auto& item : std::filesystem::recursive_directory_iterator(target)) check(item.path());
}
static void secure_installed() {
    preflight_install();
    const auto target = known_folder(FOLDERID_ProgramFilesX64) / L"Point-Hardened";
    protect_install_path(target, true);
    for (const auto* name : {L"Point.exe", L"PointFetcher.exe", L"PointBrowserFetcher.exe", L"PointInstallCheck.exe", L"point-security.conf"})
        protect_install_path(target / name, false);
    if (std::filesystem::exists(target / L"scripts")) {
        protect_install_path(target / L"scripts", true);
        if (std::filesystem::exists(target / L"scripts/point_xlsx_to_csv.ps1"))
            protect_install_path(target / L"scripts/point_xlsx_to_csv.ps1", false);
    }
    point::compliance::load_policy(target);
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--preflight-install") { preflight_install(); return 0; }
        if (argc == 2 && std::wstring(argv[1]) == L"--secure-installed") { secure_installed(); return 0; }
        if (argc == 3 && std::wstring(argv[1]) == L"--repair-install") {
            repair_install(argv[2]); return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--detect-account") {
            write_current_account(argv[2]); return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--validate-account") {
            account_sid(argv[2]); return 0;
        }
        const bool setup = argc == 4 && std::wstring(argv[1]) == L"--provision";
        const bool shortcuts = argc == 2 && std::wstring(argv[1]) == L"--refresh-shortcuts";
        if (argc != 1 && !setup && !shortcuts) throw std::runtime_error("Invalid installation-check arguments.");
        const auto root = point::compliance::application_directory();
        const auto policy = point::compliance::load_policy(root);
        if (shortcuts) { refresh_shortcuts(root); return 0; }
        if (setup) {
            const std::wstring choice(argv[3]);
            if (choice != L"export" && choice != L"view") throw std::runtime_error("Invalid access option.");
            provision(argv[2], choice == L"export");
            std::cout << "Access configured. Sign out and back in to refresh Windows group membership.\n";
        }
        std::wcout << L"PASS: protected folder, policy ownership, ACL and policy syntax: " << root.wstring() << L"\n";
        if (!setup) {
            try { point::compliance::authorize_current_user(policy); std::cout << "Current account has Point access.\n"; }
            catch (const std::exception& ex) { std::cout << "Installation valid; account provisioning is separate: " << ex.what() << "\n"; }
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "INSTALLATION CHECK FAILED: " << ex.what() << "\n";
        if (argc > 1 && std::wstring(argv[1]) == L"--repair-install")
            MessageBoxA(nullptr, ex.what(), "Point repair could not finish", MB_OK | MB_ICONERROR);
        return 1;
    }
}
