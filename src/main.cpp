#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <ole2.h>
#include <imm.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.UI.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Pdf;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Graphics::Imaging;
using namespace std::chrono_literals;

constexpr wchar_t APP_NAME[] = L"包子PDF";
constexpr UINT WM_RESULT = WM_APP + 1;
constexpr uint64_t MAX_PIXELS = 12'000'000;
enum Command { Open=100, Prev, Next, PageEdit, ZoomOut, ZoomIn, FitPage, FitWidth, Rotate, Full, Help, CloseDoc, Actual, GoPage, HelpDetails, EnableExcel, DisableExcel };
enum class Fit { Page, Width, Custom };
int RegisterExcel(bool enable);

struct Request {
    uint64_t generation=0, fileId=0;
    std::wstring path, password;
    std::shared_ptr<std::vector<uint8_t>> embedded;
    uint32_t page=0;
    int width=900, height=700, rotation=0;
    Fit fit=Fit::Page;
    double zoom=1;
};
struct Result {
    uint64_t generation=0;
    uint32_t count=0, page=0, width=0, height=0;
    double zoom=1;
    bool limited=false;
    HRESULT error=S_OK;
    std::wstring message;
    std::vector<uint8_t> pixels;
};

std::wstring LocalPath(std::wstring path) {
    if (path.empty() || PathIsURLW(path.c_str())) throw hresult_invalid_argument(L"请选择本机 PDF 文件。");
    path=std::filesystem::absolute(path).lexically_normal().wstring();
    if (path.rfind(L"\\\\",0)==0 || path.rfind(L"\\?\\",0)==0)
        throw hresult_invalid_argument(L"此离线阅读器只打开本机文件，请先将文件复制到本机。");
    if (path.size()<3 || path[1]!=L':' || GetDriveTypeW(path.substr(0,3).c_str())==DRIVE_REMOTE)
        throw hresult_invalid_argument(L"不支持网络路径，请先将文件复制到本机。");
    return path;
}

// A single engine lives on the render thread. Superseded async work is cancelled;
// no render queue, page cache, history, registry settings, or network client exists.
class Engine {
    PdfDocument document{nullptr};
    uint64_t fileId=UINT64_MAX;
    std::atomic<uint64_t>* latest;
    std::atomic<bool>* stop;
    template<class T> auto Wait(T operation, uint64_t generation) {
        while (operation.Status()==AsyncStatus::Started) {
            if ((stop && stop->load()) || (latest && latest->load()!=generation)) {
                operation.Cancel();
                throw hresult_canceled();
            }
            std::this_thread::sleep_for(20ms);
        }
        if ((stop && stop->load()) || (latest && latest->load()!=generation)) throw hresult_canceled();
        return operation.get();
    }
public:
    Engine(std::atomic<uint64_t>* l=nullptr,std::atomic<bool>* s=nullptr):latest(l),stop(s) {}
    void Clear() { document=nullptr; fileId=UINT64_MAX; }
    Result Render(const Request& q) {
        Result r; r.generation=q.generation;
        if (!document || fileId!=q.fileId) {
            Clear();
            if(q.embedded){
                InMemoryRandomAccessStream stream;
                DataWriter writer(stream);writer.WriteBytes(*q.embedded);Wait(writer.StoreAsync(),q.generation);writer.DetachStream();stream.Seek(0);
                document=Wait(PdfDocument::LoadFromStreamAsync(stream,q.password),q.generation);
            }else{
                auto file=Wait(StorageFile::GetFileFromPathAsync(LocalPath(q.path)),q.generation);
                document=Wait(PdfDocument::LoadFromFileAsync(file,q.password),q.generation);
            }
            fileId=q.fileId;
        }
        r.count=document.PageCount();
        if (!r.count) throw hresult_invalid_argument(L"这份 PDF 没有可显示的页面。");
        r.page=std::min(q.page,r.count-1);
        auto page=document.GetPage(r.page);
        struct PageCloser { PdfPage p; ~PageCloser(){ if(p) p.Close(); } } closer{page};
        const auto size=page.Size();
        if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width<=0 || size.Height<=0)
            throw hresult_invalid_argument(L"PDF 页面尺寸无效。");
        double sw=size.Width, sh=size.Height;
        if(q.rotation%2) std::swap(sw,sh);
        r.zoom=q.fit==Fit::Page ? std::min(q.width/sw,q.height/sh) : q.fit==Fit::Width ? q.width/sw : q.zoom;
        r.zoom=std::clamp(r.zoom,0.02,8.0);
        double scale=r.zoom;
        double maxScale=std::min({std::sqrt(double(MAX_PIXELS)/(sw*sh)),8192.0/sw,8192.0/sh});
        if (scale>maxScale) { scale=maxScale; r.zoom=scale; r.limited=true; }
        const uint32_t w=std::max(1u,uint32_t(std::floor(size.Width*scale)));
        const uint32_t h=std::max(1u,uint32_t(std::floor(size.Height*scale)));
        PdfPageRenderOptions options;
        options.DestinationWidth(w); options.DestinationHeight(h);
        options.BackgroundColor(Windows::UI::Color{255,255,255,255});
        options.BitmapEncoderId(BitmapEncoder::BmpEncoderId());
        InMemoryRandomAccessStream stream;
        Wait(page.RenderToStreamAsync(stream,options),q.generation);
        stream.Seek(0);
        auto decoder=Wait(BitmapDecoder::CreateAsync(stream),q.generation);
        BitmapTransform transform;
        transform.Rotation(static_cast<BitmapRotation>(q.rotation));
        auto provider=Wait(decoder.GetPixelDataAsync(BitmapPixelFormat::Bgra8,BitmapAlphaMode::Ignore,transform,
            ExifOrientationMode::IgnoreExifOrientation,ColorManagementMode::DoNotColorManage),q.generation);
        auto bytes=provider.DetachPixelData();
        r.width=decoder.PixelWidth(); r.height=decoder.PixelHeight();
        if(q.rotation%2) std::swap(r.width,r.height);
        if(uint64_t(r.width)*r.height>MAX_PIXELS || bytes.size()!=uint64_t(r.width)*r.height*4)
            throw hresult_error(E_FAIL,L"页面位图超出资源限制。");
        r.pixels.assign(bytes.begin(),bytes.end());
        return r;
    }
};

