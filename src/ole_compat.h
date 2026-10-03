// Read-only local OLE server for the Adobe document CLSID found in the acceptance
// workbook. It preserves the existing storage and never rewrites a workbook.
// UI/COM calls run on the main STA; only rendering runs on the worker MTA.
#pragma once
#include <functional>
inline void OleTrace(const std::string&){}

const CLSID PDF_CLSID={0xb801ca65,0xa1fc,0x11d0,{0x85,0xad,0x44,0x45,0x53,0x54,0,0}};
constexpr wchar_t PDF_CLSID_TEXT[]=L"{B801CA65-A1FC-11D0-85AD-444553540000}";
class PdfOleObject;
std::vector<PdfOleObject*> oleObjects;

HRESULT LoadPdfStorage(IStorage* storage,std::shared_ptr<std::vector<uint8_t>>& bytes) noexcept {
    if(!storage)return E_POINTER;
    try {
        com_ptr<IStream> stream;
        HRESULT hr=storage->OpenStream(L"CONTENTS",nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,stream.put());
        if(FAILED(hr))return hr;
        STATSTG stat{};check_hresult(stream->Stat(&stat,STATFLAG_NONAME));
        if(stat.cbSize.QuadPart<8||stat.cbSize.QuadPart>512ull*1024*1024)return STG_E_INVALIDHEADER;
        auto data=std::make_shared<std::vector<uint8_t>>(size_t(stat.cbSize.QuadPart));
        ULONG read=0;check_hresult(stream->Read(data->data(),ULONG(data->size()),&read));
        if(read!=data->size()||memcmp(data->data(),"%PDF-",5)!=0)return STG_E_INVALIDHEADER;
        bytes=std::move(data);return S_OK;
    }catch(...){return to_hresult();}
}

