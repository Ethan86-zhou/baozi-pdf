#include <windows.h>
#include <ole2.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
struct Sink: IAdviseSink {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IAdviseSink)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{ULONG n=--refs;if(!n)delete this;return n;}
    void STDMETHODCALLTYPE OnDataChange(FORMATETC*,STGMEDIUM*)override{}
    void STDMETHODCALLTYPE OnViewChange(DWORD,LONG)override{}
    void STDMETHODCALLTYPE OnRename(IMoniker*)override{}
    void STDMETHODCALLTYPE OnSave()override{}
    void STDMETHODCALLTYPE OnClose()override{}
};
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    HRESULT hr=OleInitialize(nullptr);if(FAILED(hr))return 3;
    std::ofstream log{std::filesystem::path(argv[2])};
    auto check=[&](const char* label,HRESULT code){log<<label<<"\t0x"<<std::hex<<unsigned(code)<<std::endl;return SUCCEEDED(code);};
    IStorage* storage=nullptr;IOleObject* object=nullptr;IPersistStorage* persist=nullptr;
    CLSID original{0xb801ca65,0xa1fc,0x11d0,{0x85,0xad,0x44,0x45,0x53,0x54,0,0}},treated{};
    check("CoGetTreatAsClass",CoGetTreatAsClass(original,&treated));
    wchar_t guid[64]{};StringFromGUID2(treated,guid,64);std::wcout<<guid<<std::endl;
    IUnknown* direct=nullptr;auto directResult=CoCreateInstance(treated,nullptr,CLSCTX_LOCAL_SERVER,IID_IUnknown,reinterpret_cast<void**>(&direct));check("Direct CoCreateInstance treated CLSID",directResult);if(direct)direct->Release();
    if(!check("StgOpenStorage",StgOpenStorage(argv[1],nullptr,STGM_READ|STGM_SHARE_DENY_WRITE,nullptr,0,&storage)))return 4;
    if(!check("OleLoad default handler",OleLoad(storage,IID_IOleObject,nullptr,reinterpret_cast<void**>(&object))))return 5;
    check("SetHostNames",object->SetHostNames(L"Acceptance Probe",L"Existing Adobe OLE"));
    RECT rect{0,0,600,800};
    if(!check("DoVerb PRIMARY",object->DoVerb(OLEIVERB_PRIMARY,nullptr,nullptr,0,nullptr,&rect)))return 6;
    IDataObject* data=nullptr;if(!check("IDataObject",object->QueryInterface(IID_IDataObject,reinterpret_cast<void**>(&data))))return 7;
    auto sink=new Sink;FORMATETC format{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};DWORD connection=0;
    if(!check("Excel DAdvise",data->DAdvise(&format,ADVF_NODATA,sink,&connection)))return 8;
    check("DUnadvise",data->DUnadvise(connection));sink->Release();data->Release();
    if(!check("IPersistStorage",object->QueryInterface(IID_IPersistStorage,reinterpret_cast<void**>(&persist))))return 9;
    if(persist->IsDirty()!=S_FALSE){log<<"FAIL\tRead-only object is dirty"<<std::endl;return 10;}
    std::wstring savePath=std::wstring(argv[2])+L".storage";IStorage* saved=nullptr;
    if(!check("Create save-as test storage",StgCreateDocfile(savePath.c_str(),STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,&saved)))return 11;
    if(!check("Save-as preserves original storage",persist->Save(saved,FALSE)))return 12;
    auto content=[](IStorage* stg){IStream* s=nullptr;std::vector<unsigned char> bytes;if(FAILED(stg->OpenStream(L"CONTENTS",nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&s)))return bytes;STATSTG info{};s->Stat(&info,STATFLAG_NONAME);bytes.resize(size_t(info.cbSize.QuadPart));ULONG read=0;s->Read(bytes.data(),ULONG(bytes.size()),&read);s->Release();if(read!=bytes.size())bytes.clear();return bytes;};
    auto before=content(storage),after=content(saved);if(before.empty()||before!=after){log<<"FAIL\tSaved PDF bytes differ"<<std::endl;return 13;}
    CLSID savedClass{};ReadClassStg(saved,&savedClass);if(savedClass!=original){log<<"FAIL\tSaved class differs"<<std::endl;return 14;}
    log<<"PASS\tRead-only state and saved PDF bytes/CLSID preserved"<<std::endl;
    persist->SaveCompleted(nullptr);persist->Release();saved->Release();
    log<<"PASS\tExisting Adobe OLE activation succeeded"<<std::endl;
    object->Release();storage->Release();OleUninitialize();return 0;
}
