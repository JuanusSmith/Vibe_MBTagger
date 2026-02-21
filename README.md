# MusicBrainz Tagger — Native Win32 C++

A zero-dependency native Windows application that identifies audio files via
AcoustID fingerprinting, fetches full metadata from MusicBrainz, and exports
everything to CSV. No Python, no Electron, no .NET — pure Win32 C++.

---

## Features

- **Drag-and-drop** files AND folders (recursive scan) anywhere onto the window
- **Multi-threaded** processing — one thread per CPU core, up to 16
- **Per-thread progress lanes** — each worker shows its current file and phase
- **Title-bar percentage** — `[42%] MusicBrainz Tagger` while processing
- **Zero external C++ dependencies** — uses only Win32 APIs and WinHTTP
- **Built-in tag reader** — parses FLAC, MP3 (ID3v2), and OGG Vorbis natively
- **Built-in JSON parser** — no rapidjson, nlohmann/json, etc. needed
- **CSV export** with UTF-8 BOM (opens correctly in Excel)
- **API key saved to registry** — enter it once, remembered forever
- Dark theme with Consolas monospace UI

---

## Building

### Requirements

| Tool | Version |
|------|---------|
| Visual Studio 2022 | Community or better |
| Windows SDK | 10.0.22000 or newer |
| C++ standard | C++20 |
| Target | x64, Windows 10+ |

No NuGet packages required. All dependencies are built into Windows.

### Steps

1. Open `MBTagger.sln` in Visual Studio 2022
2. Select **Release | x64**
3. Press **Ctrl+Shift+B** (Build Solution)
4. Output: `build\Release\MBTagger.exe`

### Command-line build (MSBuild)

```bat
msbuild MBTagger.sln /p:Configuration=Release /p:Platform=x64
```

---

## Setup & First Run

### 1. Get an AcoustID API key (free)

1. Sign up at https://acoustid.org/login
2. Register a new application
3. Copy your API key
4. Paste it into the key bar at the top of the app — it's saved automatically

### 2. Install fpcalc.exe (Chromaprint)

AcoustID fingerprinting requires the `fpcalc.exe` binary.

**Option A — place next to MBTagger.exe (recommended):**
1. Download from https://acoustid.org/chromaprint
2. Extract `fpcalc.exe` into the same folder as `MBTagger.exe`

**Option B — add to PATH:**
The app will find `fpcalc.exe` anywhere on your `%PATH%`.

---

## Usage

1. **Drop** audio files or entire folders onto the window
   - Folders are scanned recursively for: `.flac .mp3 .ogg .m4a .wav .aiff .ape .wv .opus`
   - Or click **📂 Add Files / Folders…** to use the file picker
2. Click **▶ Scan Files**
3. Watch the worker thread lanes process files in parallel
4. Click any file in the list to view its full metadata on the right
5. Click **⬇ Export CSV** when done

---

## Architecture

```
MBTagger.exe
├── main.cpp              Entry point, COM init, message loop
├── MainWindow.h          Win32 window, custom painting, IDropTarget
├── Scanner.h             Thread pool (std::thread × cpu_count), work queue
├── TagReader.h           Binary FLAC/ID3v2/OGG tag parser (no taglib)
├── MusicBrainzApi.h      fpcalc runner, AcoustID HTTP, MusicBrainz HTTP
├── HttpClient.h          WinHTTP HTTPS GET client
├── Json.h                Recursive-descent JSON parser (no external lib)
└── CsvExport.h           UTF-8 CSV writer
```

### Threading model

```
Main thread (UI)
  │
  └─ Scanner::Start() ──► Coordinator thread
                              │
                              ├─ WorkerThread(0) ──► fpcalc → AcoustID → MB
                              ├─ WorkerThread(1) ──► fpcalc → AcoustID → MB
                              ├─ WorkerThread(2) ──► fpcalc → AcoustID → MB
                              └─ WorkerThread(N) ──► fpcalc → AcoustID → MB
                                        │
                                        └─ PostMessage(WM_SCAN_EVENT) → main thread
```

Worker threads post `ScanEvent` messages to the main window via `PostMessageW`,
keeping all UI updates on the main thread (thread-safe).

MusicBrainz requires ≥1 second between requests — this is enforced via a
shared mutex and `Sleep()` inside `MusicBrainzClient::LookupRecording()`.

### Tag reading

Tags are read from raw binary without external libraries:

| Format | Method |
|--------|--------|
| FLAC | STREAMINFO block → sample rate, bit depth, duration; VORBIS_COMMENT block → tags |
| MP3 | ID3v2 frame parser (v2.3 and v2.4, Latin-1/UTF-16/UTF-8) |
| OGG | OGG page reader → Vorbis identification + comment headers |

---

## CSV Columns

| Column | Source |
|--------|--------|
| Filename | File system |
| Folder | Parent directory path |
| Title, Artist, Album Artist, Album, Date | MusicBrainz (falls back to file tags) |
| Track, Total Tracks, Disc | MusicBrainz |
| Genre, Label | File tags |
| ISRC, Country, Status, Type | MusicBrainz |
| Duration, Sample Rate, Bit Depth, Channels | Audio stream info |
| File Size | File system |
| AcoustID Score (%) | AcoustID confidence |
| MB Recording/Release/Artist/Group ID | MusicBrainz UUIDs |

---

## Extending

### Adding M4A / AAC support

Implement `ReadM4a()` in `TagReader.h`:
- Parse ISO Base Media File Format (MPEG-4 boxes)
- Look for `moov > udta > meta > ilst` atoms for iTunes tags
- `mdhd` box inside `moov > trak > mdia` for duration and sample rate

### Writing tags back

After lookup, use the MusicBrainz data in `ScanResult::mb` to write back:
- FLAC: rewrite VORBIS_COMMENT metadata block
- MP3: construct ID3v2 frames, write new header
- Consider using TagLib (LGPL) for production tag writing

---

## License

MIT — see LICENSE file.
