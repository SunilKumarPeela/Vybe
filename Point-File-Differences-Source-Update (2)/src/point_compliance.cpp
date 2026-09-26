#include "point_compliance.h"
#include "point_security_rules.h"
#include <unordered_set>

#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <wincrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace point::compliance {
namespace {

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}


std::wstring widen_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Invalid UTF-8 in security policy");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count);
    return result;
}

bool current_token_is_member(const std::wstring& account_name) {
    DWORD sid_bytes = 0;
    DWORD domain_chars = 0;
    SID_NAME_USE use{};
    LookupAccountNameW(
        nullptr, account_name.c_str(), nullptr, &sid_bytes,
        nullptr, &domain_chars, &use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    std::vector<unsigned char> sid(sid_bytes);
    std::wstring domain(domain_chars, L'\0');
    if (!LookupAccountNameW(
            nullptr, account_name.c_str(), sid.data(), &sid_bytes,
            domain.data(), &domain_chars, &use)) {
        return false;
    }
    BOOL member = FALSE;
    return CheckTokenMembership(nullptr, sid.data(), &member) && member;
}

std::vector<unsigned char> read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open protected file");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || length > 64 * 1024 * 1024)
        throw std::runtime_error("Protected file exceeds safety limit");
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty())
        input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) throw std::runtime_error("Protected file read failed");
    return bytes;
}

void write_binary_atomic(
        const std::filesystem::path& path,
        const unsigned char* data, std::size_t size) {
    auto temporary = path;
    temporary += L".secure.tmp";
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Unable to write protected file");
        output.write(
            reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(size));
        if (!output) throw std::runtime_error("Protected file write failed");
    }
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("Unable to publish protected file");
    }
}

void delete_expired_files(
        const std::filesystem::path& directory, int retention_days) {
    if (retention_days < 1) return;
    std::error_code ec;
    const auto cutoff = std::filesystem::file_time_type::clock::now() -
        std::chrono::hours(24LL * retention_days);
    for (const auto& entry :
         std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        // This DPAPI-protected policy is durable application configuration,
        // not an expiring saved query or generated workspace artifact.
        if (entry.path().filename() == L"field-synonyms.dat" ||
            entry.path().filename() == L"user-relationships.dat" ||
            entry.path().filename() == L"auto-import-schedules.dat" ||
            entry.path().filename() == L"point-audit.lock") continue;
        const auto modified = entry.last_write_time(ec);
        if (!ec && modified < cutoff) {
            std::filesystem::remove(entry.path(), ec);
            if (ec) throw std::runtime_error("Retention could not delete an expired file.");
        }
        ec.clear();
    }
}

}  // namespace

std::filesystem::path application_directory() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Cannot locate Point installation.");
    buffer.resize(length); return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path data_directory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path)))
        throw std::runtime_error("Cannot locate local user data folder.");
    const auto root = std::filesystem::path(path) / L"Point" / L"SecureData";
    CoTaskMemFree(path); return root;
}

Policy parse_policy_text(const std::string& text) {
    if (text.size() > 64 * 1024) throw std::runtime_error("Security policy is oversized.");
    Policy policy;
    std::istringstream input(text);
    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            throw std::runtime_error("Invalid point-security.conf entry");
        const auto key = trim_copy(line.substr(0, separator));
        const auto value = trim_copy(line.substr(separator + 1));
        if (!seen.insert(key).second) throw std::runtime_error("Duplicate security-policy setting.");
        if (key == "enforce_windows_groups")
            policy.enforce_windows_groups = security_rules::boolean(value);
        else if (key == "allowed_windows_groups")
            policy.allowed_groups = widen_utf8(value);
        else if (key == "export_windows_groups")
            policy.export_groups = widen_utf8(value);
        else if (key == "export_retention_days")
            policy.export_retention_days = security_rules::retention_days(value);
        else if (key == "workspace_retention_days")
            policy.workspace_retention_days = security_rules::retention_days(value);
        else if (key == "log_retention_days")
            policy.log_retention_days = security_rules::retention_days(value);
        else
            throw std::runtime_error("Unknown point-security.conf setting");
    }
    if (!input.eof()) throw std::runtime_error("Security-policy read failed.");
    if (seen.size() != 6) throw std::runtime_error("Security policy must explicitly specify all six settings.");
    if (!policy.enforce_windows_groups) throw std::runtime_error("This hardened build requires Windows-group enforcement.");
    if (policy.allowed_groups.empty())
        throw std::runtime_error("At least one allowed Windows group is required");
    if (policy.export_groups.empty())
        throw std::runtime_error("At least one export Windows group is required");
    for (int days : {
            policy.export_retention_days,
            policy.workspace_retention_days,
            policy.log_retention_days}) {
        if (days < 1 || days > 3650)
            throw std::runtime_error("Retention days must be from 1 to 3650");
    }
    return policy;
}

