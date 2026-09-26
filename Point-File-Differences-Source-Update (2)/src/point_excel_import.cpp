#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "point_excel_import.h"
#include "point_compliance.h"
#include "point_file_validation.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>

namespace point {
namespace {
bool excel_extension(const std::filesystem::path& path) {
    if (path.filename().wstring().rfind(L"~$", 0) == 0) return false;
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) {
                       return static_cast<wchar_t>(std::towlower(value));
                   });
    return extension == L".xlsx";
}

bool csv_extension(const std::filesystem::path& path) {
    if (path.filename().wstring().rfind(L"~$", 0) == 0) return false;
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) {
                       return static_cast<wchar_t>(std::towlower(value));
                   });
    return extension == L".csv";
}

bool csv_has_header(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string line;
    std::getline(input, line);
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
    return std::any_of(line.begin(), line.end(), [](unsigned char ch) {
        return ch > 32 && ch != ',' && ch != '"';
    });
}

std::wstring safe_component(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' ||
            ch == L'"' || ch == L'/' || ch == L'\\' || ch == L'|' ||
            ch == L'?' || ch == L'*')
            ch = L'_';
    }
    while (!value.empty() &&
           (value.back() == L' ' || value.back() == L'.'))
        value.pop_back();
    if (value.empty()) value = L"Sheet";
    if (value.size() > 80) value.resize(80);
    return value;
}

bool native_xlsx_extension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".xlsx";
}

std::wstring command_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(ch);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

bool run_native_xlsx_reader(const std::filesystem::path& script,
                            const std::filesystem::path& workbook,
                            const std::filesystem::path& cache,
                            std::size_t ordinal,
                            const std::wstring& stem,
                            const std::function<bool()>& cancelled) {
    std::error_code script_error;
    if (!std::filesystem::is_regular_file(script, script_error) ||
        script_error)
        return false;
    const DWORD attributes = GetFileAttributesW(script.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;
    wchar_t system_directory[MAX_PATH]{};
    if (!GetSystemDirectoryW(system_directory, MAX_PATH)) return false;
    const auto powershell = std::filesystem::path(system_directory) /
        L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    std::wstring command = command_argument(powershell.wstring()) +
        L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy AllSigned -File " +
        command_argument(script.wstring()) + L" -InputPath " + command_argument(workbook.wstring()) +
        L" -OutputDirectory " + command_argument(cache.wstring()) +
        L" -Ordinal " + std::to_wstring(ordinal) + L" -SafeStem " + command_argument(stem);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job); return false;
    }
    if (!CreateProcessW(powershell.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, script.parent_path().c_str(), &startup, &process)) {
        CloseHandle(job); return false;
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); return false;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        CloseHandle(job); CloseHandle(process.hThread); CloseHandle(process.hProcess); return false;
    }
    const auto started = GetTickCount64();
    bool aborted = false;
    for (;;) {
        const auto wait = WaitForSingleObject(process.hProcess, 100);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED || (cancelled && cancelled()) || GetTickCount64() - started > 300000ULL) {
            aborted = true; break;
        }
    }
    DWORD exit_code = 1;
    if (!aborted) GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(job); // Also terminates the child process tree on cancellation/timeout.
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (aborted) throw std::runtime_error("Excel helper cancelled, timed out, or became unavailable.");
    return exit_code == 0;

}

std::vector<std::filesystem::path> generated_sheets(
        const std::filesystem::path& cache, std::size_t ordinal,
        const std::wstring& stem) {
    const std::wstring prefix = std::to_wstring(ordinal) + L"__" + stem + L"__";
    std::vector<std::filesystem::path> output;
    for (const auto& entry : std::filesystem::directory_iterator(cache)) {
        std::error_code error;
        if (entry.is_regular_file(error) && !error && csv_extension(entry.path()) &&
            csv_has_header(entry.path()) &&
            entry.path().filename().wstring().rfind(prefix, 0) == 0)
            output.push_back(entry.path());
    }
    std::sort(output.begin(), output.end());
    return output;
}


}  // namespace

