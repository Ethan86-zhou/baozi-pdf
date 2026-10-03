#include "../src/navigation.h"
#include <iostream>
int main(){
    int failures=0;
    auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    auto a=MoveWheel(0,3,0,0,-1,72);check(a.page==1,"fitted page wheel down turns page");
    a=MoveWheel(1,3,0,0,1,72);check(a.page==0&&a.bottomOnArrival,"wheel up goes to previous bottom");
    a=MoveWheel(0,3,0,500,-1,72);check(a.page==0&&a.offset==72,"zoomed page scrolls before turning");
    a=MoveWheel(0,3,480,500,-1,72);check(a.page==0&&a.offset==500,"reaching bottom does not skip content");
    a=MoveWheel(0,3,500,500,-1,72);check(a.page==1,"continued wheel at bottom turns page");
    a=MoveWheel(1,3,10,500,1,72);check(a.page==1&&a.offset==0,"scroll to top before previous page");
    a=MoveWheel(0,3,0,0,1,72);check(a.page==0,"first page boundary");
    a=MoveWheel(2,3,500,500,-1,72);check(a.page==2&&a.offset==500,"last page boundary");
    a=MoveWheel(1,3,0,500,0,72);check(a.page==1&&a.offset==0,"partial detent does not turn page");
    return failures?1:0;
}
