// MusicBrainzApi.h - AcoustID fingerprinting + MusicBrainz metadata lookup
#pragma once
#include "pch.h"
#include "HttpClient.h"
#include "Json.h"

// ── Result structs ────────────────────────────────────────────────────────────

struct FingerprintResult {
    bool         ok          = false;
    std::wstring error;
    std::wstring recordingId;
    double       score       = 0.0;
};

struct MusicBrainzData {
    bool         ok              = false;
    std::wstring error;

    std::wstring title;
    std::wstring artist;
    std::wstring albumArtist;
    std::wstring album;
    std::wstring date;
    std::wstring trackNumber;
    std::wstring totalTracks;
    std::wstring discNumber;
    std::wstring genre;
    std::wstring label;
    std::wstring isrc;
    std::wstring country;
    std::wstring status;
    std::wstring primaryType;
    std::wstring mbRecordingId;
    std::wstring mbReleaseId;
    std::wstring mbArtistId;
    std::wstring releaseGroupId;
    int          releaseCount    = 0;
    double       acoustidScore   = 0.0;
};

// ── fpcalc runner ─────────────────────────────────────────────────────────────

struct FpcalcResult {
    bool        ok           = false;
    std::string fingerprint;
    int         durationSec  = 0;
};

static FpcalcResult RunFpcalc(const std::wstring& audioPath) {
    FpcalcResult r;

    // Locate fpcalc.exe next to our own exe, or fall back to PATH
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Remove filename to get directory - use simple string manipulation
    std::wstring fpcalcPath = exePath;
    size_t lastSlash = fpcalcPath.rfind(L'\\');
    if (lastSlash != std::wstring::npos)
        fpcalcPath = fpcalcPath.substr(0, lastSlash + 1) + L"fpcalc.exe";
    else
        fpcalcPath = L"fpcalc.exe";

    // Fall back to just "fpcalc.exe" on PATH if not found next to exe
    if (GetFileAttributesW(fpcalcPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        fpcalcPath = L"fpcalc.exe";

    std::wstring cmd = L"\"" + fpcalcPath + L"\" -plain \"" + audioPath + L"\"";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return r;

    STARTUPINFOW si = {};
    si.cb         = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdMut = cmd;
    BOOL created = CreateProcessW(
        nullptr, cmdMut.data(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr, &si, &pi);

    CloseHandle(hWritePipe); // must close write end before reading

    if (!created) {
        CloseHandle(hReadPipe);
        return r;
    }

    std::string output;
    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
        output.append(buf, bytesRead);

    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    // Parse output: "DURATION=N" and "FINGERPRINT=xxx"
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        // Trim CR
        while (!line.empty() && (line.back()=='\r'||line.back()=='\n')) line.pop_back();
        if (line.rfind("DURATION=", 0) == 0)
            r.durationSec = std::stoi(line.substr(9));
        else if (line.rfind("FINGERPRINT=", 0) == 0)
            r.fingerprint = line.substr(12);
    }
    r.ok = !r.fingerprint.empty();
    return r;
}

// ── AcoustID lookup ───────────────────────────────────────────────────────────

class AcoustIdClient {
    std::wstring apiKey_;
public:
    explicit AcoustIdClient(std::wstring key) : apiKey_(std::move(key)) {}

    FingerprintResult Lookup(const std::wstring& audioPath) {
        FingerprintResult r;

        auto fpcalc = RunFpcalc(audioPath);
        if (!fpcalc.ok) {
            r.error = L"fpcalc.exe not found or failed. "
                      L"Download Chromaprint from acoustid.org/chromaprint "
                      L"and place fpcalc.exe next to MBTagger.exe";
            return r;
        }

        std::wstring url =
            L"https://api.acoustid.org/v2/lookup"
            L"?client=" + apiKey_ +
            L"&meta=recordings+releases+releasegroups"
            L"&duration=" + std::to_wstring(fpcalc.durationSec) +
            L"&fingerprint=" + HttpClient::UrlEncode(
                std::wstring(fpcalc.fingerprint.begin(), fpcalc.fingerprint.end()));

        auto resp = HttpClient::Get(url);
        if (!resp.ok()) {
            r.error = L"AcoustID HTTP error " + std::to_wstring(resp.statusCode);
            return r;
        }

        auto j = JsonParser::Parse(resp.body);
        if (j["status"].str != "ok") {
            r.error = L"AcoustID error: " + j["error"]["message"].WStr();
            return r;
        }

        const auto& results = j["results"];
        if (results.IsArray() && results.Size() > 0) {
            const auto& best = results[static_cast<size_t>(0)];
            r.score = best["score"].num;
            const auto& recs = best["recordings"];
            if (recs.IsArray() && recs.Size() > 0)
                r.recordingId = recs[static_cast<size_t>(0)]["id"].WStr();
        }

        if (r.recordingId.empty()) {
            r.error = L"No match found";
            return r;
        }
        r.ok = true;
        return r;
    }
};

// ── MusicBrainz lookup ────────────────────────────────────────────────────────

class MusicBrainzClient {
    static constexpr const wchar_t* BASE = L"https://musicbrainz.org/ws/2/";

public:
    MusicBrainzData LookupRecording(const std::wstring& recordingId,
                                    double acoustidScore = 0.0)
    {
        MusicBrainzData d;
        d.acoustidScore = acoustidScore;

        std::wstring url = std::wstring(BASE) +
            L"recording/" + recordingId +
            L"?fmt=json&inc=artists+releases+release-groups+isrcs+tags";

        // MusicBrainz rate limit: 1 request per second
        static std::mutex mbMutex;
        static std::chrono::steady_clock::time_point lastCall =
            std::chrono::steady_clock::now() - std::chrono::seconds(2);
        {
            std::lock_guard<std::mutex> lock(mbMutex);
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastCall).count();
            if (elapsed < 1100)
                Sleep(static_cast<DWORD>(1100 - elapsed));
            lastCall = std::chrono::steady_clock::now();
        }

        auto resp = HttpClient::Get(url, {
            L"User-Agent: MBTagger/2.0 (https://github.com/user/mb-tagger)"
        });

        if (!resp.ok()) {
            d.error = L"MusicBrainz HTTP " + std::to_wstring(resp.statusCode);
            return d;
        }

        auto j = JsonParser::Parse(resp.body);

        d.mbRecordingId = j["id"].WStr();
        d.title         = j["title"].WStr();

        // ISRC
        const auto& isrcs = j["isrcs"];
        if (isrcs.IsArray() && isrcs.Size() > 0)
            d.isrc = isrcs[static_cast<size_t>(0)].WStr();

        // Artist credits
        const auto& credits = j["artist-credit"];
        if (credits.IsArray()) {
            std::wstring artistStr, artistIds;
            for (size_t i = 0; i < credits.Size(); i++) {
                const auto& c = credits[i];
                const auto& a = c["artist"];
                if (!a.IsNull()) {
                    if (!artistStr.empty()) artistStr += L" / ";
                    artistStr  += a["name"].WStr();
                    if (!artistIds.empty()) artistIds += L", ";
                    artistIds  += a["id"].WStr();
                }
            }
            d.artist     = artistStr;
            d.mbArtistId = artistIds;
        }

        // Releases
        const auto& rels = j["releases"];
        d.releaseCount = rels.IsArray() ? static_cast<int>(rels.Size()) : 0;

        if (d.releaseCount > 0) {
            // Prefer first "Official" release
            size_t bestIdx = 0;
            for (size_t i = 0; i < rels.Size(); i++) {
                if (rels[i]["status"].str == "Official") { bestIdx = i; break; }
            }
            const auto& rel = rels[bestIdx];

            d.mbReleaseId = rel["id"].WStr();
            d.album       = rel["title"].WStr();
            d.date        = rel["date"].WStr();
            d.country     = rel["country"].WStr();
            d.status      = rel["status"].WStr();

            // Release group
            const auto& rg = rel["release-group"];
            d.releaseGroupId = rg["id"].WStr();
            d.primaryType    = rg["primary-type"].WStr();

            // Track number
            const auto& media = rel["media"];
            if (media.IsArray()) {
                for (size_t mi = 0; mi < media.Size(); mi++) {
                    const auto& medium = media[mi];
                    const auto& tracks = medium["tracks"];
                    if (!tracks.IsArray()) continue;
                    for (size_t ti = 0; ti < tracks.Size(); ti++) {
                        const auto& track = tracks[ti];
                        if (track["recording"]["id"].WStr() == recordingId) {
                            d.trackNumber = track["number"].WStr();
                            d.totalTracks = std::to_wstring(medium["track-count"].Int());
                            d.discNumber  = std::to_wstring(medium["position"].Int());
                        }
                    }
                }
            }
        }

        d.ok = true;
        return d;
    }
};