class PdfOleObject final:public IOleObject,public IPersistStorage,public IPersistFile,public IDataObject {
    ULONG refs=1;
    com_ptr<IOleClientSite> site;
    com_ptr<IOleAdviseHolder> advise;
    com_ptr<IDataAdviseHolder> dataAdvise;
    com_ptr<IStorage> backing;
    std::shared_ptr<std::vector<uint8_t>> pdf;
    std::wstring filename,title=L"Excel 内嵌 PDF";
    SIZEL extent{2540,2540};
public:
    PdfOleObject(){CreateOleAdviseHolder(advise.put());CreateDataAdviseHolder(dataAdvise.put());oleObjects.push_back(this);}
    ~PdfOleObject(){oleObjects.erase(std::remove(oleObjects.begin(),oleObjects.end(),this),oleObjects.end());}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override {
        wchar_t iidText[64]{};StringFromGUID2(iid,iidText,64);OleTrace("QI "+to_string(iidText));
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_IOleObject)*out=static_cast<IOleObject*>(this);
        else if(iid==IID_IPersist||iid==IID_IPersistStorage)*out=static_cast<IPersistStorage*>(this);
        else if(iid==IID_IPersistFile)*out=static_cast<IPersistFile*>(this);
        else if(iid==IID_IDataObject)*out=static_cast<IDataObject*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetClientSite(IOleClientSite* value)override{OleTrace("SetClientSite");site.copy_from(value);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetClientSite(IOleClientSite** out)override{if(!out)return E_POINTER;*out=site.get();if(*out)(*out)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetHostNames(LPCOLESTR,LPCOLESTR name)override{if(name&&*name)title=std::wstring(name)+L" · 内嵌 PDF";return S_OK;}
    HRESULT STDMETHODCALLTYPE Close(DWORD)override{OleTrace("Close");if(advise)advise->SendOnClose();site=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetMoniker(DWORD,IMoniker*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD assign,DWORD which,IMoniker** out)override{OleTrace("GetMoniker");if(!out)return E_POINTER;*out=nullptr;return site?site->GetMoniker(assign,which,out):E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE InitFromData(IDataObject*,BOOL,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetClipboardData(DWORD,IDataObject** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DoVerb(LONG verb,LPMSG,IOleClientSite* active,LONG,HWND,LPCRECT)override{
        OleTrace("DoVerb "+std::to_string(verb));
        if(verb==OLEIVERB_HIDE)return S_OK;
        if(verb!=OLEIVERB_PRIMARY&&verb!=OLEIVERB_SHOW&&verb!=OLEIVERB_OPEN)return OLEOBJ_S_INVALIDVERB;
        try{if(active)site.copy_from(active);if(pdf)app->OpenEmbedded(pdf,title);else if(!filename.empty()){KillTimer(app->window,2);app->OpenFile(filename);ShowWindow(app->window,SW_SHOWNORMAL);SetForegroundWindow(app->window);}else return OLE_E_BLANK;
            if(site)OleTrace("OnShowWindow result "+std::to_string(site->OnShowWindow(TRUE)));OleTrace("DoVerb S_OK");return S_OK;}catch(...){OleTrace("DoVerb exception");return to_hresult();}
    }
    HRESULT STDMETHODCALLTYPE EnumVerbs(IEnumOLEVERB** out)override{OleTrace("EnumVerbs");return OleRegEnumVerbs(PDF_CLSID,out);}
    HRESULT STDMETHODCALLTYPE Update()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE IsUpToDate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetUserClassID(CLSID* out)override{return GetClassID(out);}
    HRESULT STDMETHODCALLTYPE GetUserType(DWORD,LPOLESTR* out)override{
        if(!out)return E_POINTER;constexpr wchar_t text[]=L"Adobe PDF（包子PDF只读）";
        *out=static_cast<LPOLESTR>(CoTaskMemAlloc(sizeof(text)));if(!*out)return E_OUTOFMEMORY;memcpy(*out,text,sizeof(text));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetExtent(DWORD,SIZEL* size)override{if(!size)return E_POINTER;extent=*size;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD,SIZEL* size)override{if(!size)return E_POINTER;*size=extent;return S_OK;}
    HRESULT STDMETHODCALLTYPE Advise(IAdviseSink* sink,DWORD* connection)override{return advise?advise->Advise(sink,connection):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD connection)override{return advise?advise->Unadvise(connection):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA** out)override{return advise?advise->EnumAdvise(out):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetMiscStatus(DWORD,DWORD* out)override{OleTrace("GetMiscStatus");if(!out)return E_POINTER;*out=OLEMISC_CANTLINKINSIDE;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetColorScheme(LOGPALETTE*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* out)override{OleTrace("GetClassID");if(!out)return E_POINTER;*out=PDF_CLSID;return S_OK;}
    HRESULT STDMETHODCALLTYPE IsDirty()override{return S_FALSE;}
    HRESULT STDMETHODCALLTYPE InitNew(IStorage*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Load(IStorage* storage)override{OleTrace("Load");auto hr=LoadPdfStorage(storage,pdf);if(SUCCEEDED(hr))backing.copy_from(storage);return hr;}
    // No edits exist. The container keeps its original streams, CLSID and preview.
    HRESULT STDMETHODCALLTYPE Save(IStorage* target,BOOL same)override{if(same)return S_OK;if(!target||!backing)return STG_E_INVALIDPOINTER;auto hr=backing->CopyTo(0,nullptr,nullptr,target);if(FAILED(hr))return hr;return WriteClassStg(target,PDF_CLSID);}
    HRESULT STDMETHODCALLTYPE SaveCompleted(IStorage* storage)override{if(storage)backing.copy_from(storage);return S_OK;}
    HRESULT STDMETHODCALLTYPE HandsOffStorage()override{backing=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE Load(LPCOLESTR path,DWORD)override{try{filename=LocalPath(path?path:L"");return S_OK;}catch(...){return to_hresult();}}
    HRESULT STDMETHODCALLTYPE Save(LPCOLESTR,BOOL)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SaveCompleted(LPCOLESTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurFile(LPOLESTR* out)override{if(!out)return E_POINTER;*out=nullptr;return S_FALSE;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM*)override{OleTrace("GetData format="+std::to_string(f?f->cfFormat:0));return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{OleTrace("GetDataHere");return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*)override{OleTrace("QueryGetData");return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* out)override{if(out)out->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC** out)override{OleTrace("EnumFormatEtc");if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* format,DWORD flags,IAdviseSink* sink,DWORD* connection)override{OleTrace("DAdvise");return dataAdvise?dataAdvise->Advise(this,format,flags,sink,connection):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection)override{return dataAdvise?dataAdvise->Unadvise(connection):OLE_E_NOCONNECTION;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** out)override{return dataAdvise?dataAdvise->EnumAdvise(out):E_OUTOFMEMORY;}
    void Disconnect(){if(advise)advise->SendOnClose();if(site)site->OnShowWindow(FALSE);site=nullptr;CoDisconnectObject(static_cast<IOleObject*>(this),0);}
};
class PdfClassFactory final:public IClassFactory {
    ULONG refs=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out)override{if(outer)return CLASS_E_NOAGGREGATION;try{auto obj=new PdfOleObject;auto hr=obj->QueryInterface(iid,out);obj->Release();return hr;}catch(...){return to_hresult();}}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL)override{return S_OK;}
};
DWORD StartOleServer(){auto factory=new PdfClassFactory;DWORD cookie=0;HRESULT hr=CoRegisterClassObject(PDF_CLSID,factory,CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie);factory->Release();check_hresult(hr);return cookie;}
void CloseOleObjects(){auto copy=oleObjects;for(auto obj:copy)obj->AddRef();for(auto obj:copy){obj->Disconnect();obj->Release();}}

struct RegKey{HKEY key=nullptr;~RegKey(){if(key)RegCloseKey(key);}operator HKEY()const{return key;}};
void RegCheck(LSTATUS result){if(result!=ERROR_SUCCESS)throw hresult_error(HRESULT_FROM_WIN32(result));}
void RegString(HKEY parent,const wchar_t* subkey,const wchar_t* name,const std::wstring& value){RegKey k;RegCheck(RegCreateKeyExW(parent,subkey,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&k.key,nullptr));RegCheck(RegSetValueExW(k,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*2)));}
void RegisterExcelView(bool enable,REGSAM view,const wchar_t* backupName,const std::wstring& exe) {
    const std::wstring cls=L"Software\\Classes\\CLSID\\"+std::wstring(PDF_CLSID_TEXT);
    const std::wstring backup=L"Software\\BaoziPDF\\OleBackup\\"+std::wstring(backupName);
    RegKey user;RegCheck(RegOpenKeyExW(HKEY_CURRENT_USER,L"",0,KEY_ALL_ACCESS|view,&user.key));
    RegKey saved;
    if(enable){
        DWORD disposition=0;RegCheck(RegCreateKeyExW(user,backup.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&saved.key,&disposition));
        if(disposition==REG_CREATED_NEW_KEY){
            RegKey original;DWORD existed=RegOpenKeyExW(user,cls.c_str(),0,KEY_READ,&original.key)==ERROR_SUCCESS;
            RegCheck(RegSetValueExW(saved,L"Existed",0,REG_DWORD,reinterpret_cast<BYTE*>(&existed),sizeof(existed)));
            if(existed){RegKey copy;RegCheck(RegCreateKeyExW(saved,L"Original",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&copy.key,nullptr));RegCheck(RegCopyTreeW(original,nullptr,copy));}
        }
        RegString(saved,L"",L"Executable",exe);
        RegKey target;RegCheck(RegCreateKeyExW(user,cls.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&target.key,nullptr));
        RegString(target,L"",nullptr,L"Adobe PDF - Baozi read-only compatibility");
        RegString(target,L"LocalServer32",nullptr,L"\""+exe+L"\" --ole");RegString(target,L"LocalServer32",L"ServerExecutable",exe);
        RegString(target,L"InprocHandler32",nullptr,L"ole32.dll");RegString(target,L"Verb\\0",nullptr,L"打开,0,2");
        RegString(target,L"Verb\\-2",nullptr,L"打开,0,2");RegString(target,L"MiscStatus",nullptr,L"32");
        RegDeleteTreeW(target,L"TreatAs");
    }else{
        if(RegOpenKeyExW(user,backup.c_str(),0,KEY_READ,&saved.key)!=ERROR_SUCCESS)return;
        // Refuse to remove a class registration changed by another application.
        RegKey current;RegCheck(RegOpenKeyExW(user,(cls+L"\\LocalServer32").c_str(),0,KEY_READ,&current.key));
        wchar_t command[32768]{};DWORD n=sizeof(command);RegCheck(RegQueryValueExW(current,nullptr,nullptr,nullptr,reinterpret_cast<BYTE*>(command),&n));
        wchar_t owner[32768]{};n=sizeof(owner);RegCheck(RegQueryValueExW(saved,L"Executable",nullptr,nullptr,reinterpret_cast<BYTE*>(owner),&n));
        if(std::wstring(command)!=L"\""+std::wstring(owner)+L"\" --ole")throw hresult_error(E_FAIL,L"对象注册已被其他程序修改，请勿自动覆盖。");
        RegCloseKey(current.key);current.key=nullptr;
        DWORD existed=0;n=sizeof(existed);RegCheck(RegQueryValueExW(saved,L"Existed",nullptr,nullptr,reinterpret_cast<BYTE*>(&existed),&n));
        RegCheck(RegDeleteTreeW(user,cls.c_str()));
        if(existed){RegKey original,target;RegCheck(RegOpenKeyExW(saved,L"Original",0,KEY_READ,&original.key));RegCheck(RegCreateKeyExW(user,cls.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&target.key,nullptr));RegCheck(RegCopyTreeW(original,nullptr,target));}
        RegCloseKey(saved.key);saved.key=nullptr;RegCheck(RegDeleteTreeW(user,backup.c_str()));
    }
}
int RegisterExcel(bool enable){
    try{wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
        RegisterExcelView(enable,KEY_WOW64_64KEY,L"x64",path);
        RegisterExcelView(enable,KEY_WOW64_32KEY,L"x86",path);
        return 0;
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"Excel 兼容注册失败",MB_ICONERROR);return 1;}
}

int RegisterPdf(){
    try{
        wchar_t filename[32768]{};GetModuleFileNameW(nullptr,filename,32768);
        std::wstring exe=filename,command=L"\""+exe+L"\" \"%1\"";
        RegString(HKEY_CURRENT_USER,L"Software\\Classes\\BaoziPDF.Document",nullptr,L"包子PDF 文档");
        RegString(HKEY_CURRENT_USER,L"Software\\Classes\\BaoziPDF.Document",L"FriendlyTypeName",L"包子PDF 文档");
        RegString(HKEY_CURRENT_USER,L"Software\\Classes\\BaoziPDF.Document\\DefaultIcon",nullptr,L"\""+exe+L"\",0");
        RegString(HKEY_CURRENT_USER,L"Software\\Classes\\BaoziPDF.Document\\shell\\open\\command",nullptr,command);
        std::wstring application=L"Software\\Classes\\Applications\\"+std::filesystem::path(exe).filename().wstring();
        RegString(HKEY_CURRENT_USER,application.c_str(),L"FriendlyAppName",APP_NAME);
        RegString(HKEY_CURRENT_USER,(application+L"\\SupportedTypes").c_str(),L".pdf",L"");
        RegString(HKEY_CURRENT_USER,(application+L"\\shell\\open\\command").c_str(),nullptr,command);
        RegKey openWith;RegCheck(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Classes\\.pdf\\OpenWithProgids",0,nullptr,0,KEY_SET_VALUE,nullptr,&openWith.key,nullptr));
        RegCheck(RegSetValueExW(openWith,L"BaoziPDF.Document",0,REG_NONE,nullptr,0));
        RegString(HKEY_CURRENT_USER,L"Software\\BaoziPDF\\Capabilities",L"ApplicationName",APP_NAME);
        RegString(HKEY_CURRENT_USER,L"Software\\BaoziPDF\\Capabilities",L"ApplicationDescription",L"轻量只读 PDF 阅读器，支持 Excel 内嵌 PDF");
        RegString(HKEY_CURRENT_USER,L"Software\\BaoziPDF\\Capabilities\\FileAssociations",L".pdf",L"BaoziPDF.Document");
        RegString(HKEY_CURRENT_USER,L"Software\\RegisteredApplications",L"BaoziPDF",L"Software\\BaoziPDF\\Capabilities");
        SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
        return 0;
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"PDF 文件关联失败",MB_ICONERROR);return 1;}
}

// Explicit one-time maintenance command. Accept only a sibling installation's
// own backup, preserving its original restoration data when changing branding.
int MigrateSettings(const std::wstring& previous){
    try{
        if(previous.empty()||previous==L"BaoziPDF"||previous.find_first_of(L"\\/")!=std::wstring::npos)return 2;
        RegKey software;RegCheck(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software",0,KEY_ALL_ACCESS,&software.key));
        RegKey old;auto status=RegOpenKeyExW(software,previous.c_str(),0,KEY_READ,&old.key);
        if(status==ERROR_FILE_NOT_FOUND)return 0;RegCheck(status);
        RegKey proof;RegCheck(RegOpenKeyExW(old,L"OleBackup\\x64",0,KEY_READ,&proof.key));
        wchar_t owner[32768]{},exe[32768]{};DWORD bytes=sizeof(owner);
        RegCheck(RegQueryValueExW(proof,L"Executable",nullptr,nullptr,reinterpret_cast<BYTE*>(owner),&bytes));
        GetModuleFileNameW(nullptr,exe,32768);
        if(std::filesystem::path(owner).parent_path()!=std::filesystem::path(exe).parent_path())return 3;
        RegKey target;RegCheck(RegCreateKeyExW(software,L"BaoziPDF",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&target.key,nullptr));
        RegKey existing;if(RegOpenKeyExW(target,L"OleBackup",0,KEY_READ,&existing.key)==ERROR_SUCCESS)return 4;
        RegKey sourceBackup,destBackup;
        RegCheck(RegOpenKeyExW(old,L"OleBackup",0,KEY_READ,&sourceBackup.key));
        RegCheck(RegCreateKeyExW(target,L"OleBackup",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&destBackup.key,nullptr));
        RegCheck(RegCopyTreeW(sourceBackup,nullptr,destBackup));
        RegCloseKey(sourceBackup.key);sourceBackup.key=nullptr;RegCloseKey(proof.key);proof.key=nullptr;RegCloseKey(old.key);old.key=nullptr;
        RegCheck(RegDeleteTreeW(software,(previous+L"\\OleBackup").c_str()));
        RegDeleteKeyW(software,previous.c_str()); // succeeds only if no unrelated subkeys remain
        return 0;
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"配置迁移失败",MB_ICONERROR);return 1;}
}
