// MainWindow.h - Main application window
#pragma once
#include "pch.h"
#include "Scanner.h"
#include "CsvExport.h"
#include "resource.h"

#define WM_SCAN_EVENT (WM_USER + 100)

enum : int {
    IDC_LISTVIEW   = 200,
    IDC_BTN_SCAN   = 201,
    IDC_BTN_EXPORT = 202,
    IDC_BTN_CLEAR  = 203,
    IDC_BTN_BROWSE = 204,
    IDC_EDIT_KEY   = 205,
    IDC_STATIC_KEY = 206,
};

static const COLORREF CLR_BG      = RGB(0x0B,0x0C,0x0E);
static const COLORREF CLR_SURFACE = RGB(0x11,0x12,0x15);
static const COLORREF CLR_BORDER  = RGB(0x25,0x25,0x30);
static const COLORREF CLR_ACCENT  = RGB(0xE8,0xC5,0x47);
static const COLORREF CLR_ACCENT2 = RGB(0x4F,0xC3,0xA1);
static const COLORREF CLR_DANGER  = RGB(0xE0,0x5C,0x5C);
static const COLORREF CLR_TEXT    = RGB(0xDD,0xDD,0xE8);
static const COLORREF CLR_MUTED   = RGB(0x62,0x63,0x7A);

// ── IDropTarget ───────────────────────────────────────────────────────────────

class DropTarget : public IDropTarget {
    LONG   refCount_ = 1;
    HWND   hwnd_;
    std::function<void(std::vector<std::wstring>)> onDrop_;
public:
    DropTarget(HWND hwnd, std::function<void(std::vector<std::wstring>)> cb)
        : hwnd_(hwnd), onDrop_(std::move(cb)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid==IID_IUnknown||riid==IID_IDropTarget){ *ppv=this; AddRef(); return S_OK; }
        *ppv=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&refCount_);
        if (n==0) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*,DWORD,POINTL,DWORD* e) override { *e=DROPEFFECT_COPY; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* e)               override { *e=DROPEFFECT_COPY; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragLeave()                                    override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pObj,DWORD,POINTL,DWORD* e) override {
        *e = DROPEFFECT_COPY;
        FORMATETC fmt = { CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL };
        STGMEDIUM stg = {};
        if (FAILED(pObj->GetData(&fmt,&stg))) return S_OK;
        HDROP hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
        if (hDrop) {
            UINT n = DragQueryFileW(hDrop,0xFFFFFFFF,nullptr,0);
            std::vector<std::wstring> paths;
            paths.reserve(n);
            for (UINT i=0;i<n;i++) {
                wchar_t buf[MAX_PATH*2];
                DragQueryFileW(hDrop,i,buf,MAX_PATH*2);
                paths.push_back(buf);
            }
            GlobalUnlock(stg.hGlobal);
            ReleaseStgMedium(&stg);
            onDrop_(std::move(paths));
        }
        return S_OK;
    }
};

// ── MainWindow ────────────────────────────────────────────────────────────────

class MainWindow {
    HWND   hwnd_       = nullptr;
    HWND   hList_      = nullptr;
    HWND   hBtnScan_   = nullptr;
    HWND   hBtnExport_ = nullptr;
    HWND   hBtnClear_  = nullptr;
    HWND   hBtnBrowse_ = nullptr;
    HWND   hEditKey_   = nullptr;
    HWND   hStaticKey_ = nullptr;

    HFONT  hFontMono_  = nullptr;
    HFONT  hFontUI_    = nullptr;
    HFONT  hFontBig_   = nullptr;
    HBRUSH hBrushBg_   = nullptr;
    HBRUSH hBrushSurf_ = nullptr;

    std::unique_ptr<Scanner>  scanner_;
    std::vector<std::wstring> pendingPaths_;

    struct LaneState {
        std::wstring filename;
        std::wstring step;
        int  pct    = 0;
        bool active = false;
        bool done   = false;
    };
    std::vector<LaneState> lanes_;
    int numWorkers_ = 4;
    int selectedIdx_ = -1;

    RECT rcLanes_   = {};

    static constexpr int LEFT_W    = 380;
    static constexpr int LANE_H    = 18;
    static constexpr int KEY_BAR_H = 36;
    static constexpr int ACTIONS_H = 56;

public:
    bool Create(HINSTANCE hInst) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc   = WndProcStatic;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"MBTaggerMain";
        if (!RegisterClassExW(&wc)) return false;

