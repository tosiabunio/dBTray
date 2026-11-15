#include "LaunchAtLogin.h"

#include <windows.h>
#include <shellapi.h>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"dBTray";

std::wstring ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// The program part of a Run entry such as `"C:\Tools\dBTray.exe" --startup`.
std::wstring ProgramOf(const std::wstring& command) {
    if (!command.empty() && command.front() == L'"') {
        const size_t end = command.find(L'"', 1);
        return command.substr(1, end == std::wstring::npos ? end : end - 1);
    }
    return command.substr(0, command.find(L' '));
}

std::wstring SystemMessage(LSTATUS status) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, DWORD(status), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring message(text ? text : L"", length);
    LocalFree(text);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message.empty() ? L"Couldn't change the startup entry." : message;
}

}  // namespace

void LaunchAtLogin::Refresh() {
    registeredHere_ = registeredElsewhere_ = disabledInSettings_ = false;

    wchar_t command[2048];
    DWORD size = sizeof(command);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, command,
                     &size) != ERROR_SUCCESS) {
        return;
    }
    registeredHere_ = CompareStringOrdinal(ProgramOf(command).c_str(), -1,
                                           ExecutablePath().c_str(), -1, TRUE) == CSTR_EQUAL;
    registeredElsewhere_ = !registeredHere_;

    // Settings › Apps › Startup records its switch here; an odd first byte means "off".
    BYTE approved[16];
    size = sizeof(approved);
    if (RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, kValueName, RRF_RT_REG_BINARY, nullptr,
                     approved, &size) == ERROR_SUCCESS && size >= 1) {
        disabledInSettings_ = (approved[0] & 1) != 0;
    }
}

void LaunchAtLogin::SetEnabled(bool enabled) {
    LSTATUS status;
    if (enabled) {
        const std::wstring command = L"\"" + ExecutablePath() + L"\" " + kStartupSwitch;
        status = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                                 DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    errorMessage_ = status == ERROR_SUCCESS ? std::wstring() : SystemMessage(status);
    Refresh();
}

void LaunchAtLogin::OpenSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL);
}
