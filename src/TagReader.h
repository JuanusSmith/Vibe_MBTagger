// TagReader.h - Reads metadata tags from FLAC, MP3, OGG, M4A files
// Uses pure Win32 / standard C++ with no external tag libraries.
#pragma once
#include "pch.h"

struct AudioTags {
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
    std::wstring comment;
    std::wstring mbRecordingId;
    std::wstring mbReleaseId;
    std::wstring mbArtistId;

    // Technical
    double  durationSec  = 0.0;
    int     sampleRate   = 0;
    int     bitsPerSample= 0;
    int     channels     = 0;
    int64_t fileSizeBytes= 0;

    // Derived
    std::wstring DurationFormatted() const {
        if (durationSec <= 0.0) return L"";
        int total = static_cast<int>(durationSec);
        return std::to_wstring(total / 60) + L":" +
               (total % 60 < 10 ? L"0" : L"") + std::to_wstring(total % 60);
    }
    std::wstring FileSizeFormatted() const {
        if (fileSizeBytes < 1024)        return std::to_wstring(fileSizeBytes) + L" B";
        if (fileSizeBytes < 1048576)     return std::to_wstring(fileSizeBytes / 1024) + L" KB";
        return std::to_wstring(fileSizeBytes / 1048576) + L" MB";
    }
};

// ── Utility helpers ──────────────────────────────────────────────────────────

static inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static inline uint32_t ReadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}
static inline uint32_t ReadLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static inline uint64_t ReadLE64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

// ── Vorbis Comment reader (used by FLAC and OGG) ─────────────────────────────

static void ParseVorbisComments(const uint8_t* data, size_t len, AudioTags& out) {
    if (len < 4) return;
    const uint8_t* p   = data;
    const uint8_t* end = data + len;

    // Vendor string length
    uint32_t vendorLen = ReadLE32(p); p += 4;
    if (p + vendorLen > end) return;
    p += vendorLen;

    if (p + 4 > end) return;
    uint32_t count = ReadLE32(p); p += 4;

    auto getField = [&](const std::string& line, const char* key) -> std::wstring {
        std::string k(key);
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        std::string l(line.substr(0, k.size()));
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l == k && line.size() > k.size() && line[k.size()] == '=')
            return Utf8ToWide(line.substr(k.size() + 1));
        return {};
    };

    for (uint32_t i = 0; i < count && p + 4 <= end; i++) {
        uint32_t tagLen = ReadLE32(p); p += 4;
        if (p + tagLen > end) break;
        std::string tag(reinterpret_cast<const char*>(p), tagLen);
        p += tagLen;

        auto try_set = [&](std::wstring& field, const char* key) {
            auto v = getField(tag, key);
            if (!v.empty() && field.empty()) field = std::move(v);
        };
        try_set(out.title,       "title");
        try_set(out.artist,      "artist");
        try_set(out.albumArtist, "albumartist");
        try_set(out.album,       "album");
        try_set(out.date,        "date");
        try_set(out.trackNumber, "tracknumber");
        try_set(out.totalTracks, "totaltracks");
        try_set(out.discNumber,  "discnumber");
        try_set(out.genre,       "genre");
        try_set(out.label,       "label");
        try_set(out.isrc,        "isrc");
        try_set(out.comment,     "comment");
        try_set(out.mbRecordingId, "musicbrainz_trackid");
        try_set(out.mbReleaseId,   "musicbrainz_albumid");
        try_set(out.mbArtistId,    "musicbrainz_artistid");
    }
}

// ── FLAC reader ───────────────────────────────────────────────────────────────

static bool ReadFlac(const std::wstring& path, AudioTags& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::unique_ptr<FILE, decltype(&fclose)> guard(f, fclose);

    uint8_t sig[4];
    if (fread(sig, 1, 4, f) != 4) return false;
    if (sig[0]!='f'||sig[1]!='L'||sig[2]!='a'||sig[3]!='C') return false;

    bool hasStreamInfo = false;

    while (true) {
        uint8_t hdr[4];
        if (fread(hdr, 1, 4, f) != 4) break;
        bool   isLast   = (hdr[0] & 0x80) != 0;
        uint8_t type    = hdr[0] & 0x7F;
        uint32_t length = (uint32_t(hdr[1])<<16)|(uint32_t(hdr[2])<<8)|hdr[3];

        std::vector<uint8_t> block(length);
        if (fread(block.data(), 1, length, f) != length) break;

        if (type == 0 && length >= 34) {
            // STREAMINFO
            hasStreamInfo = true;
            out.sampleRate    = (int(block[10])<<12)|(int(block[11])<<4)|(block[12]>>4);
            out.channels      = ((block[12]>>1) & 0x07) + 1;
            out.bitsPerSample = (((block[12]&1)<<4)|(block[13]>>4)) + 1;
            // Total samples is 36-bit value
            uint64_t totalSamples =
                (uint64_t(block[13] & 0x0F) << 32) | ReadBE32(&block[14]);
            if (out.sampleRate > 0 && totalSamples > 0)
                out.durationSec = double(totalSamples) / out.sampleRate;
        }
        else if (type == 4) {
            // VORBIS_COMMENT
            ParseVorbisComments(block.data(), block.size(), out);
        }

        if (isLast) break;
    }
    return hasStreamInfo;
}

