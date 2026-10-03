#pragma once
#include <algorithm>
#include <cstdint>

struct WheelMove { uint32_t page; int offset; bool bottomOnArrival; };
// Only turn a page when a fresh wheel step starts at its edge.
inline WheelMove MoveWheel(uint32_t page,uint32_t count,int offset,int maximum,int ticks,int step){
    if(!count||!ticks)return {page,offset,false};
    if(ticks<0&&offset>=maximum&&page+1<count)return {page+1,0,false};
    if(ticks>0&&offset<=0&&page>0)return {page-1,0,true};
    int next=int(std::clamp<int64_t>(int64_t(offset)-int64_t(ticks)*step,0,maximum));
    return {page,next,false};
}
