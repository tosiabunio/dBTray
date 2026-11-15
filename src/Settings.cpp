#include "Settings.h"

#include <windows.h>
#include <shlobj.h>

namespace {

constexpr wchar_t kSection[] = L"dBTray";
constexpr wchar_t kShowsLoudnessKey[] = L"ShowLoudness";
constexpr wchar_t kMeasuresInBackgroundKey[] = L"KeepMeasuringWhenClosed";
constexpr wchar_t kFileName[] = L"dBTray.ini";

std::wstring ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

std::wstring RoamingDirectory() {
    std::wstring directory;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        directory = std::wstring(roaming) + L"\\dBTray\\";
    }
    CoTaskMemFree(roaming);
    return directory;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

void Settings::Load() {
    const std::wstring portablePath = ExecutableDirectory() + kFileName;
    const std::wstring roaming = RoamingDirectory();
    fallbackPath_ = roaming.empty() ? std::wstring() : roaming + kFileName;
    path_ = FileExists(portablePath) || !FileExists(fallbackPath_) ? portablePath : fallbackPath_;

    showsLoudness_ = GetPrivateProfileIntW(kSection, kShowsLoudnessKey, 0, path_.c_str()) != 0;
    measuresInBackground_ =
        GetPrivateProfileIntW(kSection, kMeasuresInBackgroundKey, 0, path_.c_str()) != 0;
}

void Settings::SetShowsLoudness(bool value) {
    showsLoudness_ = value;
    Save();
}

void Settings::SetMeasuresInBackground(bool value) {
    measuresInBackground_ = value;
    Save();
}

void Settings::Save() {
    if (Write() || fallbackPath_.empty() || path_ == fallbackPath_) return;
    // The executable's folder isn't writable: keep the settings in %APPDATA% from now on.
    path_ = fallbackPath_;
    CreateDirectoryW(RoamingDirectory().c_str(), nullptr);
    Write();
}

bool Settings::Write() const {
    return WritePrivateProfileStringW(kSection, kShowsLoudnessKey, showsLoudness_ ? L"1" : L"0",
                                      path_.c_str()) &&
           WritePrivateProfileStringW(kSection, kMeasuresInBackgroundKey,
                                      measuresInBackground_ ? L"1" : L"0", path_.c_str());
}