struct App {
    HWND window{},canvas{},pageEdit{},totalLabel{},zoomLabel{};
    HFONT font{},titleFont{},smallFont{};
    HBRUSH white=CreateSolidBrush(RGB(255,255,255)), background=CreateSolidBrush(RGB(232,236,241));
    std::vector<HWND> controls;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<Request> pending;
    std::atomic<uint64_t> latest{0};
    std::atomic<bool> stop{false};
    Request request;
    std::unique_ptr<Result> bitmap;
    uint32_t pageCount=0;
    int scrollX=0,scrollY=0,dpi=96;
    bool busy=false, fullscreen=false, dragging=false;
    bool oleMode=false;
    POINT dragStart{};
    int dragX=0,dragY=0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    std::wstring status=L"本地阅读 · 不保存历史";
    int Px(int n)const {return MulDiv(n,dpi,96);}
    void StartWorker() {
        worker=std::thread([this] {
            init_apartment(apartment_type::multi_threaded);
            {
                Engine engine(&latest,&stop);
                while(!stop) {
                    Request q;
                    { std::unique_lock<std::mutex> lock(mutex); condition.wait(lock,[this]{return stop || pending.has_value();});
                      if(stop) break; q=std::move(*pending); pending.reset(); }
                    if(q.path.empty()) { engine.Clear(); continue; }
                    auto result=std::make_unique<Result>(); result->generation=q.generation;
                    try { *result=engine.Render(q); }
                    catch(const hresult_canceled&) {continue;}
                    catch(const hresult_error& e) { result->error=e.code(); result->message=e.message().c_str(); }
                    catch(const std::exception&) { result->error=E_FAIL; result->message=L"文件无法读取或可用内存不足。"; }
                    if(!stop && latest==q.generation) {
                        if(PostMessageW(window,WM_RESULT,0,reinterpret_cast<LPARAM>(result.get()))) result.release();
                    }
                    if(!q.password.empty()) SecureZeroMemory(q.password.data(),q.password.size()*sizeof(wchar_t));
                }
            }
            uninit_apartment();
        });
    }
    void Shutdown() {
        stop=true; ++latest; condition.notify_one();
        if(worker.joinable()) worker.join();
        MSG msg; while(PeekMessageW(&msg,window,WM_RESULT,WM_RESULT,PM_REMOVE)) delete reinterpret_cast<Result*>(msg.lParam);
    }
    ~App(){Shutdown(); DeleteObject(font);DeleteObject(titleFont);DeleteObject(smallFont);DeleteObject(white);DeleteObject(background);}
    RECT View()const {RECT r{};GetClientRect(canvas,&r);return r;}
    void Queue(bool clear=false) {
        if(request.path.empty()) return;
        if(clear){bitmap.reset();scrollX=scrollY=0;}
        auto v=View(); request.width=std::max(1,int(v.right)-Px(48));request.height=std::max(1,int(v.bottom)-Px(48));
        request.generation=++latest;
        busy=true;status=L"正在渲染…";
        {std::lock_guard<std::mutex> lock(mutex);pending=request;} condition.notify_one();
        UpdateControls();InvalidateRect(canvas,nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);
    }
    void SetFonts() {
        if(font) DeleteObject(font);if(titleFont) DeleteObject(titleFont);if(smallFont) DeleteObject(smallFont);
        font=CreateFontW(-Px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        titleFont=CreateFontW(-Px(30),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        smallFont=CreateFontW(-Px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        for(auto c:controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }
    void UpdateControls() {
        bool loaded=pageCount>0;
        for(auto c:controls){int id=GetDlgCtrlID(c);if(id!=Open&&id!=Help&&id!=Full)EnableWindow(c,loaded);}
        EnableWindow(GetDlgItem(window,Prev),loaded&&request.page>0);
        EnableWindow(GetDlgItem(window,Next),loaded&&request.page+1<pageCount);
        SetWindowTextW(pageEdit,loaded?std::to_wstring(request.page+1).c_str():L"—");
        SetWindowTextW(totalLabel,(L"/ "+(loaded?std::to_wstring(pageCount):L"—")).c_str());
        SetWindowTextW(zoomLabel,bitmap?(std::to_wstring(int(std::round(bitmap->zoom*100)))+L"%").c_str():L"—");
    }
    void OpenFile(const std::wstring& path) {
        try { request.path=LocalPath(path); }
        catch(const hresult_error& e){MessageBoxW(window,e.message().c_str(),L"无法打开",MB_ICONINFORMATION);return;}
        request.embedded.reset();
        KillTimer(window,1); ++request.fileId;request.password.clear();request.page=0;request.rotation=0;request.fit=Fit::Page;
        pageCount=0;bitmap.reset();scrollX=scrollY=0;
        SetWindowTextW(window,(std::filesystem::path(request.path).filename().wstring()+L" — "+APP_NAME).c_str());Queue(true);
    }
    void OpenEmbedded(std::shared_ptr<std::vector<uint8_t>> bytes,const std::wstring& title) {
        KillTimer(window,2);KillTimer(window,1);request.embedded=std::move(bytes);request.path=L"Excel 内嵌 PDF";
        ++request.fileId;request.password.clear();request.page=0;request.rotation=0;request.fit=Fit::Page;pageCount=0;
        SetWindowTextW(window,(title+L" — "+APP_NAME).c_str());ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);Queue(true);
    }
    void ChooseFile() {
        std::vector<wchar_t> filename(32768);
        OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFilter=L"PDF 文档 (*.pdf)\0*.pdf\0所有文件\0*.*\0";
        o.lpstrFile=filename.data();o.nMaxFile=static_cast<DWORD>(filename.size());o.lpstrTitle=L"选择 PDF";
        o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_EXPLORER;
        if(GetOpenFileNameW(&o))OpenFile(filename.data());
    }
    void CloseFile() {
        KillTimer(window,1);++latest;request.path.clear();request.embedded.reset();request.password.clear();++request.fileId;pageCount=0;bitmap.reset();busy=false;
        {std::lock_guard<std::mutex> lock(mutex);pending=request;}condition.notify_one();
        status=L"本地阅读 · 不保存历史";SetWindowTextW(window,APP_NAME);UpdateControls();UpdateScroll();InvalidateRect(window,nullptr,TRUE);InvalidateRect(canvas,nullptr,FALSE);
    }
    void Navigate(int64_t p) {if(!pageCount)return;auto n=uint32_t(std::clamp<int64_t>(p,0,pageCount-1));if(n!=request.page){request.page=n;Queue(true);}SetFocus(canvas);}
    void Zoom(double factor) {if(!pageCount)return;double base=request.fit==Fit::Custom?request.zoom:(bitmap?bitmap->zoom:request.zoom);request.zoom=std::clamp(base*factor,0.1,8.0);request.fit=Fit::Custom;scrollX=scrollY=0;Queue();SetFocus(canvas);}
    void ToggleFullscreen() {
        if(!fullscreen){GetWindowPlacement(window,&placement);SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN);
            MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi);
            fullscreen=true;SetWindowPos(window,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}
        else{fullscreen=false;SetWindowLongPtrW(window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN);SetWindowPlacement(window,&placement);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}
    }
    void Command(int cmd) {
        switch(cmd){
        case Open:ChooseFile();break;
        case Prev:Navigate(int64_t(request.page)-1);break;
        case Next:Navigate(int64_t(request.page)+1);break;
        case GoPage:SetFocus(pageEdit);SendMessageW(pageEdit,EM_SETSEL,0,-1);break;
        case ZoomIn:Zoom(1.25);break;case ZoomOut:Zoom(0.8);break;
        case FitPage:case FitWidth:case Actual:if(pageCount){request.fit=cmd==FitPage?Fit::Page:cmd==FitWidth?Fit::Width:Fit::Custom;request.zoom=1;scrollX=scrollY=0;Queue();SetFocus(canvas);}break;
        case Rotate:if(pageCount){request.rotation=(request.rotation+1)%4;Queue(true);}SetFocus(canvas);break;
        case Full:ToggleFullscreen();SetFocus(canvas);break;
        case CloseDoc:if(oleMode)PostMessageW(window,WM_CLOSE,0,0);else CloseFile();break;
        case Help:{HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,HelpDetails,L"使用帮助");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,EnableExcel,L"启用 Excel 内嵌 PDF");AppendMenuW(menu,MF_STRING,DisableExcel,L"恢复原有 Excel PDF 关联");RECT r{};GetWindowRect(GetDlgItem(window,Help),&r);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,r.left,r.bottom,0,window,nullptr);DestroyMenu(menu);if(selected)Command(selected);break;}
        case EnableExcel:case DisableExcel:if(RegisterExcel(cmd==EnableExcel)==0)MessageBoxW(window,cmd==EnableExcel?L"已为当前用户启用。\n\n请关闭 Adobe 和 Excel，再重新打开工作簿。\n之后双击已有的 Adobe 类型 PDF 图标即可阅读。\n\n请将本程序保留在当前位置；移动后需重新启用。":L"已恢复启用前的 Excel PDF 对象关联。\n请重新打开 Excel。",L"Excel 内嵌 PDF",MB_OK);break;
        case HelpDetails:MessageBoxW(window,L"包子PDF  1.0\n\n打开文件  Ctrl+O   ·   支持拖入 PDF\n翻页  ← / → 或 PageUp / PageDown\n首页 / 末页  Home / End\n跳转页码  Ctrl+G，输入后按 Enter\n缩放  Ctrl+滚轮 或 + / −\n适合整页  Ctrl+0   ·   适合宽度  Ctrl+1\n原始大小  Ctrl+2   ·   顺时针旋转  R\n全屏  F11   ·   退出全屏  Esc\n关闭文档  Ctrl+W   ·   退出程序  Alt+F4\n滚轮上下移动；拖动页面、滚动条或上下方向键平移；Shift+滚轮横向滚动。\n\n使用 Windows 系统 PDF 能力。只读、无账号、无联网代码、\n无更新器、无历史记录、无后台服务。\n支持已验收的 Adobe 类型 Excel 内嵌 PDF（需注册）。\n本版不提供文字选择、搜索、目录或 PDF 编辑。",L"使用帮助",MB_OK);break;
        }
    }
    void UpdateScroll() {
        auto v=View();int w=bitmap?int(bitmap->width)+Px(48):0,h=bitmap?int(bitmap->height)+Px(48):0;
        scrollX=std::clamp(scrollX,0,std::max(0,w-int(v.right)));scrollY=std::clamp(scrollY,0,std::max(0,h-int(v.bottom)));
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMin=0;si.nMax=std::max(0,w-1);si.nPage=v.right;si.nPos=scrollX;SetScrollInfo(canvas,SB_HORZ,&si,TRUE);
        si.nMax=std::max(0,h-1);si.nPage=v.bottom;si.nPos=scrollY;SetScrollInfo(canvas,SB_VERT,&si,TRUE);
    }
    void Scroll(int dx,int dy) {scrollX+=dx;scrollY+=dy;UpdateScroll();InvalidateRect(canvas,nullptr,FALSE);}
    void Layout() {
        RECT c{};GetClientRect(window,&c);int x=Px(16),y=Px(12),h=Px(30);
        const std::pair<int,int> specs[]={{Open,82},{Prev,36},{Next,36},{PageEdit,52},{500,64},{ZoomOut,34},{501,60},{ZoomIn,34},{FitPage,76},{FitWidth,76},{Rotate,56},{Full,56},{Help,48}};
        for(auto s:specs){MoveWindow(GetDlgItem(window,s.first),x,y,Px(s.second),h,TRUE);x+=Px(s.second+6);}
        MoveWindow(canvas,0,Px(54),c.right,std::max(0,int(c.bottom)-Px(84)),TRUE);UpdateScroll();
        InvalidateRect(window,nullptr,FALSE);
    }
};
App* app=nullptr;
#include "ole_compat.h"

