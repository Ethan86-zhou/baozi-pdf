#include "../src/text_search.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int checks=0;
void Check(bool condition,const char* label) {
    if(!condition)throw std::runtime_error(label);
    ++checks;std::cout<<"PASS "<<label<<'\n';
}
template<class Action> bool SearchFails(Action action) {
    try{action();}catch(const SearchError&){return true;}return false;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2)throw std::runtime_error("fixture directory required");
        const std::filesystem::path root(argv[1]);
        TextSearchEngine engine;
        uint64_t id=0;
        auto source=[&](const wchar_t* name){return SearchSource{++id,(root/name).wstring(),L"",nullptr};};
        auto sample=source(L"sample.pdf");
        auto pages=engine.Find(sample,L"PAGE",true);
        Check(pages.hits.size()==3&&pages.pages==4&&pages.textPages==3,"text layer only, excludes scanned page");
        Check(pages.hits[0].page==0&&pages.hits[1].page==1&&pages.hits[2].page==3,"document order and intrinsic rotation page");
        bool bounds=true;
        for(auto& hit:pages.hits){bounds=bounds&&!hit.rectangles.empty()&&hit.length==4;
            for(auto& r:hit.rectangles)bounds=bounds&&r.left>=0&&r.top>=0&&r.right<=1&&r.bottom<=1&&r.left<r.right&&r.top<r.bottom;}
        Check(bounds,"normalized highlight boxes");
        auto portrait=pages.hits.front().rectangles.front(),rotated=pages.hits.back().rectangles.front();
        Check(portrait.top>.9&&rotated.left>.7&&rotated.right<.85,"portrait footer and rotated heading geometry");
        Check(engine.Find(sample,L"page",false).hits.size()==3,"case insensitive English");
        Check(engine.Find(sample,L"page",true).hits.empty(),"case sensitive English");
        Check(engine.Find(sample,L"中文",true).hits.size()==1,"exact Chinese text");
        Check(engine.Find(sample,L"旋转",true).hits.size()==1,"Chinese intrinsic rotation");
        auto phrase=engine.Find(sample,L"PAGE NAVIGATION",true);
        Check(phrase.hits.size()==1&&phrase.hits.front().page==1,"exact phrase on landscape page");
        Check(engine.Find(sample,L"SCANNED IMAGE",false).hits.empty(),"raster text is not searched or OCRed");
        Check(engine.Find(sample,L"不存在的词",true).hits.empty(),"missing query");
        Check(engine.Find(sample,L"",true).hits.empty(),"empty query");
        Check(SearchFails([&]{engine.Find(sample,std::wstring(257,L'x'),true);}),"query length bound");
        auto unicode=source(L"中文 路径.pdf");
        Check(engine.Find(unicode,L"中文",true).hits.size()==1,"Unicode and space in path");
        std::ifstream stream(root/L"sample.pdf",std::ios::binary);
        auto data=std::make_shared<std::vector<uint8_t>>(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
        auto embedded=SearchSource{++id,L"embedded PDF",L"",data};
        Check(engine.Find(embedded,L"PAGE",true).hits.size()==3,"in-memory embedded PDF");
        auto encrypted=source(L"encrypted.pdf");encrypted.password=L"wrong";
        Check(SearchFails([&]{engine.Find(encrypted,L"中文",true);}),"wrong password reports failure");
        encrypted.password=L"reader-test";
        Check(engine.Find(encrypted,L"中文",true).hits.size()==1,"encrypted document text");
        auto large=source(L"large.pdf");
        auto capped=engine.Find(large,L"Register",true);
        Check(capped.capped&&capped.hits.size()==MAX_SEARCH_HITS&&capped.pages==250,"bounded 8000-match document");
        auto last=engine.Find(large,L"PAGE 250 / 250",true);
        Check(last.hits.size()==1&&last.hits.front().page==249,"search reaches final page");
        std::atomic<uint64_t> latest{42};bool cancelled=false;uint32_t scanned=0;
        try{engine.Find(large,L"NAVIGATION",true,42,&latest,nullptr,[&](const SearchProgress& p){scanned=p.scanned;if(p.scanned==3)++latest;});}
        catch(const SearchCancelled&){cancelled=true;}
        Check(cancelled&&scanned==3,"cancellation drops old search after page boundary");
        std::atomic<bool> stop{true};cancelled=false;
        try{engine.Find(large,L"Register",true,43,&latest,&stop);}catch(const SearchCancelled&){cancelled=true;}
        Check(cancelled,"shutdown cancellation");
        Check(engine.Find(sample,L"中文",true).hits.size()==1,"document switch after cancellation");
        engine.Clear();
        Check(engine.Find(sample,L"PAGE",true).hits.size()==3,"release and reopen text engine");
        auto broken=source(L"broken.pdf"),missing=source(L"missing.pdf");
        Check(SearchFails([&]{engine.Find(broken,L"x",true);}),"broken PDF error");
        Check(SearchFails([&]{engine.Find(missing,L"x",true);}),"missing PDF error");
        SearchRect box{.1,.2,.3,.4};auto clockwise=RotateSearchRect(box,1);
        Check(std::abs(clockwise.left-.6)<1e-9&&std::abs(clockwise.top-.1)<1e-9&&std::abs(clockwise.right-.8)<1e-9&&std::abs(clockwise.bottom-.3)<1e-9,"viewer rotation geometry");
        for(int i=0;i<4;++i)box=RotateSearchRect(box,1);
        Check(std::abs(box.left-.1)<1e-9&&std::abs(box.top-.2)<1e-9&&std::abs(box.right-.3)<1e-9&&std::abs(box.bottom-.4)<1e-9,"four rotations restore geometry");
        std::cout<<checks<<" search checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
