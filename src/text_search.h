#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SearchRect { double left=0,top=0,right=0,bottom=0; };
struct SearchHit {
    uint32_t page=0;
    int start=0,length=0;
    std::vector<SearchRect> rectangles;
};
struct SearchSource {
    uint64_t fileId=0;
    std::wstring path,password;
    std::shared_ptr<std::vector<uint8_t>> embedded;
};
struct SearchProgress { uint64_t generation=0; uint32_t scanned=0,total=0; size_t matches=0; };
struct SearchResult {
    uint64_t generation=0;
    std::wstring query,error;
    std::vector<SearchHit> hits;
    uint32_t pages=0,textPages=0;
    bool capped=false;
};
struct SearchCancelled {};
struct SearchError { std::wstring message; };
constexpr size_t MAX_SEARCH_HITS=5000;
SearchRect RotateSearchRect(SearchRect rectangle,int rotation);

// PDFium is used only on one search thread, independently of Windows rendering.
// Keep one document and one text page at a time; retain only bounded hit boxes.
class TextSearchEngine {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    TextSearchEngine();
    ~TextSearchEngine();
    TextSearchEngine(const TextSearchEngine&)=delete;
    TextSearchEngine& operator=(const TextSearchEngine&)=delete;
    void Clear();
    SearchResult Find(const SearchSource& source,const std::wstring& query,bool matchCase,
        uint64_t generation=0,const std::atomic<uint64_t>* latest=nullptr,
        const std::atomic<bool>* stop=nullptr,
        const std::function<void(const SearchProgress&)>& progress={});
};