// ── ID3v2 reader (MP3) ────────────────────────────────────────────────────────

static std::wstring DecodeId3String(const uint8_t* data, size_t len) {
    if (len == 0) return {};
    uint8_t enc = data[0];
    const uint8_t* s = data + 1;
    size_t slen = len - 1;
    // Strip BOM and null terminators
    if (enc == 1 || enc == 2) {
        // UTF-16
        if (slen >= 2 && ((s[0]==0xFF&&s[1]==0xFE)||(s[0]==0xFE&&s[1]==0xFF))) {
            s += 2; slen -= 2;
        }
        while (slen >= 2 && s[slen-2]==0 && s[slen-1]==0) slen -= 2;
        std::wstring result;
        result.resize(slen / 2);
        memcpy(result.data(), s, slen);
        return result;
    } else {
        // Latin-1 or UTF-8
        while (slen > 0 && s[slen-1] == 0) slen--;
        std::string raw(reinterpret_cast<const char*>(s), slen);
        if (enc == 3) return Utf8ToWide(raw);
        // Latin-1 -> wide
        std::wstring w(raw.size(), L'\0');
        for (size_t i = 0; i < raw.size(); i++) w[i] = wchar_t(uint8_t(raw[i]));
        return w;
    }
}

static bool ReadMp3Id3(const std::wstring& path, AudioTags& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::unique_ptr<FILE, decltype(&fclose)> guard(f, fclose);

    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) != 10) return false;
    if (hdr[0]!='I'||hdr[1]!='D'||hdr[2]!='3') return false;

    // Syncsafe size
    uint32_t tagSize =
        (uint32_t(hdr[6])<<21)|(uint32_t(hdr[7])<<14)|
        (uint32_t(hdr[8])<<7)|hdr[9];

    bool id3v24 = (hdr[3] >= 4);
    std::vector<uint8_t> tagData(tagSize);
    if (fread(tagData.data(), 1, tagSize, f) != tagSize) return false;

    size_t pos = 0;
    while (pos + 10 <= tagSize) {
        const uint8_t* p = tagData.data() + pos;
        std::string frameId(reinterpret_cast<const char*>(p), 4);
        uint32_t frameSize;
        if (id3v24) {
            frameSize = (uint32_t(p[4])<<21)|(uint32_t(p[5])<<14)|
                        (uint32_t(p[6])<<7)|p[7];
        } else {
            frameSize = ReadBE32(p + 4);
        }
        pos += 10;
        if (frameSize == 0 || pos + frameSize > tagSize) break;

        auto text = [&]() { return DecodeId3String(tagData.data()+pos, frameSize); };
        auto try_txxx = [&](std::wstring& field, const wchar_t* desc) {
            // TXXX: encoding(1) + description + \0 + value
            if (frameSize < 2) return;
            auto full = DecodeId3String(tagData.data()+pos, frameSize);
            // The description ends at the first null in full
            auto nl = full.find(L'\0');
            if (nl == std::wstring::npos) return;
            std::wstring d = full.substr(0, nl);
            std::wstring v = full.substr(nl+1);
            if (_wcsicmp(d.c_str(), desc) == 0 && field.empty()) field = v;
        };

        if      (frameId == "TIT2") { if (out.title.empty())       out.title       = text(); }
        else if (frameId == "TPE1") { if (out.artist.empty())      out.artist      = text(); }
        else if (frameId == "TPE2") { if (out.albumArtist.empty()) out.albumArtist = text(); }
        else if (frameId == "TALB") { if (out.album.empty())       out.album       = text(); }
        else if (frameId == "TDRC"||frameId == "TYER") {
            if (out.date.empty()) out.date = text();
        }
        else if (frameId == "TRCK") {
            if (out.trackNumber.empty()) {
                auto t = text();
                auto sl = t.find(L'/');
                if (sl != std::wstring::npos) {
                    out.trackNumber = t.substr(0, sl);
                    out.totalTracks = t.substr(sl+1);
                } else out.trackNumber = t;
            }
        }
        else if (frameId == "TPOS") { if (out.discNumber.empty())  out.discNumber  = text(); }
        else if (frameId == "TCON") { if (out.genre.empty())       out.genre       = text(); }
        else if (frameId == "TPUB") { if (out.label.empty())       out.label       = text(); }
        else if (frameId == "TSRC") { if (out.isrc.empty())        out.isrc        = text(); }
        else if (frameId == "COMM") { if (out.comment.empty())     out.comment     = text(); }
        else if (frameId == "TXXX") {
            try_txxx(out.mbRecordingId, L"MusicBrainz Track Id");
            try_txxx(out.mbReleaseId,   L"MusicBrainz Album Id");
            try_txxx(out.mbArtistId,    L"MusicBrainz Artist Id");
        }

        pos += frameSize;
    }

    // MP3 duration: scan for first MPEG frame header to get sample rate,
    // then use file size heuristic (good enough for display)
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    out.fileSizeBytes = fileSize;

    // Look for Xing/VBRI header or first MP3 frame
    fseek(f, tagSize + 10, SEEK_SET);
    std::vector<uint8_t> frameSearch(4096);
    size_t read = fread(frameSearch.data(), 1, 4096, f);
    for (size_t i = 0; i + 3 < read; i++) {
        uint8_t b0 = frameSearch[i], b1 = frameSearch[i+1];
        if (b0 != 0xFF || (b1 & 0xE0) != 0xE0) continue;
        // MPEG sync word found
        uint8_t b2 = frameSearch[i+2];
        int srIdx = (b2 >> 2) & 0x03;
        static const int srTable[4] = {44100, 48000, 32000, 0};
        out.sampleRate = srTable[srIdx];
        int brIdx  = (b2 >> 4) & 0x0F;
        static const int brTable[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        int bitrate = brTable[brIdx] * 1000;
        if (bitrate > 0 && out.sampleRate > 0)
            out.durationSec = double(fileSize * 8) / bitrate;
        break;
    }
    return true;
}