        hwnd_ = CreateWindowExW(
            WS_EX_ACCEPTFILES, L"MBTaggerMain",
            L"MusicBrainz Tagger",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,1200,700,
            nullptr,nullptr,hInst,this);
        return hwnd_ != nullptr;
    }

    void Show(int n) { ShowWindow(hwnd_,n); UpdateWindow(hwnd_); }

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        MainWindow* self = nullptr;
        if (msg==WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        }
        if (self) return self->WndProc(msg,wp,lp);
        return DefWindowProcW(hwnd,msg,wp,lp);
    }

    LRESULT WndProc(UINT msg,WPARAM wp,LPARAM lp) {
        switch (msg) {
        case WM_CREATE:       return OnCreate();
        case WM_DESTROY:      PostQuitMessage(0); return 0;
        case WM_SIZE:         OnSize(LOWORD(lp),HIWORD(lp)); return 0;
        case WM_PAINT:        OnPaint(); return 0;
        case WM_ERASEBKGND:   return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: return OnCtlColor((HDC)wp,(HWND)lp);
        case WM_COMMAND:      OnCommand(LOWORD(wp)); return 0;
        case WM_NOTIFY:       return OnNotify(reinterpret_cast<NMHDR*>(lp));
        case WM_SCAN_EVENT:   OnScanEvent(reinterpret_cast<ScanEvent*>(lp)); return 0;
        case WM_DROPFILES:    OnDropFiles((HDROP)wp); return 0;
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }

    // Helper: create child window with correct HMENU cast
    HWND MakeChild(LPCWSTR cls, LPCWSTR txt, DWORD style, int id) {
        return CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|style,
            0,0,0,0,hwnd_,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
            nullptr,nullptr);
    }

    LRESULT OnCreate() {
        hBrushBg_   = CreateSolidBrush(CLR_BG);
        hBrushSurf_ = CreateSolidBrush(CLR_SURFACE);

        hFontMono_ = CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            FIXED_PITCH,L"Consolas");
        hFontUI_ = CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH,L"Segoe UI");
        hFontBig_ = CreateFontW(18,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH,L"Segoe UI");

        hStaticKey_ = MakeChild(L"STATIC",L"AcoustID Key:",SS_LEFT,IDC_STATIC_KEY);
        SendMessageW(hStaticKey_,WM_SETFONT,(WPARAM)hFontUI_,0);

        hEditKey_ = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
            0,0,0,0,hwnd_,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_EDIT_KEY)),
            nullptr,nullptr);
        SendMessageW(hEditKey_,WM_SETFONT,(WPARAM)hFontMono_,0);

        // Load saved API key
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\MBTagger",0,KEY_READ,&hk)==ERROR_SUCCESS) {
            wchar_t buf[256]; DWORD sz=sizeof(buf);
            if (RegQueryValueExW(hk,L"AcoustIDKey",nullptr,nullptr,(LPBYTE)buf,&sz)==ERROR_SUCCESS)
                SetWindowTextW(hEditKey_,buf);
            RegCloseKey(hk);
        }

        hBtnScan_   = MakeChild(L"BUTTON",L"\u25B6  Scan Files",  BS_PUSHBUTTON,IDC_BTN_SCAN);
        hBtnExport_ = MakeChild(L"BUTTON",L"\u2B07  Export CSV",  BS_PUSHBUTTON,IDC_BTN_EXPORT);
        hBtnClear_  = MakeChild(L"BUTTON",L"\u2715  Clear",       BS_PUSHBUTTON,IDC_BTN_CLEAR);
        hBtnBrowse_ = MakeChild(L"BUTTON",L"\U0001F4C2  Add Files / Folders\u2026",BS_PUSHBUTTON,IDC_BTN_BROWSE);

        for (HWND h : {hBtnScan_,hBtnExport_,hBtnClear_,hBtnBrowse_})
            SendMessageW(h,WM_SETFONT,(WPARAM)hFontUI_,0);

        EnableWindow(hBtnScan_,   FALSE);
        EnableWindow(hBtnExport_, FALSE);

        // ListView
        hList_ = CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
            WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOSORTHEADER,
            0,0,0,0,hwnd_,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_LISTVIEW)),
            nullptr,nullptr);
        SendMessageW(hList_,WM_SETFONT,(WPARAM)hFontMono_,0);
        ListView_SetExtendedListViewStyle(hList_,
            LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
        SetWindowTheme(hList_,L"DarkMode_Explorer",nullptr);

        auto addCol = [&](const wchar_t* txt,int w){
            LVCOLUMNW lvc={};
            lvc.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
            lvc.fmt=LVCFMT_LEFT; lvc.cx=w;
            lvc.pszText=const_cast<wchar_t*>(txt);
            ListView_InsertColumn(hList_,99,&lvc);
        };
        addCol(L"File",   200); addCol(L"Status", 80);
        addCol(L"Title",  160); addCol(L"Artist",130);
        addCol(L"Album",  130); addCol(L"Date",   55);
        addCol(L"Score",   55);

        RegisterDragDrop(hwnd_, new DropTarget(hwnd_,
            [this](std::vector<std::wstring> paths){ AddPaths(std::move(paths)); }));
        DragAcceptFiles(hwnd_,TRUE);

        numWorkers_ = std::min((int)std::thread::hardware_concurrency(),16);
        if (numWorkers_<1) numWorkers_=4;
        lanes_.resize(numWorkers_);

        return 0;
    }

    void OnSize(int w,int h) {
        int lanesH  = numWorkers_*(LANE_H+2)+22;
        int listTop = KEY_BAR_H+lanesH+4;
        int listH   = h-KEY_BAR_H-lanesH-ACTIONS_H-8;

        SetWindowPos(hStaticKey_,nullptr,8,(KEY_BAR_H-13)/2,110,16,SWP_NOZORDER);
        SetWindowPos(hEditKey_,  nullptr,120,6,LEFT_W-210,KEY_BAR_H-12,SWP_NOZORDER);
        SetWindowPos(hBtnBrowse_,nullptr,LEFT_W-85,6,80,KEY_BAR_H-12,SWP_NOZORDER);
        SetWindowPos(hList_,     nullptr,4,listTop,LEFT_W-8,listH,SWP_NOZORDER);

        int actY=h-ACTIONS_H, bw=(LEFT_W-12)/3;
        SetWindowPos(hBtnScan_,  nullptr,4,        actY+8,bw,   32,SWP_NOZORDER);
        SetWindowPos(hBtnExport_,nullptr,4+bw+2,   actY+8,bw,   32,SWP_NOZORDER);
        SetWindowPos(hBtnClear_, nullptr,4+bw*2+4, actY+8,bw-4, 32,SWP_NOZORDER);

        rcLanes_={4,KEY_BAR_H,LEFT_W-4,KEY_BAR_H+lanesH};
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc=BeginPaint(hwnd_,&ps);
        RECT rc; GetClientRect(hwnd_,&rc);

        HDC memDC=CreateCompatibleDC(hdc);
        HBITMAP bmp=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
        SelectObject(memDC,bmp);

        FillRect(memDC,&rc,hBrushBg_);

        // Dividers
        auto hline=[&](int x1,int y,int x2){
            HPEN p=CreatePen(PS_SOLID,1,CLR_BORDER);
            SelectObject(memDC,p);
            MoveToEx(memDC,x1,y,nullptr); LineTo(memDC,x2,y);
            DeleteObject(p);
        };
        auto vline=[&](int x,int y1,int y2){
            HPEN p=CreatePen(PS_SOLID,1,CLR_BORDER);
            SelectObject(memDC,p);
            MoveToEx(memDC,x,y1,nullptr); LineTo(memDC,x,y2);
            DeleteObject(p);
        };
        vline(LEFT_W,0,rc.bottom);
        hline(0,KEY_BAR_H,LEFT_W);

        DrawWorkerLanes(memDC);

        RECT rcR={LEFT_W+1,KEY_BAR_H,rc.right,rc.bottom};
        DrawDetailPanel(memDC,rcR);

        BitBlt(hdc,0,0,rc.right,rc.bottom,memDC,0,0,SRCCOPY);
        DeleteObject(bmp); DeleteDC(memDC);
        EndPaint(hwnd_,&ps);
    }

    void DrawWorkerLanes(HDC dc) {
        SetBkMode(dc,TRANSPARENT);
        HFONT old=(HFONT)SelectObject(dc,hFontMono_);

        SetTextColor(dc,CLR_MUTED);
        RECT rl={8,rcLanes_.top,LEFT_W-4,rcLanes_.top+14};
        DrawTextW(dc,L"WORKER THREADS",-1,&rl,DT_LEFT|DT_SINGLELINE|DT_VCENTER);

        int y=rcLanes_.top+16;
        for (int i=0;i<numWorkers_;i++) {
            const auto& lane=lanes_[i];
            int bx=30, bw=LEFT_W-bx-155, bh=4, by=y+(LANE_H-bh)/2;

            wchar_t wid[4]; swprintf_s(wid,L"%2d",i);
            SetTextColor(dc,CLR_MUTED);
            RECT rid={4,y,28,y+LANE_H};
            DrawTextW(dc,wid,-1,&rid,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);

            RECT bg={bx,by,bx+bw,by+bh};
            HBRUSH br=CreateSolidBrush(CLR_BORDER);
            FillRect(dc,&bg,br); DeleteObject(br);

            if (lane.pct>0) {
                int fw=(int)(bw*lane.pct/100.0);
                RECT fill={bx,by,bx+fw,by+bh};
                br=CreateSolidBrush(lane.done?CLR_ACCENT2:CLR_ACCENT);
                FillRect(dc,&fill,br); DeleteObject(br);
            }

            RECT rfn={bx+bw+6,y,LEFT_W-4,y+LANE_H};
            SetTextColor(dc,lane.done?CLR_ACCENT2:(lane.active?CLR_TEXT:CLR_MUTED));
            std::wstring fn=lane.filename;
            if (fn.size()>20) fn=L"\u2026"+fn.substr(fn.size()-19);
            DrawTextW(dc,fn.empty()?L"\u2014":fn.c_str(),-1,&rfn,
                DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);

            y+=LANE_H+2;
        }
        SelectObject(dc,old);
    }

    void DrawDetailPanel(HDC dc,RECT rc) {
        SetBkMode(dc,TRANSPARENT);

        if (selectedIdx_<0||!scanner_) {
            SetTextColor(dc,CLR_MUTED);
            SelectObject(dc,hFontUI_);
            DrawTextW(dc,L"Drop audio files or folders to begin",-1,&rc,
                DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }

        const auto& results=scanner_->Results();
        if (selectedIdx_>=(int)results.size()) return;
        const auto& r =*results[static_cast<size_t>(selectedIdx_)];
        const auto& t =r.tags;
        const auto& mb=r.mb;

        auto pick=[](const std::wstring& a,const std::wstring& b)->const std::wstring& {
            return !a.empty()?a:b;
        };

        int x=rc.left+16, y=rc.top+14;

        SelectObject(dc,hFontBig_);
        SetTextColor(dc,CLR_TEXT);
        RECT rt={x,y,rc.right-8,y+26};
        std::wstring title=pick(mb.title,t.title);
        DrawTextW(dc,title.empty()?L"Unknown Title":title.c_str(),-1,&rt,
            DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        y+=28;

        SelectObject(dc,hFontUI_);
        SetTextColor(dc,CLR_MUTED);
        std::wstring al=pick(mb.artist,t.artist);
        if (mb.acoustidScore>0){
            wchar_t sc[32]; swprintf_s(sc,L"  [%.1f%% match]",mb.acoustidScore);
            al+=sc;
        }
        RECT ra={x,y,rc.right-8,y+18};
        DrawTextW(dc,al.c_str(),-1,&ra,DT_LEFT|DT_SINGLELINE);
        y+=24;

        HPEN pen=CreatePen(PS_SOLID,1,CLR_BORDER);
        SelectObject(dc,pen);
        MoveToEx(dc,x,y,nullptr); LineTo(dc,rc.right-8,y);
        DeleteObject(pen);
        y+=10;

        SelectObject(dc,hFontMono_);
        int colW=(rc.right-rc.left-32)/2;

        struct F { std::wstring label,value; };
        std::vector<F> fields={
            {L"ALBUM",   pick(mb.album,t.album)},
            {L"DATE",    pick(mb.date, t.date)},
            {L"TRACK",   pick(mb.trackNumber,t.trackNumber)+
                         (!pick(mb.totalTracks,t.totalTracks).empty()
                          ?L" / "+pick(mb.totalTracks,t.totalTracks):L"")},
            {L"DISC",    pick(mb.discNumber,t.discNumber)},
            {L"GENRE",   pick(mb.genre,t.genre)},
            {L"LABEL",   pick(mb.label,t.label)},
            {L"ISRC",    pick(mb.isrc,t.isrc)},
            {L"COUNTRY", mb.country},
            {L"STATUS",  mb.status},
            {L"TYPE",    mb.primaryType},
            {L"DURATION",t.DurationFormatted()},
            {L"SAMPLE RATE",t.sampleRate>0?std::to_wstring(t.sampleRate)+L" Hz":L""},
            {L"BIT DEPTH",t.bitsPerSample>0?std::to_wstring(t.bitsPerSample)+L"-bit":L""},
            {L"CHANNELS",t.channels>0?std::to_wstring(t.channels):L""},
            {L"FILE SIZE",t.FileSizeFormatted()},
        };

        int col=0, rowH=36;
        for (const auto& f:fields) {
            if (f.value.empty()) continue;
            int fx=x+col*(colW+8), fy=y;
            SetTextColor(dc,CLR_MUTED);
            RECT rl={fx,fy,fx+colW,fy+13};
            DrawTextW(dc,f.label.c_str(),-1,&rl,DT_LEFT|DT_SINGLELINE);
            SetTextColor(dc,CLR_TEXT);
            RECT rv={fx,fy+14,fx+colW,fy+rowH};
            DrawTextW(dc,f.value.c_str(),-1,&rv,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            col++;
            if (col>=2){col=0;y+=rowH;}
            if (y>rc.bottom-rowH) break;
        }
        if (col!=0) y+=rowH;
        y+=8;

        pen=CreatePen(PS_SOLID,1,CLR_BORDER);
        SelectObject(dc,pen);
        MoveToEx(dc,x,y,nullptr); LineTo(dc,rc.right-8,y);
        DeleteObject(pen);
        y+=8;

        struct MF { std::wstring label,value; };
        for (const MF& mf:{
            MF{L"Recording ID",mb.mbRecordingId},
            MF{L"Release ID",  mb.mbReleaseId},
            MF{L"Artist ID",   mb.mbArtistId}})
        {
            if (mf.value.empty()) continue;
            SetTextColor(dc,CLR_MUTED);
            RECT rl={x,y,x+90,y+14};
            DrawTextW(dc,mf.label.c_str(),-1,&rl,DT_LEFT|DT_SINGLELINE);
            SetTextColor(dc,CLR_ACCENT2);
            RECT rv={x+95,y,rc.right-8,y+14};
            DrawTextW(dc,mf.value.c_str(),-1,&rv,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            y+=18;
        }

        if (!mb.ok&&!mb.error.empty()) {
            RECT re={x,y+4,rc.right-8,y+24};
            SetTextColor(dc,CLR_DANGER);
            DrawTextW(dc,(L"\u26A0 "+mb.error).c_str(),-1,&re,DT_LEFT|DT_SINGLELINE);
        }
    }

    LRESULT OnCtlColor(HDC hdc,HWND) {
        SetBkColor(hdc,CLR_SURFACE);
        SetTextColor(hdc,CLR_TEXT);
        return (LRESULT)hBrushSurf_;
    }

    void OnCommand(int id) {
        switch(id){
        case IDC_BTN_SCAN:   StartScan();   break;
        case IDC_BTN_EXPORT: DoExport();    break;
        case IDC_BTN_CLEAR:  ClearAll();    break;
        case IDC_BTN_BROWSE: BrowseFiles(); break;
        }
    }

    void BrowseFiles() {
        wchar_t buf[32768]={};
        OPENFILENAMEW ofn={};
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_;
        ofn.lpstrFilter=L"Audio Files\0*.flac;*.mp3;*.ogg;*.m4a;*.wav;*.aiff;*.ape;*.wv;*.opus\0All Files\0*.*\0";
        ofn.lpstrFile=buf; ofn.nMaxFile=32768;
        ofn.Flags=OFN_ALLOWMULTISELECT|OFN_EXPLORER|OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return;

        std::vector<std::wstring> paths;
        wchar_t* p=buf;
        std::wstring dir=p; p+=dir.size()+1;
        if (*p==L'\0') { paths.push_back(dir); }
        else { while(*p){ paths.push_back(dir+L"\\"+p); p+=wcslen(p)+1; } }
        AddPaths(std::move(paths));
    }

    void AddPaths(std::vector<std::wstring> rawPaths) {
        auto files=Scanner::CollectFiles(rawPaths);
        for (const auto& f:files) {
            bool dup=false;
            for (const auto& e:pendingPaths_) if(e==f){dup=true;break;}
            if (!dup) pendingPaths_.push_back(f);
        }
        RefreshListView();
        EnableWindow(hBtnScan_,pendingPaths_.empty()?FALSE:TRUE);
    }

    void RefreshListView() {
        ListView_DeleteAllItems(hList_);
        const auto* results=scanner_?&scanner_->Results():nullptr;

        auto addRow=[&](int idx,const std::wstring& path,
            const std::wstring& status,const std::wstring& title,
            const std::wstring& artist,const std::wstring& album,
            const std::wstring& date,const std::wstring& score)
        {
            std::wstring fn=path;
            if (auto sl=fn.rfind(L'\\');sl!=std::wstring::npos) fn=fn.substr(sl+1);
            LVITEMW lvi={}; lvi.mask=LVIF_TEXT; lvi.iItem=idx;
            lvi.pszText=fn.data();
            ListView_InsertItem(hList_,&lvi);
            ListView_SetItemText(hList_,idx,1,const_cast<wchar_t*>(status.c_str()));
            ListView_SetItemText(hList_,idx,2,const_cast<wchar_t*>(title.c_str()));
            ListView_SetItemText(hList_,idx,3,const_cast<wchar_t*>(artist.c_str()));
            ListView_SetItemText(hList_,idx,4,const_cast<wchar_t*>(album.c_str()));
            ListView_SetItemText(hList_,idx,5,const_cast<wchar_t*>(date.c_str()));
            ListView_SetItemText(hList_,idx,6,const_cast<wchar_t*>(score.c_str()));
        };

        if (results&&!results->empty()) {
            for (int i=0;i<(int)results->size();i++) {
                const auto& r=*(*results)[static_cast<size_t>(i)];
                const auto& mb=r.mb; const auto& t=r.tags;
                std::wstring status,score;
                switch(r.status){
                    case ScanStatus::Pending: status=L"Pending";      break;
                    case ScanStatus::Running: status=r.currentStep;   break;
                    case ScanStatus::Done:    status=mb.ok?L"\u2713 Matched":L"\u2717 No match"; break;
                    case ScanStatus::Error:   status=L"Error";        break;
                }
                if (mb.acoustidScore>0){ wchar_t sc[16]; swprintf_s(sc,L"%.0f%%",mb.acoustidScore); score=sc; }
                addRow(i,r.filePath,status,
                    !mb.title.empty()?mb.title:t.title,
                    !mb.artist.empty()?mb.artist:t.artist,
                    !mb.album.empty()?mb.album:t.album,
                    !mb.date.empty()?mb.date:t.date,score);
            }
        } else {
            for (int i=0;i<(int)pendingPaths_.size();i++)
                addRow(i,pendingPaths_[static_cast<size_t>(i)],L"Pending",L"",L"",L"",L"",L"");
        }
    }

    void StartScan() {
        if (pendingPaths_.empty()) return;
        wchar_t keyBuf[256]; GetWindowTextW(hEditKey_,keyBuf,256);
        std::wstring key=keyBuf;
        if (key.empty()){
            MessageBoxW(hwnd_,
                L"Please enter your AcoustID API key.\n\nGet a free key at https://acoustid.org/login",
                L"API Key Required",MB_ICONWARNING);
            return;
        }
        HKEY hk;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\MBTagger",0,nullptr,0,
            KEY_WRITE,nullptr,&hk,nullptr)==ERROR_SUCCESS){
            RegSetValueExW(hk,L"AcoustIDKey",0,REG_SZ,
                (const BYTE*)key.c_str(),(DWORD)((key.size()+1)*sizeof(wchar_t)));
            RegCloseKey(hk);
        }
        for (auto& l:lanes_) l={};
        scanner_=std::make_unique<Scanner>(key,hwnd_,WM_SCAN_EVENT);
        numWorkers_=scanner_->WorkerCount();
        lanes_.resize(numWorkers_);
        scanner_->Start(pendingPaths_);
        pendingPaths_.clear();
        EnableWindow(hBtnScan_,FALSE);
        EnableWindow(hBtnExport_,FALSE);
        RefreshListView();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void DoExport() {
        if (!scanner_||scanner_->Results().empty()) return;
        wchar_t buf[MAX_PATH]=L"musicbrainz_export.csv";
        OPENFILENAMEW ofn={};
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_;
        ofn.lpstrFilter=L"CSV Files\0*.csv\0All Files\0*.*\0";
        ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrDefExt=L"csv"; ofn.Flags=OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) return;
        if (CsvExporter::Export(buf,scanner_->Results()))
            MessageBoxW(hwnd_,(std::wstring(L"Saved to:\n")+buf).c_str(),
                L"Export Complete",MB_ICONINFORMATION);
        else
            MessageBoxW(hwnd_,L"Failed to write CSV file.",L"Error",MB_ICONERROR);
    }

    void ClearAll() {
        if (scanner_&&scanner_->IsRunning()) scanner_->Cancel();
        scanner_.reset();
        pendingPaths_.clear();
        selectedIdx_=-1;
        ListView_DeleteAllItems(hList_);
        for (auto& l:lanes_) l={};
        EnableWindow(hBtnScan_,FALSE);
        EnableWindow(hBtnExport_,FALSE);
        SetWindowTextW(hwnd_,L"MusicBrainz Tagger");
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void OnScanEvent(ScanEvent* ev) {
        if (!ev||!scanner_) return;
        std::unique_ptr<ScanEvent> guard(ev);
        const auto& results=scanner_->Results();

        if (ev->kind==ScanEvent::Kind::FileStarted||ev->kind==ScanEvent::Kind::FileProgress) {
            if (ev->fileIndex>=0&&ev->fileIndex<(int)results.size()) {
                const auto& r=*results[static_cast<size_t>(ev->fileIndex)];
                if (ev->workerId>=0&&ev->workerId<(int)lanes_.size()) {
                    std::wstring fn=r.filePath;
                    if (auto sl=fn.rfind(L'\\');sl!=std::wstring::npos) fn=fn.substr(sl+1);
                    auto& l=lanes_[static_cast<size_t>(ev->workerId)];
                    l.filename=fn; l.step=r.currentStep;
                    l.pct=r.progress.load(); l.active=true; l.done=false;
                }
            }
        } else if (ev->kind==ScanEvent::Kind::FileDone) {
            if (ev->workerId>=0&&ev->workerId<(int)lanes_.size()) {
                auto& l=lanes_[static_cast<size_t>(ev->workerId)];
                l.pct=100; l.active=false; l.done=true;
            }
            if (ev->total>0) {
                int pct=ev->totalDone*100/ev->total;
                wchar_t title[64]; swprintf_s(title,L"[%d%%] MusicBrainz Tagger",pct);
                SetWindowTextW(hwnd_,title);
            }
            // Auto-select latest matched file
            if (ev->fileIndex>=0&&ev->fileIndex<(int)results.size()) {
                const auto& r=*results[static_cast<size_t>(ev->fileIndex)];
                if (r.status==ScanStatus::Done) { selectedIdx_=ev->fileIndex; }
            }
        } else if (ev->kind==ScanEvent::Kind::AllDone) {
            EnableWindow(hBtnScan_,FALSE);
            EnableWindow(hBtnExport_,TRUE);
            SetWindowTextW(hwnd_,L"MusicBrainz Tagger \u2014 Done");
        }

        RefreshListView();
        RECT rcL={0,KEY_BAR_H,LEFT_W,rcLanes_.bottom};
        InvalidateRect(hwnd_,&rcL,FALSE);
    }

    LRESULT OnNotify(NMHDR* nm) {
        if (nm->hwndFrom==hList_&&(nm->code==NM_CLICK||nm->code==LVN_ITEMCHANGED)) {
            int sel=ListView_GetNextItem(hList_,-1,LVNI_SELECTED);
            if (sel!=selectedIdx_){ selectedIdx_=sel; InvalidateRect(hwnd_,nullptr,FALSE); }
        }
        return 0;
    }

    void OnDropFiles(HDROP hDrop) {
        UINT n=DragQueryFileW(hDrop,0xFFFFFFFF,nullptr,0);
        std::vector<std::wstring> paths;
        for (UINT i=0;i<n;i++){
            wchar_t buf[MAX_PATH*2];
            DragQueryFileW(hDrop,i,buf,MAX_PATH*2);
            paths.push_back(buf);
        }
        DragFinish(hDrop);
        AddPaths(std::move(paths));
    }
};
