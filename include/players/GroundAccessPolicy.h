#ifndef GROUND_ACCESS_POLICY_H
#define GROUND_ACCESS_POLICY_H
#include <algorithm>
#include <vector>

// Placement only inspects the footprint and its one-tile border. Preserve
// local passage around building blocks; never build or search a map-wide graph.
class GroundAccessPolicy {
public:
    struct Rect { int x, y, w, h; };
    template<class Passable>
    static bool allows(Rect r, bool needsExit, Passable passable, bool diagonal = false) {
        if (r.w<=0 || r.h<=0) return false;
        const int w=r.w+2,h=r.h+2;
        std::vector<int> before(w*h,-1),after(w*h,-1);
        int frontage=0;
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            const bool border=x==0 || y==0 || x==w-1 || y==h-1;
            if (passable(r.x+x-1,r.y+y-1)) {
                before[y*w+x]=-2;
                if (border) { after[y*w+x]=-2; ++frontage; }
            }
        }
        if (needsExit && !frontage) return false;
        auto label=[&](std::vector<int>& cells) {
            std::vector<int> queue;
            for (int seed=0;seed<w*h;++seed) if (cells[seed]==-2) {
                queue.clear();queue.push_back(seed);cells[seed]=seed;
                for (size_t q=0;q<queue.size();++q) {
                    const int x=queue[q]%w,y=queue[q]/w;
                    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                        if ((!dx && !dy) || (!diagonal && dx && dy)) continue;
                        const int nx=x+dx,ny=y+dy;
                        if (nx<0 || ny<0 || nx>=w || ny>=h) continue;
                        const int n=ny*w+nx;
                        if (cells[n]==-2) {cells[n]=seed;queue.push_back(n);}
                    }
                }
            }
        };
        label(before);label(after);
        // Every previously connected pair of surviving border tiles must still
        // connect locally. Adjacent buildings can share walls; closing a lane
        // between them cannot cut the remaining sides off from one another.
        std::vector<int> destination(w*h,-1);
        for (int i=0;i<w*h;++i) if (after[i]>=0) {
            int& target=destination[before[i]];
            if (target>=0 && target!=after[i]) return false;
            target=after[i];
        }
        return true;
    }
};
#endif