// ── OGG Vorbis reader ─────────────────────────────────────────────────────────

static bool ReadOgg(const std::wstring& path, AudioTags& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::unique_ptr<FILE, decltype(&fclose)> guard(f, fclose);

    // OGG pages: look for identification header (type 0x01) and comment header (type 0x03)
    bool gotIdent   = false;
    bool gotComment = false;
    uint32_t pageSeq = 0;

    while (!gotComment) {
        uint8_t hdr[27];
        if (fread(hdr, 1, 27, f) != 27) break;
        if (hdr[0]!='O'||hdr[1]!='g'||hdr[2]!='g'||hdr[3]!='S') break;

        uint8_t segs = hdr[26];
        std::vector<uint8_t> segTable(segs);
        if (fread(segTable.data(), 1, segs, f) != segs) break;

        uint32_t pageLen = 0;
        for (uint8_t s : segTable) pageLen += s;

        std::vector<uint8_t> pageData(pageLen);
        if (fread(pageData.data(), 1, pageLen, f) != pageLen) break;

        if (pageLen < 7) continue;
        uint8_t packetType = pageData[0];

        if (packetType == 0x01 && !gotIdent &&
            memcmp(pageData.data()+1, "vorbis", 6) == 0) {
            if (pageLen >= 28) {
                out.channels   = pageData[11];
                out.sampleRate = ReadLE32(&pageData[12]);
                int nomBitrate = int(ReadLE32(&pageData[20]));
                // Duration needs granule position from last page — skip for now
                // Use bitrate + file size heuristic
                if (nomBitrate > 0 && out.sampleRate > 0) {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f);
                    out.fileSizeBytes = sz;
                    out.durationSec = double(sz * 8) / nomBitrate;
                }
            }
            gotIdent = true;
        }
        else if (packetType == 0x03 && gotIdent &&
                 memcmp(pageData.data()+1, "vorbis", 6) == 0) {
            ParseVorbisComments(pageData.data()+7, pageLen-7, out);
            gotComment = true;
        }
        pageSeq++;
    }
    return gotIdent;
}

// ── Main dispatch ─────────────────────────────────────────────────────────────

class TagReader {
public:
    static AudioTags Read(const std::wstring& path) {
        AudioTags tags;

        // File size
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
            tags.fileSizeBytes =
                (int64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        }

        std::wstring ext = path;
        auto dot = ext.rfind(L'.');
        if (dot != std::wstring::npos) {
            ext = ext.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        }

        if      (ext == L".flac") ReadFlac(path, tags);
        else if (ext == L".mp3")  ReadMp3Id3(path, tags);
        else if (ext == L".ogg")  ReadOgg(path, tags);
        // m4a/aac/opus: left as extension point (requires more complex box parsing)

        return tags;
    }
};
