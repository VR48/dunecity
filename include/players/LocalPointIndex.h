#ifndef LOCAL_POINT_INDEX_H
#define LOCAL_POINT_INDEX_H
#include <algorithm>
#include <vector>
#include <cstddef>
// Per-planning-call index. Values refer to a caller-owned, immutable snapshot;
// rebuilding it avoids stale positions or reservations between yard decisions.
class LocalPointIndex {
    static constexpr int cellSize=8;
    struct Point { int x,y; size_t id; };
    int width,height;
    std::vector<std::vector<Point>> cells;
public:
    LocalPointIndex(int w,int h):width(std::max(1,(w+7)/8)),height(std::max(1,(h+7)/8)),cells(width*height) {}
    void add(int x,int y,size_t id) {
        if(x>=0 && y>=0 && x/8<width && y/8<height) cells[(y/8)*width+x/8].push_back({x,y,id});
    }
    template<class Visit> void visit(int x,int y,int radius,Visit visitPoint) const {
        const int left=std::max(0,x-radius),right=std::min(width*8-1,x+radius);
        const int top=std::max(0,y-radius),bottom=std::min(height*8-1,y+radius);
        if(left>right || top>bottom) return;
        for(int yy=top/cellSize;yy<=bottom/cellSize;++yy)
            for(int xx=left/cellSize;xx<=right/cellSize;++xx)
                for(const auto& p:cells[yy*width+xx])
                    if(p.x>=left && p.x<=right && p.y>=top && p.y<=bottom) visitPoint(p.id);
    }
};
#endif
