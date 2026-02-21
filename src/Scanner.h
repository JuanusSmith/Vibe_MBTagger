// Scanner.h - Multi-threaded file scanner
#pragma once
#include "pch.h"
#include "TagReader.h"
#include "MusicBrainzApi.h"

// ── Per-file result ───────────────────────────────────────────────────────────

enum class ScanStatus { Pending, Running, Done, Error };

struct ScanResult {
    std::wstring filePath;
    std::wstring folderPath;
    ScanStatus   status    = ScanStatus::Pending;
    int          workerId  = -1;

    std::atomic<int> progress{ 0 };
    std::wstring     currentStep;

    AudioTags       tags;
    MusicBrainzData mb;
    std::wstring    errorMsg;
};

// ── Progress event posted to main window ─────────────────────────────────────

struct ScanEvent {
    enum class Kind { FileStarted, FileProgress, FileDone, AllDone } kind;
    int fileIndex  = -1;
    int workerId   = -1;
    int totalDone  = 0;
    int total      = 0;
};

// ── Scanner ───────────────────────────────────────────────────────────────────

class Scanner {
public:
    using ProgressCallback = std::function<void(const ScanEvent&)>;

    explicit Scanner(std::wstring acoustIdKey, HWND notifyWnd, UINT notifyMsg)
        : acoustIdKey_(std::move(acoustIdKey))
        , notifyWnd_(notifyWnd)
        , notifyMsg_(notifyMsg)
    {
        numWorkers_ = std::min(static_cast<int>(std::thread::hardware_concurrency()), 16);
        if (numWorkers_ < 1) numWorkers_ = 4;
    }

    int  WorkerCount() const { return numWorkers_; }
    bool IsRunning()   const { return running_.load(); }
    int  Total()       const { return total_; }
    int  TotalDone()   const { return totalDone_.load(); }

    const std::vector<std::shared_ptr<ScanResult>>& Results() const { return results_; }

    // Recursively collect audio files from a list of paths (files or folders)
    static std::vector<std::wstring> CollectFiles(const std::vector<std::wstring>& paths) {
        static const std::unordered_set<std::wstring> EXTS = {
            L".flac", L".mp3", L".ogg", L".m4a",
            L".wav",  L".aiff",L".ape", L".wv", L".opus"
        };

        std::vector<std::wstring> files;

        std::function<void(const std::wstring&)> walk = [&](const std::wstring& p) {
            DWORD attr = GetFileAttributesW(p.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) return;

            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                WIN32_FIND_DATAW fd = {};
                HANDLE h = FindFirstFileW((p + L"\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) return;
                do {
                    if (fd.cFileName[0] == L'.') continue;
                    walk(p + L"\\" + fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            } else {
                std::wstring ext = p;
                size_t dot = ext.rfind(L'.');
                if (dot != std::wstring::npos) {
                    ext = ext.substr(dot);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if (EXTS.count(ext)) files.push_back(p);
                }
            }
        };

        for (const auto& p : paths) walk(p);
        return files;
    }

    // Start scanning — non-blocking, runs on a thread pool
    void Start(const std::vector<std::wstring>& filePaths) {
        if (running_.load()) return;
        running_   = true;
        cancelled_ = false;
        totalDone_ = 0;
        total_     = static_cast<int>(filePaths.size());

        results_.clear();
        results_.reserve(filePaths.size());
        for (const auto& p : filePaths) {
            auto r      = std::make_shared<ScanResult>();
            r->filePath = p;
            // Extract parent folder
            size_t sl = p.rfind(L'\\');
            r->folderPath = (sl != std::wstring::npos) ? p.substr(0, sl) : L"";
            results_.push_back(r);
        }

        std::thread([this] { RunPool(); }).detach();
    }

    void Cancel() { cancelled_ = true; }

private:
    std::wstring      acoustIdKey_;
    HWND              notifyWnd_;
    UINT              notifyMsg_;
    int               numWorkers_  = 4;
    std::atomic<bool> running_     { false };
    std::atomic<bool> cancelled_   { false };
    std::atomic<int>  totalDone_   { 0 };
    int               total_       = 0;

    std::vector<std::shared_ptr<ScanResult>> results_;

    std::mutex              qMutex_;
    std::queue<int>         workQueue_;
    std::condition_variable qCV_;

    void Notify(ScanEvent ev) {
        ev.totalDone = totalDone_.load();
        ev.total     = total_;
        auto* copy   = new ScanEvent(ev);
        PostMessageW(notifyWnd_, notifyMsg_, 0, reinterpret_cast<LPARAM>(copy));
    }

    void WorkerThread(int workerId) {
        AcoustIdClient    acoustid(acoustIdKey_);
        MusicBrainzClient mb;

        while (!cancelled_.load()) {
            int idx = -1;
            {
                std::unique_lock<std::mutex> lk(qMutex_);
                qCV_.wait(lk, [this] {
                    return !workQueue_.empty() || cancelled_.load();
                });
                if (cancelled_.load()) break;
                if (workQueue_.empty()) continue;
                idx = workQueue_.front();
                workQueue_.pop();
            }

            auto& r    = *results_[static_cast<size_t>(idx)];
            r.status   = ScanStatus::Running;
            r.workerId = workerId;

            Notify({ ScanEvent::Kind::FileStarted, idx, workerId });

            // Step 1: Tags
            r.currentStep = L"Reading tags\u2026";
            r.progress    = 20;
            Notify({ ScanEvent::Kind::FileProgress, idx, workerId });
            r.tags = TagReader::Read(r.filePath);

            if (cancelled_.load()) break;

            // Step 2: Fingerprint
            r.currentStep = L"Fingerprinting\u2026";
            r.progress    = 45;
            Notify({ ScanEvent::Kind::FileProgress, idx, workerId });
            auto fp = acoustid.Lookup(r.filePath);

            if (cancelled_.load()) break;

            // Step 3: MusicBrainz
            r.currentStep = L"MusicBrainz lookup\u2026";
            r.progress    = 75;
            Notify({ ScanEvent::Kind::FileProgress, idx, workerId });

            if (fp.ok)
                r.mb = mb.LookupRecording(fp.recordingId, fp.score * 100.0);
            else
                r.mb.error = fp.error;

            r.progress    = 100;
            r.currentStep = r.mb.ok ? L"Matched" : L"No match";
            r.status      = ScanStatus::Done;

            totalDone_++;
            Notify({ ScanEvent::Kind::FileDone, idx, workerId });
        }
    }

    void RunPool() {
        {
            std::lock_guard<std::mutex> lk(qMutex_);
            for (int i = 0; i < static_cast<int>(results_.size()); i++)
                workQueue_.push(i);
        }

        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(numWorkers_));
        for (int w = 0; w < numWorkers_; w++)
            threads.emplace_back([this, w] { WorkerThread(w); });

        qCV_.notify_all();

        for (auto& t : threads) t.join();

        running_ = false;
        Notify({ ScanEvent::Kind::AllDone, -1, -1 });
    }
};