Policy load_policy(const std::filesystem::path& root) {
    const auto policy_path = root / "point-security.conf";
    const DWORD attributes = GetFileAttributesW(policy_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        std::filesystem::file_size(policy_path) > 64 * 1024)
        throw std::runtime_error("Security policy is missing, linked, or oversized.");
    // A writable policy would let an ordinary user grant themselves export access.
    // Require an administrator-controlled deployment, including its containing directory.
    auto require_admin_control = [](const std::filesystem::path& path) {
        const auto attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            throw std::runtime_error("Policy location must not be a reparse point.");
        PSID owner = nullptr; PACL dacl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                &owner, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot inspect security-policy permissions.");
        struct DescriptorGuard { PSECURITY_DESCRIPTOR value; ~DescriptorGuard() { LocalFree(value); } } guard{descriptor};
        auto trusted = [](PSID sid) {
            return sid && (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid));
        };
        const auto bytes = path.u8string();
        const std::string location(bytes.begin(), bytes.end());
        if (!trusted(owner))
            throw std::runtime_error("Installation owner is not Administrators or SYSTEM: " + location +
                ". Run the updated install_hardened.bat, then open the installed Point-Hardened copy. Elevating the EXE alone does not change ownership.");
        if (!dacl)
            throw std::runtime_error("Installation has an unrestricted ACL: " + location);
        GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
        constexpr DWORD writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
            FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* raw = nullptr;
            if (!GetAce(dacl, i, &raw)) throw std::runtime_error("Invalid policy access-control entry.");
            const auto* header = static_cast<ACE_HEADER*>(raw);
            if ((header->AceFlags & INHERIT_ONLY_ACE) != 0) continue;
            if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
                throw std::runtime_error("Unsupported policy ACL; use standard administrator/SYSTEM write permissions.");
            auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
            DWORD mask = ace->Mask; MapGenericMask(&mask, &mapping);
            if ((mask & writes) != 0 && !trusted(reinterpret_cast<PSID>(&ace->SidStart)))
                throw std::runtime_error("Unexpected write permission on: " + location + ". Run install_hardened.bat to repair the protected installation.");
        }
    };
    require_admin_control(root); require_admin_control(policy_path);
    std::ifstream input(policy_path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open security policy.");
    std::ostringstream text; text << input.rdbuf();
    if (input.bad()) throw std::runtime_error("Security-policy read failed.");
    return parse_policy_text(text.str());
}

void authorize_current_user(const Policy& policy) {
    if (!policy.enforce_windows_groups) throw std::runtime_error("Windows-group enforcement is required.");
    std::wstringstream groups(policy.allowed_groups);
    std::wstring group;
    while (std::getline(groups, group, L';')) {
        const auto first = group.find_first_not_of(L" \t");
        const auto last = group.find_last_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        group = group.substr(first, last - first + 1);
        if (current_token_is_member(group)) return;
    }
    throw std::runtime_error(
        "Access denied: the Windows user is not in an allowed Point group");
}

bool current_user_can_export(const Policy& policy) {
    if (!policy.enforce_windows_groups) return false;
    std::wstringstream groups(policy.export_groups);
    std::wstring group;
    while (std::getline(groups, group, L';')) {
        const auto first = group.find_first_not_of(L" \t");
        const auto last = group.find_last_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        group = group.substr(first, last - first + 1);
        if (current_token_is_member(group)) return true;
    }
    return false;
}

void harden_data_directories(const std::filesystem::path& root) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;OW)",
            SDDL_REVISION_1, &descriptor, nullptr)) {
        throw std::runtime_error("Unable to create the Point directory ACL");
    }
    for (const wchar_t* name : {
            L"Inbox", L"Workspace", L"Exports", L"Logs", L"Staging", L"BrowserFetcher", L"Fetcher"}) {
        const auto path = root / name;
        std::filesystem::create_directories(path);
        const auto attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            LocalFree(descriptor);
            throw std::runtime_error("Linked or inaccessible Point data directory rejected.");
        }
        if (!SetFileSecurityW(
                path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor)) {
            LocalFree(descriptor);
            throw std::runtime_error("Unable to protect Point data directories");
        }
    }
    LocalFree(descriptor);
}

void enforce_retention(
        const std::filesystem::path& root, const Policy& policy) {
    delete_expired_files(
        root / "Exports", policy.export_retention_days);
    delete_expired_files(
        root / "Workspace", policy.workspace_retention_days);
    delete_expired_files(
        root / "Logs", policy.log_retention_days);
}

void write_user_protected_file(const std::filesystem::path& path, const std::string& plain) {
    if (plain.size() > 64 * 1024 * 1024) throw std::runtime_error("Protected file exceeds safety limit");
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Point protected workspace", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output))
        throw std::runtime_error("Windows DPAPI could not protect the file");
    try { write_binary_atomic(path, output.pbData, output.cbData); }
    catch (...) { LocalFree(output.pbData); throw; }
    LocalFree(output.pbData);
}

std::string read_user_protected_file(const std::filesystem::path& path) {
    auto protected_bytes = read_binary(path);
    if (protected_bytes.size() >= 11 &&
        std::equal(
            protected_bytes.begin(), protected_bytes.begin() + 11,
            reinterpret_cast<const unsigned char*>("POINT_VIEW_"))) {
        throw std::runtime_error(
            "Unencrypted legacy view rejected; save it again with Point v8");
    }
    DATA_BLOB input{
        static_cast<DWORD>(protected_bytes.size()),
        protected_bytes.data()};
    DATA_BLOB output{};
    if (!CryptUnprotectData(
            &input, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        throw std::runtime_error(
            "Workspace cannot be decrypted by the current Windows user");
    }
    std::string result(
        reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

}  // namespace point::compliance