ExcelImportResult prepare_import_sources(
        const std::filesystem::path& inbox,
        const std::filesystem::path& cache,
        const std::function<void(
            std::size_t, std::size_t,
            const std::filesystem::path&, bool)>& progress,
        const std::function<bool()>& cancelled) {
    ExcelImportResult result;
    std::filesystem::create_directories(cache);
    for (const auto& entry : std::filesystem::directory_iterator(cache)) {
        std::error_code cleanup_error;
        const auto name = entry.path().filename().wstring();
        if (entry.is_regular_file(cleanup_error) && !cleanup_error &&
            name.rfind(L".point-import-", 0) == 0)
            std::filesystem::remove(entry.path(), cleanup_error);
    }

    std::vector<std::filesystem::path> workbooks_to_import;
    for (const auto& entry :
         std::filesystem::directory_iterator(inbox)) {
        std::error_code error;
        if (!entry.is_regular_file(error) || error ||
            entry.is_symlink(error) || error)
            continue;
        auto extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension == L".xls" || extension == L".xlsm" || extension == L".xlsb") {
            result.issues.push_back(entry.path().filename().string() + ": legacy/macro-capable format blocked; export CSV or XLSX.");
            continue;
        }
        if (csv_extension(entry.path()))
            result.csv_sources.push_back(entry.path());
        else if (excel_extension(entry.path())) {
            const DWORD attributes = GetFileAttributesW(entry.path().c_str());
            std::wstring reason;
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                !point_validation::validate(entry.path(), entry.path().filename(), reason)) {
                result.issues.push_back(entry.path().filename().string() + ": workbook rejected by format/safety validation");
                continue;
            }
            workbooks_to_import.push_back(entry.path());
        }
    }
    if (workbooks_to_import.empty()) return result;

    std::sort(workbooks_to_import.begin(), workbooks_to_import.end());
    std::ostringstream signature;
    // Changing this token invalidates worksheet caches produced by older
    // import engines, including the former Protected View empty-cache bug.
    signature << "PointExcelCache=11\n";
    for (const auto& workbook : workbooks_to_import) {
        std::error_code error;
        const auto size = std::filesystem::file_size(workbook, error);
        if (error) continue;
        const auto modified =
            std::filesystem::last_write_time(workbook, error);
        if (error) continue;
        signature << workbook.filename().string() << '\t' << size << '\t'
                  << modified.time_since_epoch().count() << '\n';
    }
    const auto manifest = cache / "point-excel-cache.manifest";
    std::ifstream existing_manifest(manifest, std::ios::binary);
    const std::string existing_signature{
        std::istreambuf_iterator<char>(existing_manifest),
        std::istreambuf_iterator<char>()};
    if (!existing_signature.empty() &&
        existing_signature == signature.str()) {
        const auto original_csv_count = result.csv_sources.size();
        for (const auto& entry :
             std::filesystem::directory_iterator(cache)) {
            std::error_code error;
            if (entry.is_regular_file(error) && !error &&
                csv_extension(entry.path()) && csv_has_header(entry.path()))
                result.csv_sources.push_back(entry.path());
        }
        std::sort(result.csv_sources.begin(), result.csv_sources.end());
        if (result.csv_sources.size() > original_csv_count) {
            result.workbook_count = workbooks_to_import.size();
            result.worksheet_count = result.csv_sources.size() - original_csv_count;
            result.cache_reused = true;
            if (progress) {
                for (std::size_t index = 0;
                     index < workbooks_to_import.size(); ++index)
                    progress(index + 1, workbooks_to_import.size(),
                             workbooks_to_import[index], true);
            }
            return result;
        }
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(cache)) {
        std::error_code error;
        if (entry.is_regular_file(error) &&
            csv_extension(entry.path()))
            std::filesystem::remove(entry.path(), error);
    }

    // Standard Office workbooks are parsed without launching Excel. This is
    // both faster and immune to Protected View withholding automation data.
    std::set<std::size_t> native_imports;
    const auto native_script = compliance::application_directory() / L"scripts" /
                               L"point_xlsx_to_csv.ps1";
    for (std::size_t index = 0; index < workbooks_to_import.size(); ++index) {
        const auto& workbook_path = workbooks_to_import[index];
        if (!native_xlsx_extension(workbook_path)) continue;
        if (cancelled && cancelled()) {
            result.issues.push_back("Refresh cancelled");
            return result;
        }
        if (progress)
            progress(index + 1, workbooks_to_import.size(), workbook_path, false);
        const auto stem = safe_component(workbook_path.stem().wstring());
        if (!run_native_xlsx_reader(native_script, workbook_path, cache,
                                    index + 1, stem, cancelled))
            continue;
        auto sheets = generated_sheets(cache, index + 1, stem);
        if (sheets.empty()) continue;
        native_imports.insert(index + 1);
        ++result.workbook_count;
        result.worksheet_count += sheets.size();
        result.csv_sources.insert(result.csv_sources.end(),
                                  sheets.begin(), sheets.end());
    }
    if (native_imports.size() == workbooks_to_import.size()) {
        std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
        output << signature.str();
        return result;
    }

    for (std::size_t index = 0; index < workbooks_to_import.size(); ++index) {
        if (!native_imports.contains(index + 1))
            result.issues.push_back(workbooks_to_import[index].filename().string() +
                ": signed XLSX reader missing, blocked, or failed; Excel automation fallback is disabled.");
    }
    return result;
}

}  // namespace point
