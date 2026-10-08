#include <windows.h>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include "text_search.h"

SearchRect RotateSearchRect(SearchRect r,int rotation) {
    switch(rotation%4) {
    case 1:return {1-r.bottom,r.left,1-r.top,r.right};
    case 2:return {1-r.right,1-r.bottom,1-r.left,1-r.top};
    case 3:return {r.top,1-r.right,r.bottom,1-r.left};
    default:return r;
    }
}

struct TextSearchEngine::Impl {
    FPDF_DOCUMENT document=nullptr;
    FILE* file=nullptr;
    FPDF_FILEACCESS access{};
    std::shared_ptr<std::vector<uint8_t>> embedded;
    uint64_t fileId=UINT64_MAX;
    std::wstring path;
    void Clear() {
        if(document)FPDF_CloseDocument(document);
        document=nullptr;
        if(file)fclose(file);
        file=nullptr;embedded.reset();path.clear();fileId=UINT64_MAX;
    }
    static int Read(void* parameter,unsigned long position,unsigned char* buffer,unsigned long length) {
        auto handle=static_cast<FILE*>(parameter);
        return _fseeki64(handle,position,SEEK_SET)==0 && fread(buffer,1,length,handle)==length;
    }
    void Open(const SearchSource& source) {
        if(document&&fileId==source.fileId&&path==source.path&&embedded==source.embedded)return;
        Clear();
        std::string password;
        struct PasswordWiper { std::string& value; ~PasswordWiper(){if(!value.empty())SecureZeroMemory(value.data(),value.size());} } wipe{password};
        if(!source.password.empty()) {
            int length=WideCharToMultiByte(CP_UTF8,0,source.password.data(),int(source.password.size()),nullptr,0,nullptr,nullptr);
            password.resize(length);
            WideCharToMultiByte(CP_UTF8,0,source.password.data(),int(source.password.size()),password.data(),length,nullptr,nullptr);
        }
        if(source.embedded) {
            embedded=source.embedded;
            document=FPDF_LoadMemDocument64(embedded->data(),embedded->size(),password.empty()?nullptr:password.c_str());
        } else {
            if(_wfopen_s(&file,source.path.c_str(),L"rb")!=0||!file)throw SearchError{L"无法读取 PDF 的文字层。"};
            if(_fseeki64(file,0,SEEK_END)!=0){Clear();throw SearchError{L"无法读取 PDF 文件大小。"};}
            auto length=_ftelli64(file);
            if(length<=0||uint64_t(length)>std::numeric_limits<unsigned long>::max()) {
                Clear();throw SearchError{L"文字搜索暂不支持空文件或大于 4 GiB 的 PDF。"};
            }
            access.m_FileLen=static_cast<unsigned long>(length);access.m_GetBlock=Read;access.m_Param=file;
            document=FPDF_LoadCustomDocument(&access,password.empty()?nullptr:password.c_str());
        }
        if(!document) {
            auto error=FPDF_GetLastError();Clear();
            throw SearchError{error==FPDF_ERR_PASSWORD?L"搜索无法解锁文档，请关闭后用正确密码重新打开。":L"无法读取这份 PDF 的文字层。"};
        }
        fileId=source.fileId;path=source.path;
    }
};

TextSearchEngine::TextSearchEngine():impl(std::make_unique<Impl>()){FPDF_InitLibrary();}
TextSearchEngine::~TextSearchEngine(){impl->Clear();FPDF_DestroyLibrary();}
void TextSearchEngine::Clear(){impl->Clear();}

SearchResult TextSearchEngine::Find(const SearchSource& source,const std::wstring& query,bool matchCase,
    uint64_t generation,const std::atomic<uint64_t>* latest,const std::atomic<bool>* stop,
    const std::function<void(const SearchProgress&)>& progress) {
    auto check=[&]{if((stop&&stop->load())||(latest&&latest->load()!=generation))throw SearchCancelled{};};
    check();SearchResult result;result.generation=generation;result.query=query;
    if(query.empty())return result;
    if(query.size()>256)throw SearchError{L"关键词最多 256 个字符。"};
    impl->Open(source);check();
    int pages=FPDF_GetPageCount(impl->document);
    if(pages<=0)throw SearchError{L"文档没有可搜索页面。"};
    result.pages=static_cast<uint32_t>(pages);
    for(int index=0;index<pages;++index) {
        check();
        FPDF_PAGE page=FPDF_LoadPage(impl->document,index);
        if(!page)throw SearchError{L"无法搜索第 "+std::to_wstring(index+1)+L" 页。"};
        struct PageCloser{FPDF_PAGE page;~PageCloser(){FPDF_ClosePage(page);}} closePage{page};
        FPDF_TEXTPAGE text=FPDFText_LoadPage(page);
        if(!text)throw SearchError{L"无法读取第 "+std::to_wstring(index+1)+L" 页的文字层。"};
        struct TextCloser{FPDF_TEXTPAGE text;~TextCloser(){FPDFText_ClosePage(text);}} closeText{text};
        if(FPDFText_CountChars(text)>0)++result.textPages;
        auto search=FPDFText_FindStart(text,reinterpret_cast<FPDF_WIDESTRING>(query.c_str()),matchCase?FPDF_MATCHCASE:0,0);
        if(!search)throw SearchError{L"无法执行文字搜索。"};
        struct FindCloser{FPDF_SCHHANDLE search;~FindCloser(){FPDFText_FindClose(search);}} closeFind{search};
        while(FPDFText_FindNext(search)) {
            check();
            // Do one extra lookup to distinguish exactly 5000 hits from truncation.
            if(result.hits.size()==MAX_SEARCH_HITS){result.capped=true;break;}
            SearchHit hit;hit.page=static_cast<uint32_t>(index);
            hit.start=FPDFText_GetSchResultIndex(search);hit.length=FPDFText_GetSchCount(search);
            int rectangleCount=FPDFText_CountRects(text,hit.start,hit.length);
            for(int rectangle=0;rectangle<std::min(rectangleCount,32);++rectangle) {
                double left=0,top=0,right=0,bottom=0;
                if(!FPDFText_GetRect(text,rectangle,&left,&top,&right,&bottom))continue;
                SearchRect box{1,1,0,0};bool valid=true;
                for(auto point:std::array<std::pair<double,double>,4>{{{left,top},{right,top},{right,bottom},{left,bottom}}}) {
                    int x=0,y=0;
                    // Includes the PDF's intrinsic rotation and crop box.
                    if(!FPDF_PageToDevice(page,0,0,100000,100000,0,point.first,point.second,&x,&y)){valid=false;break;}
                    double nx=std::clamp(x/100000.0,0.0,1.0),ny=std::clamp(y/100000.0,0.0,1.0);
                    box.left=std::min(box.left,nx);box.top=std::min(box.top,ny);
                    box.right=std::max(box.right,nx);box.bottom=std::max(box.bottom,ny);
                }
                if(valid&&box.right>box.left&&box.bottom>box.top)hit.rectangles.push_back(box);
            }
            result.hits.push_back(std::move(hit));
        }
        check();if(progress)progress({generation,static_cast<uint32_t>(index+1),result.pages,result.hits.size()});
        if(result.capped)break;
    }
    check();return result;
}
