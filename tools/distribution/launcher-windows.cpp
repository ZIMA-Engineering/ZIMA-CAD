// Native portable launcher. No Qt, Python, shell or installed VC runtime.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;
std::wstring setting(const fs::path& file, const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[1024]{};
    const auto size = GetPrivateProfileStringW(section, key, L"", buffer, 1024, file.c_str());
    if (size >= 1023) throw std::runtime_error("Launcher setting is too long");
    return buffer;
}
bool safe_version(const std::wstring& value, bool custom) {
    if (value.empty() || value.size() > 64 || value.back() == L'.') return false;
    if (!custom) {
        if (value.size() != 10 || value.substr(8) == L"00") return false;
        for (auto c : value) if (c < L'0' || c > L'9') return false;
    }
    for (auto c : value) if (!(c >= L'0' && c <= L'9') && !(c >= L'A' && c <= L'Z') &&
        !(c >= L'a' && c <= L'z') && c != L'-' && c != L'_' && c != L'.') return false;
    return value != L"." && value != L"..";
}
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(2 * slashes, L'\\'); result += L'"'; return result;
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool interactive_error = true;
    try {
        std::wstring module(32768, L'\0');
        const auto count = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!count || count == module.size()) throw std::runtime_error("Cannot locate installation");
        module.resize(count); const auto root = fs::path(module).parent_path();
        int argc = 0; auto raw = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!raw) throw std::runtime_error("Cannot read arguments");
        std::vector<std::wstring> args(raw, raw + argc); LocalFree(raw);
        std::wstring version; bool custom = false, explicit_custom = false, check = false, cli = false;
        std::vector<std::wstring> forwarded;
        for (int i = 1; i < argc; ++i) {
            if (args[i] == L"-Version") {
                if (++i == argc || !version.empty()) throw std::runtime_error("Use -Version BUILD_ID once");
                version = args[i];
            } else if (args[i] == L"-Custom") { custom = true; explicit_custom = true; }
            else if (args[i] == L"-Check") { check = true; interactive_error = false; }
            else if (args[i] == L"-CLI") { cli = true; interactive_error = false; }
            else if (args[i] == L"--") { forwarded.insert(forwarded.end(), args.begin() + i + 1, args.end()); break; }
            else forwarded.push_back(args[i]);
        }
        if (version.empty()) {
            version = setting(root / "launcher.ini", L"launcher", L"windows");
            if (!explicit_custom) custom = setting(root / "launcher.ini", L"launcher", L"windows_custom") == L"true";
        }
        if (!safe_version(version, custom)) throw std::runtime_error("Invalid or missing selected version");
        const auto base = root / (custom ? L"custom/windows" : L"windows");
        const auto runtime = base / version;
        if (fs::weakly_canonical(runtime).parent_path() != fs::weakly_canonical(base))
            throw std::runtime_error("Version escapes installation");
        const auto manifest = runtime / "build.ini";
        if (setting(manifest, L"build", L"product") != L"ZIMA-CAD" ||
            setting(manifest, L"build", L"version") != version ||
            setting(manifest, L"build", L"platform") != L"windows-x64")
            throw std::runtime_error("Version manifest mismatch");
        const auto executable = runtime / (cli ? L"zima-cad-cli.exe" : L"zima-cad-cpp.exe");
        if (!fs::is_regular_file(executable)) throw std::runtime_error("Selected executable is missing");
        if (check) {
            const auto text = executable.u8string(); DWORD written;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            return 0;
        }
        for (const auto* directory : {L"config/windows", L"config/linux", L"config/templates", L"config/materials",
            L"config/formats", L"config/localization", L"Projects", L"autosave", L"recovery", L"cache", L".updates"})
            fs::create_directories(root / directory);
        const auto config = root / L"config/config.ini";
        const auto handle = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            const char text[] = "; Shared user overrides. Factory defaults belong to each version.\n"; DWORD written;
            WriteFile(handle, text, sizeof(text)-1, &written, nullptr); CloseHandle(handle);
        } else if (GetLastError() != ERROR_FILE_EXISTS) throw std::runtime_error("Cannot prepare user configuration");
        for (const auto* name : {L"QT_PLUGIN_PATH", L"QT_QPA_PLATFORM_PLUGIN_PATH", L"QT_QPA_PLATFORM",
            L"QT_STYLE_OVERRIDE", L"QML2_IMPORT_PATH", L"QML_IMPORT_PATH"}) SetEnvironmentVariableW(name, nullptr);
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH);
        const auto search = runtime.wstring() + L";" + windows + L"\\System32;" + windows;
        SetEnvironmentVariableW(L"PATH", search.c_str());
        SetEnvironmentVariableW(L"CSF_ShadersDirectory", (runtime / L"resources/occt/Shaders").c_str());
        SetEnvironmentVariableW(L"CSF_XSMessage", (runtime / L"resources/occt/XSMessage").c_str());
        SetEnvironmentVariableW(L"CSF_SHMessage", (runtime / L"resources/occt/SHMessage").c_str());
        SetEnvironmentVariableW(L"CSF_XSTEPDefaults", (runtime / L"resources/occt/XSTEPResource").c_str());
        std::wstring command = quote(executable.wstring());
        for (const auto& arg : forwarded) command += L" " + quote(arg);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
            nullptr, root.c_str(), &startup, &process)) throw std::runtime_error("Cannot start selected version");
        CloseHandle(process.hThread);
        // Keep a CLI invocation synchronous for automation and its exit code.
        DWORD status = 0;
        if (cli) { WaitForSingleObject(process.hProcess, INFINITE); GetExitCodeProcess(process.hProcess, &status); }
        CloseHandle(process.hProcess); return static_cast<int>(status);
    } catch (const std::exception& error) {
        if (interactive_error) MessageBoxA(nullptr, error.what(), "ZIMA-CAD launcher", MB_OK | MB_ICONERROR);
        else { DWORD written; const std::string message = std::string(error.what()) + "\n";
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr); }
        return 2;
    }
}
