// CsvExport.h - Exports scan results to a UTF-8 CSV file
#pragma once
#include "pch.h"
#include "Scanner.h"

class CsvExporter {
public:
    static bool Export(const std::wstring& path,
                       const std::vector<std::shared_ptr<ScanResult>>& results)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;

        // UTF-8 BOM (helps Excel)
        const uint8_t bom[] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, 1, 3, f);

        auto writeRow = [&](const std::vector<std::wstring>& cols) {
            for (size_t i = 0; i < cols.size(); i++) {
                if (i) fputc(',', f);
                WriteCell(f, cols[i]);
            }
            fputc('\r', f);
            fputc('\n', f);
        };

        // Header
        writeRow({
            L"Filename", L"Folder", L"Title", L"Artist", L"Album Artist",
            L"Album", L"Date", L"Track", L"Total Tracks", L"Disc",
            L"Genre", L"Label", L"ISRC", L"Country", L"Status", L"Type",
            L"Duration", L"Sample Rate", L"Bit Depth", L"Channels", L"File Size",
            L"AcoustID Score (%)",
            L"MB Recording ID", L"MB Release ID", L"MB Artist ID",
            L"MB Release Group ID", L"Release Count",
        });

        for (const auto& r : results) {
            if (r->status != ScanStatus::Done) continue;

            const auto& t  = r->tags;
            const auto& mb = r->mb;

            // Prefer MB data, fall back to existing tags
            auto pick = [](const std::wstring& mbVal, const std::wstring& tagVal) {
                return !mbVal.empty() ? mbVal : tagVal;
            };

            std::wstring fname = r->filePath;
            if (auto sl = fname.rfind(L'\\'); sl != std::wstring::npos)
                fname = fname.substr(sl + 1);

            writeRow({
                fname,
                r->folderPath,
                pick(mb.title,       t.title),
                pick(mb.artist,      t.artist),
                pick(mb.albumArtist, t.albumArtist),
                pick(mb.album,       t.album),
                pick(mb.date,        t.date),
                pick(mb.trackNumber, t.trackNumber),
                pick(mb.totalTracks, t.totalTracks),
                pick(mb.discNumber,  t.discNumber),
                pick(mb.genre,       t.genre),
                pick(mb.label,       t.label),
                pick(mb.isrc,        t.isrc),
                mb.country,
                mb.status,
                mb.primaryType,
                t.DurationFormatted(),
                t.sampleRate    > 0 ? std::to_wstring(t.sampleRate)    + L" Hz"  : L"",
                t.bitsPerSample > 0 ? std::to_wstring(t.bitsPerSample) + L"-bit" : L"",
                t.channels      > 0 ? std::to_wstring(t.channels)                : L"",
                t.FileSizeFormatted(),
                mb.acoustidScore > 0
                    ? (std::to_wstring((int)mb.acoustidScore) +
                       (mb.acoustidScore != (int)mb.acoustidScore
                           ? L"." + std::to_wstring((int)(mb.acoustidScore * 10) % 10)
                           : L""))
                    : L"",
                mb.mbRecordingId,
                mb.mbReleaseId,
                mb.mbArtistId,
                mb.releaseGroupId,
                mb.releaseCount > 0 ? std::to_wstring(mb.releaseCount) : L"",
            });
        }

        fclose(f);
        return true;
    }

private:
    static std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    static void WriteCell(FILE* f, const std::wstring& val) {
        std::string s = WideToUtf8(val);
        // Quote if contains comma, quote, or newline
        bool needsQuote = (s.find_first_of(",\"\r\n") != std::string::npos);
        if (needsQuote) {
            fputc('"', f);
            for (char c : s) {
                if (c == '"') fputc('"', f); // double-quote escape
                fputc(c, f);
            }
            fputc('"', f);
        } else {
            fwrite(s.c_str(), 1, s.size(), f);
        }
    }
};
