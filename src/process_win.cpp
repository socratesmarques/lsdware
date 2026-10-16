#include "lsd/process_win.hpp"
#include <cmath>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <tlhelp32.h>
namespace lsd {
namespace {
class Handle {
  public:
    explicit Handle(HANDLE h) : h_(h) {
        if (h_ == INVALID_HANDLE_VALUE || h_ == nullptr)
            throw std::runtime_error("Windows handle error " + std::to_string(GetLastError()));
    }
    ~Handle() {
        CloseHandle(h_);
    }
    operator HANDLE() const {
        return h_;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

  private:
    HANDLE h_;
};
} // namespace
Process::Process(bool allow_write) : allow_write_(allow_write) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
        do {
            if (_wcsicmp(entry.szExeFile, L"cs2.exe") == 0) {
                pid_ = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    if (!pid_)
        throw std::runtime_error("cs2.exe not running");
    DWORD rights = PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    if (allow_write_)
        rights |= PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
    handle_ = OpenProcess(rights, FALSE, pid_);
    if (!handle_)
        throw std::runtime_error("OpenProcess failed, Windows error " + std::to_string(GetLastError()));
}
Process::~Process() {
    if (handle_)
        CloseHandle(handle_);
}
bool Process::alive() const {
    return WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT;
}
std::string Process::steam_build_id() const {
    std::wstring image(32768, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(handle_, 0, image.data(), &size))
        return {};
    image.resize(size);
    auto path = std::filesystem::path(image).parent_path();
    for (int i = 0; i < 10 && !path.empty(); ++i) {
        const auto manifest = path / L"appmanifest_730.acf";
        std::ifstream f(manifest);
        if (f) {
            std::string contents((std::istreambuf_iterator<char>(f)), {});
            std::smatch match;
            const std::regex pattern("\"buildid\"\\s*\"([0-9]+)\"");
            if (std::regex_search(contents, match, pattern))
                return match[1].str();
        }
        if (path == path.parent_path())
            break;
        path = path.parent_path();
    }
    return {};
}
bool Process::read_bytes(Address address, void* out, std::size_t bytes) const {
    if (address < 0x10000 || address >= 0x0000800000000000ULL || bytes > 0x0000800000000000ULL - address)
        return false;
    SIZE_T read = 0;
    return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), out, bytes, &read) && read == bytes;
}
bool Process::write_angles(Address address, Angles angles) const {
    if (!allow_write_) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    if (!alive()) {
        SetLastError(ERROR_PROCESS_ABORTED);
        return false;
    }
    if (!std::isfinite(angles.pitch) || !std::isfinite(angles.yaw) || angles.pitch < -89 ||
        angles.pitch > 89 || angles.yaw < -180 || angles.yaw > 180) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    angles.roll = 0;
    SIZE_T written = 0;
    if (!WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(address), &angles, sizeof(angles), &written))
        return false;
    if (written != sizeof(angles)) {
        SetLastError(ERROR_PARTIAL_COPY);
        return false;
    }
    return true;
}
Module Process::module(const wchar_t* name) const {
    // ERROR_BAD_LENGTH is transient while the loader is updating its module list.
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
        Sleep(10);
    }
    Handle snapshot(raw);
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
        do {
            if (_wcsicmp(entry.szModule, name) == 0)
                return {reinterpret_cast<Address>(entry.modBaseAddr), entry.modBaseSize};
        } while (Module32NextW(snapshot, &entry));
    throw std::runtime_error("Required CS2 module not loaded");
}
HWND Process::window() const {
    struct Search {
        DWORD pid;
        HWND window;
    };
    Search search{pid_, nullptr};
    EnumWindows(
        [](HWND w, LPARAM param) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == s.pid && IsWindowVisible(w) && GetWindow(w, GW_OWNER) == nullptr) {
                RECT r{};
                GetClientRect(w, &r);
                if (r.right > 0 && r.bottom > 0) {
                    s.window = w;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}
} // namespace lsd