INT_PTR CALLBACK PasswordProc(HWND dlg,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_INITDIALOG){SetWindowLongPtrW(dlg,DWLP_USER,lp);SendDlgItemMessageW(dlg,1001,EM_SETLIMITTEXT,255,0);return TRUE;}
    if(msg==WM_COMMAND){if(LOWORD(wp)==IDOK){wchar_t pw[256]{};GetDlgItemTextW(dlg,1001,pw,256);*reinterpret_cast<std::wstring*>(GetWindowLongPtrW(dlg,DWLP_USER))=pw;SecureZeroMemory(pw,sizeof(pw));EndDialog(dlg,IDOK);return TRUE;}if(LOWORD(wp)==IDCANCEL){EndDialog(dlg,IDCANCEL);return TRUE;}}
    return FALSE;
}
void DrawLabel(HDC dc,const std::wstring& text,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),-1,&r,flags);
}
LRESULT CALLBACK CanvasProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(!app)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);auto v=app->View();
        HDC mem=CreateCompatibleDC(dc);HBITMAP buffer=CreateCompatibleBitmap(dc,std::max(1L,v.right),std::max(1L,v.bottom));auto old=SelectObject(mem,buffer);FillRect(mem,&v,app->background);
        if(app->bitmap){auto& b=*app->bitmap;int x=std::max(app->Px(24),(int(v.right)-int(b.width))/2)-app->scrollX;
            int y=std::max(app->Px(24),(int(v.bottom)-int(b.height))/2)-app->scrollY;
            RECT shadow{x+3,y+4,x+int(b.width)+3,y+int(b.height)+4};FillRect(mem,&shadow,GetSysColorBrush(COLOR_3DSHADOW));
            BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=b.width;bi.bmiHeader.biHeight=-LONG(b.height);bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
            SetDIBitsToDevice(mem,x,y,b.width,b.height,0,0,0,b.height,b.pixels.data(),&bi,DIB_RGB_COLORS);
        }else{int cy=int(v.bottom)/2;RECT r{0,cy-app->Px(100),v.right,cy-app->Px(45)};
            DrawLabel(mem,APP_NAME,r,app->titleFont,RGB(30,48,65),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            r.top=cy-app->Px(20);r.bottom=cy+app->Px(20);DrawLabel(mem,app->busy?L"正在打开文档…":L"把 PDF 拖到这里，开始阅读",r,app->font,RGB(80,95,110),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            r.top=cy+app->Px(32);r.bottom=cy+app->Px(64);DrawLabel(mem,L"Ctrl + O  打开文件",r,app->font,RGB(30,105,136),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            r.top=cy+app->Px(95);r.bottom=cy+app->Px(120);DrawLabel(mem,L"轻巧 · 只读 · 离线",r,app->smallFont,RGB(112,126,139),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        BitBlt(dc,0,0,v.right,v.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(buffer);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
    case WM_LBUTTONDOWN:SetFocus(hwnd);if(app->bitmap){app->dragging=true;app->dragStart={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};app->dragX=app->scrollX;app->dragY=app->scrollY;SetCapture(hwnd);SetCursor(LoadCursorW(nullptr,IDC_SIZEALL));}return 0;
    case WM_MOUSEMOVE:if(app->dragging){app->scrollX=app->dragX+app->dragStart.x-GET_X_LPARAM(lp);app->scrollY=app->dragY+app->dragStart.y-GET_Y_LPARAM(lp);app->Scroll(0,0);}return 0;
    case WM_LBUTTONUP:app->dragging=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:app->dragging=false;return 0;
    case WM_MOUSEWHEEL:{int delta=GET_WHEEL_DELTA_WPARAM(wp);
        if(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL){if(delta)app->Zoom(std::pow(1.25,double(delta)/WHEEL_DELTA));}
        else if(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)app->Scroll(-delta,0);
        else app->Scroll(0,-delta);
        return 0;}
    case WM_HSCROLL:case WM_VSCROLL:{int bar=msg==WM_HSCROLL?SB_HORZ:SB_VERT;SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,bar,&si);int pos=si.nPos;
        switch(LOWORD(wp)){case SB_LINEUP:pos-=app->Px(36);break;case SB_LINEDOWN:pos+=app->Px(36);break;case SB_PAGEUP:pos-=int(si.nPage)*9/10;break;case SB_PAGEDOWN:pos+=int(si.nPage)*9/10;break;case SB_THUMBTRACK:pos=si.nTrackPos;break;case SB_TOP:pos=0;break;case SB_BOTTOM:pos=si.nMax;break;}
        if(bar==SB_HORZ)app->scrollX=pos;else app->scrollY=pos;app->Scroll(0,0);return 0;}
    case WM_DROPFILES:SendMessageW(app->window,msg,wp,lp);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE:{app->window=hwnd;app->dpi=GetDpiForWindow(hwnd);
        ImmAssociateContext(hwnd,nullptr);
        auto create=[&](int id,const wchar_t* cls,const wchar_t* text,DWORD style){HWND c=CreateWindowExW(id==PageEdit?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);app->controls.push_back(c);return c;};
        create(Open,L"BUTTON",L"打开…",BS_PUSHBUTTON|WS_TABSTOP);create(Prev,L"BUTTON",L"‹",BS_PUSHBUTTON|WS_TABSTOP);create(Next,L"BUTTON",L"›",BS_PUSHBUTTON|WS_TABSTOP);
        app->pageEdit=create(PageEdit,L"EDIT",L"",ES_NUMBER|ES_CENTER|ES_AUTOHSCROLL|WS_TABSTOP);SendMessageW(app->pageEdit,EM_SETLIMITTEXT,9,0);
        app->totalLabel=create(500,L"STATIC",L"/ —",SS_CENTERIMAGE);
        create(ZoomOut,L"BUTTON",L"−",BS_PUSHBUTTON|WS_TABSTOP);app->zoomLabel=create(501,L"STATIC",L"—",SS_CENTER|SS_CENTERIMAGE);create(ZoomIn,L"BUTTON",L"+",BS_PUSHBUTTON|WS_TABSTOP);
        create(FitPage,L"BUTTON",L"整页",BS_PUSHBUTTON|WS_TABSTOP);create(FitWidth,L"BUTTON",L"宽度",BS_PUSHBUTTON|WS_TABSTOP);create(Rotate,L"BUTTON",L"旋转",BS_PUSHBUTTON|WS_TABSTOP);create(Full,L"BUTTON",L"全屏",BS_PUSHBUTTON|WS_TABSTOP);create(Help,L"BUTTON",L"帮助",BS_PUSHBUTTON|WS_TABSTOP);
        app->canvas=CreateWindowExW(0,L"QingYueCanvas",L"PDF 页面",WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_VSCROLL|WS_TABSTOP,0,0,1,1,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        ImmAssociateContext(app->canvas,nullptr);ImmAssociateContext(app->pageEdit,nullptr);
        DragAcceptFiles(hwnd,TRUE);DragAcceptFiles(app->canvas,TRUE);app->SetFonts();app->Layout();app->UpdateControls();app->StartWorker();return 0;}
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)app->Command(LOWORD(wp));return 0;
    case WM_MOUSEWHEEL:if(app&&app->canvas)return SendMessageW(app->canvas,msg,wp,lp);return 0;
    case WM_SIZE:if(app&&app->canvas){app->Layout();if(wp!=SIZE_MINIMIZED&&!app->request.path.empty())SetTimer(hwnd,1,150,nullptr);}return 0;
    case WM_TIMER:if(wp==2){KillTimer(hwnd,2);if(!IsWindowVisible(hwnd))PostMessageW(hwnd,WM_CLOSE,0,0);}else{KillTimer(hwnd,1);if(!app->request.path.empty())app->Queue();}return 0;
    case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={app?app->Px(930):930,app?app->Px(420):420};return 0;}
    case WM_DPICHANGED:app->dpi=HIWORD(wp);app->SetFonts();{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetBkColor(reinterpret_cast<HDC>(wp),RGB(255,255,255));SetTextColor(reinterpret_cast<HDC>(wp),RGB(40,52,65));return reinterpret_cast<LRESULT>(app->white);
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);FillRect(dc,&r,app->white);r.top=r.bottom-app->Px(30);r.left=app->Px(16);
        DrawLabel(dc,app->status,r,app->smallFont,RGB(81,100,112));r.right-=app->Px(16);DrawLabel(dc,L"F11 全屏   ·   Ctrl+O 打开",r,app->smallFont,RGB(100,116,129),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);EndPaint(hwnd,&ps);return 0;}
    case WM_DROPFILES:{auto drop=reinterpret_cast<HDROP>(wp);UINT n=DragQueryFileW(drop,0,nullptr,0);std::wstring path(n+1,L'\0');DragQueryFileW(drop,0,path.data(),n+1);DragFinish(drop);path.resize(n);if(n)app->OpenFile(path);return 0;}
    case WM_RESULT:{std::unique_ptr<Result> r(reinterpret_cast<Result*>(lp));if(r->generation!=app->latest)return 0;app->busy=false;
        if(FAILED(r->error)){
            if(r->error==HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD)){
                std::wstring pw;if(DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),hwnd,PasswordProc,reinterpret_cast<LPARAM>(&pw))==IDOK){app->request.password=std::move(pw);++app->request.fileId;app->Queue(true);return 0;}
            }else{std::wostringstream out;out<<L"无法显示这份 PDF。请确认文件完整，且是受支持的 PDF。\n\n"<<r->message<<L"\n错误码：0x"<<std::hex<<std::uppercase<<uint32_t(r->error);MessageBoxW(hwnd,out.str().c_str(),L"打开 / 渲染失败",MB_ICONINFORMATION);}
            app->CloseFile();return 0;
        }
        if(!app->request.password.empty()){SecureZeroMemory(app->request.password.data(),app->request.password.size()*sizeof(wchar_t));app->request.password.clear();}
        app->pageCount=r->count;app->request.page=r->page;app->request.zoom=r->zoom;
        app->status=L"第 "+std::to_wstring(r->page+1)+L" / "+std::to_wstring(r->count)+L" 页   ·   只读"+(r->limited?L"   ·   已限制位图尺寸以控制内存":L"");
        app->bitmap=std::move(r);app->UpdateScroll();app->UpdateControls();InvalidateRect(app->canvas,nullptr,FALSE);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_CLOSE:CloseOleObjects();app->Shutdown();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int SelfTest(const std::wstring& directory,const std::wstring& output) {
    std::ofstream log{std::filesystem::path(output)};if(!log)return 3;
    int failures=0;uint64_t serial=0;Engine engine;
    auto check=[&](bool pass,const std::string& label){log<<(pass?"PASS":"FAIL")<<"\t"<<label<<"\n";log.flush();if(!pass)++failures;};
    auto render=[&](const wchar_t* name,uint32_t page,int rot,double zoom,const wchar_t* password=L""){
        Request q;q.fileId=++serial;q.path=(std::filesystem::path(directory)/name).wstring();q.page=page;q.rotation=rot;q.fit=Fit::Custom;q.zoom=zoom;q.password=password;return engine.Render(q);};
    try {
        auto r=render(L"sample.pdf",0,0,1);check(r.count==4&&r.width>0&&!r.pixels.empty(),"open four-page sample");
        uint64_t dark=0;for(size_t i=0;i<r.pixels.size();i+=4)if(r.pixels[i]<180)++dark;
        check(dark>1000,"render contains visible content");
        auto rotated=render(L"sample.pdf",0,1,1);check(rotated.width==r.height&&rotated.height==r.width,"90-degree rotation swaps dimensions");
        auto half=render(L"sample.pdf",0,0,0.5);check(std::abs(int(half.width)*2-int(r.width))<=1,"50-percent zoom");
        auto high=render(L"sample.pdf",0,0,8);check(high.limited&&uint64_t(high.width)*high.height<=MAX_PIXELS,"800-percent zoom memory cap");
        auto landscape=render(L"sample.pdf",1,0,1);check(landscape.width>landscape.height,"landscape page");
        check(render(L"sample.pdf",2,0,1).pixels.size()>0,"scanned-image page");
        check(render(L"sample.pdf",3,0,1).pixels.size()>0,"intrinsic rotated page");
        check(render(L"large.pdf",249,0,1).count==250,"jump to page 250 of large document");
        check(render(L"encrypted.pdf",0,0,1,L"reader-test").count==4,"password-protected PDF");
        bool wrong=false;try{render(L"encrypted.pdf",0,0,1,L"wrong");}catch(const hresult_error& e){wrong=e.code()==HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD);log<<"INFO\tpassword HRESULT=0x"<<std::hex<<uint32_t(e.code())<<std::dec<<"\n";}check(wrong,"wrong password rejected with recognized code");
        bool bad=false;try{render(L"broken.pdf",0,0,1);}catch(...){bad=true;}check(bad,"corrupt PDF rejected");
        bool missing=false;try{render(L"missing.pdf",0,0,1);}catch(...){missing=true;}check(missing,"missing file rejected");
        check(render(L"中文 路径.pdf",0,0,1).count==4,"Unicode and space in filename");
        bool network=false;try{LocalPath(L"\\\\server\\share\\test.pdf");}catch(...){network=true;}check(network,"UNC path rejected before opening");
        auto start=std::chrono::steady_clock::now();Request q;q.path=(std::filesystem::path(directory)/L"large.pdf").wstring();q.fileId=++serial;q.fit=Fit::Page;
        for(uint32_t p=0;p<25;++p){q.page=(p*11)%250;auto page=engine.Render(q);if(page.page!=q.page)throw std::runtime_error("page mismatch");}
        check(true,"25 nonsequential page renders");log<<"INFO\t25 renders ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";
        engine.Clear();check(render(L"sample.pdf",0,0,1).count==4,"reopen after errors and document close");
    }catch(const hresult_error& e){log<<"FAIL\tUnexpected HRESULT 0x"<<std::hex<<uint32_t(e.code())<<"\n";++failures;}
    catch(const std::exception& e){log<<"FAIL\t"<<e.what()<<"\n";++failures;}
    log<<"RESULT\t"<<(failures?"FAIL":"PASS")<<"\tfailures="<<failures<<"\n";return failures?1:0;
}
int VerifyPdf(const std::wstring& path,const std::wstring& output){
    std::ofstream log{std::filesystem::path(output)};if(!log)return 3;
    try{Engine engine;Request q;q.fileId=1;q.path=path;q.fit=Fit::Page;
        auto start=std::chrono::steady_clock::now();auto first=engine.Render(q);uint32_t total=first.count;
        log<<"PASS\tpage 1 / "<<total<<"\n";
        for(uint32_t p=1;p<total;++p){q.page=p;auto r=engine.Render(q);if(r.page!=p||r.pixels.empty())return 2;log<<"PASS\tpage "<<p+1<<" / "<<total<<"\n";}
        log<<"RESULT\tPASS\tpages="<<total<<"\tms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";return 0;
    }catch(const hresult_error& e){log<<"FAIL\tHRESULT=0x"<<std::hex<<uint32_t(e.code())<<"\n";return 1;}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    try {
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        std::vector<std::wstring> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);LocalFree(argv);
        if(args.size()==3&&args[0]==L"--self-test"){init_apartment(apartment_type::multi_threaded);return SelfTest(args[1],args[2]);}
        if(args.size()==3&&args[0]==L"--verify-pdf"){init_apartment(apartment_type::multi_threaded);return VerifyPdf(args[1],args[2]);}
        if(args.size()==1&&(args[0]==L"--enable-excel"||args[0]==L"--disable-excel"))return RegisterExcel(args[0]==L"--enable-excel");
        check_hresult(OleInitialize(nullptr));
        INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
        App state;app=&state;state.oleMode=std::any_of(args.begin(),args.end(),[](const auto& a){return _wcsicmp(a.c_str(),L"-Embedding")==0||a==L"--ole";});
        WNDCLASSW canvas{};canvas.hInstance=instance;canvas.lpfnWndProc=CanvasProc;canvas.lpszClassName=L"QingYueCanvas";canvas.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&canvas);
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=MainProc;wc.lpszClassName=L"QingYuePDF";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&wc);
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,APP_NAME,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1120,820,nullptr,nullptr,instance,nullptr);
        if(!hwnd)return 2;
        DWORD oleCookie=0;
        if(state.oleMode){oleCookie=StartOleServer();SetTimer(hwnd,2,30000,nullptr);}
        else{ShowWindow(hwnd,show);UpdateWindow(hwnd);SetFocus(state.canvas);if(!args.empty())state.OpenFile(args[0]);}
        MSG msg;
        while(GetMessageW(&msg,nullptr,0,0)>0){
            bool handled=false;
            if(msg.message==WM_KEYDOWN){bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;bool edit=GetFocus()==state.pageEdit;int key=int(msg.wParam);
                if(ctrl){switch(key){case 'O':state.Command(Open);handled=true;break;case 'W':state.Command(CloseDoc);handled=true;break;case 'G':state.Command(GoPage);handled=true;break;case '0':state.Command(FitPage);handled=true;break;case '1':state.Command(FitWidth);handled=true;break;case '2':state.Command(Actual);handled=true;break;}}
                if(!handled&&key==VK_F11){state.ToggleFullscreen();handled=true;}
                if(!handled&&key==VK_ESCAPE){if(state.fullscreen)state.ToggleFullscreen();SetFocus(state.canvas);handled=true;}
                if(!handled&&edit&&key==VK_RETURN){wchar_t buf[32]{};GetWindowTextW(state.pageEdit,buf,32);auto page=_wtoi64(buf);if(page>=1&&page<=state.pageCount)state.Navigate(page-1);else{MessageBeep(MB_ICONINFORMATION);state.UpdateControls();SendMessageW(state.pageEdit,EM_SETSEL,0,-1);}handled=true;}
                if(!handled&&!edit){switch(key){case VK_LEFT:case VK_PRIOR:state.Command(Prev);handled=true;break;case VK_RIGHT:case VK_NEXT:state.Command(Next);handled=true;break;case VK_HOME:state.Navigate(0);handled=true;break;case VK_END:state.Navigate(INT64_MAX);handled=true;break;case VK_ADD:case VK_OEM_PLUS:state.Command(ZoomIn);handled=true;break;case VK_SUBTRACT:case VK_OEM_MINUS:state.Command(ZoomOut);handled=true;break;case 'R':state.Command(Rotate);handled=true;break;case VK_UP:state.Scroll(0,-state.Px(60));handled=true;break;case VK_DOWN:state.Scroll(0,state.Px(60));handled=true;break;case VK_SPACE:if(state.bitmap&&int(state.bitmap->height)+state.Px(48)>state.View().bottom)state.Scroll(0,int(state.View().bottom)*9/10);else state.Command(Next);handled=true;break;}}
            }
            if(!handled){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB&&IsDialogMessageW(hwnd,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(oleCookie)CoRevokeClassObject(oleCookie);app=nullptr;return int(msg.wParam);
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"包子PDF 启动失败",MB_ICONERROR);return 2;}
    catch(...){MessageBoxW(nullptr,L"程序发生错误，已停止。",APP_NAME,MB_ICONERROR);return 2;}
}
