#pragma once

#include <string>

// The meter's options. They live in dBTray.ini next to the executable so the app stays portable;
// if that folder isn't writable (Program Files, say) they go to %APPDATA%\dBTray instead.
class Settings {
public:
    void Load();

    bool ShowsLoudness() const { return showsLoudness_; }
    void SetShowsLoudness(bool value);
    bool MeasuresInBackground() const { return measuresInBackground_; }
    void SetMeasuresInBackground(bool value);

    // Capture keeps running after the meter closes, so integrated loudness keeps accumulating.
    // Only applies while loudness is shown.
    bool KeepsMeasuringWhenClosed() const { return showsLoudness_ && measuresInBackground_; }

private:
    void Save();
    bool Write() const;

    std::wstring path_;
    std::wstring fallbackPath_;
    bool showsLoudness_ = false;
    bool measuresInBackground_ = false;
};
