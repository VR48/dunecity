#ifndef GROUND_ACCESS_POLICY_H
#define GROUND_ACCESS_POLICY_H
#include <algorithm>
#include <cstdint>
#include <vector>

// Static vehicle connectivity only: moving units must not make a temporary
// traffic jam look like a wall. Rebuilt once per construction planning pass.
class GroundAccessPolicy {
public:
    struct Rect { int x, y, w, h; };
    void reset(int width, int height, const std::vector<uint8_t>& passable) {
        w = width; h = height; open = passable;
        parent.assign(w*h, -1); lanes.assign(w*h, false);
        std::vector<int> component(w*h, -1), queue, largest;
        int label = 0;
        for (int i=0; i<w*h; ++i) {
            if (!open[i] || component[i]>=0) continue;
            queue.clear(); queue.push_back(i); component[i]=label;
            for (size_t q=0; q<queue.size(); ++q) neighbours(queue[q], [&](int n) {
                if (open[n] && component[n]<0) { component[n]=label; queue.push_back(n); }
            });
            if (queue.size()>largest.size()) largest=queue;
            ++label;
        }
        if (largest.empty()) return;
        // One outside anchor, in the widest part of the main component. Separate
        // roots in courtyards would let a building seal a large courtyard shut.
        std::vector<int> clearance(w*h,-1);
        queue.clear();
        for (int i=0;i<w*h;++i) if (!open[i] || i%w==0 || i%w==w-1 || i<w || i>=w*(h-1)) {
            clearance[i]=0; queue.push_back(i);
        }
        for (size_t q=0;q<queue.size();++q) neighbours(queue[q],[&](int n) {
            if (clearance[n]<0) { clearance[n]=clearance[queue[q]]+1; queue.push_back(n); }
        });
        int anchor=largest.front();
        for (int i:largest) if (clearance[i]>clearance[anchor]) anchor=i;
        queue.clear(); parent[anchor]=anchor; queue.push_back(anchor);
        for (size_t q=0; q<queue.size(); ++q) neighbours(queue[q], [&](int n) {
            if (open[n] && parent[n]<0) { parent[n]=queue[q]; queue.push_back(n); }
        });
    }
    void protectUnit(int x, int y) {
        if (!inside(x,y)) return;
        for (int i=y*w+x; i>=0 && parent[i]>=0 && !lanes[i]; i=parent[i]) {
            lanes[i]=true;
            if (parent[i]==i) break;
        }
    }
    void protectExits(Rect r) {
        perimeter(r,[&](int x,int y) { protectUnit(x,y); });
    }
    bool allows(Rect r, bool needsExit) const {
        if (r.w<=0 || r.h<=0 || !inside(r.x,r.y) || !inside(r.x+r.w-1,r.y+r.h-1)) return false;
        for (int y=r.y;y<r.y+r.h;++y) for (int x=r.x;x<r.x+r.w;++x)
            if (lanes[y*w+x]) return false;
        if (!needsExit) return true;
        // Check the small ring around the proposed footprint. Every disconnected
        // piece needs a route out; a route may detour around the new building.
        std::vector<int> edge;
        perimeter(r,[&](int x,int y) {
            if (inside(x,y) && open[y*w+x]) edge.push_back(y*w+x);
        });
        if (edge.empty()) return false;
        std::vector<bool> seen(edge.size(),false);
        std::vector<size_t> queue;
        for (size_t start=0;start<edge.size();++start) {
            if (seen[start]) continue;
            queue.clear(); queue.push_back(start); seen[start]=true;
            bool reachesOutside=false;
            for (size_t q=0;q<queue.size();++q) {
                int i=edge[queue[q]];
                if (!reachesOutside && parent[i]>=0) for (;;) {
                    if (contains(r,i%w,i/w)) break;
                    if (lanes[i] || parent[i]==i) { reachesOutside=true; break; }
                    i=parent[i];
                }
                neighbours(edge[queue[q]],[&](int n) {
                    auto it=std::find(edge.begin(),edge.end(),n);
                    if (it==edge.end()) return;
                    const size_t at=it-edge.begin();
                    if (!seen[at]) { seen[at]=true; queue.push_back(at); }
                });
            }
            if (!reachesOutside) return false;
        }
        return true;
    }

private:
    int w=0,h=0;
    std::vector<uint8_t> open;
    std::vector<int> parent;
    std::vector<bool> lanes;
    bool inside(int x,int y) const { return x>=0 && y>=0 && x<w && y<h; }
    static bool contains(Rect r,int x,int y) { return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h; }
    template<class F> void neighbours(int i,F f) const {
        if (i%w>0) f(i-1);
        if (i%w+1<w) f(i+1);
        if (i>=w) f(i-w);
        if (i+w<w*h) f(i+w);
    }
    template<class F> static void perimeter(Rect r,F f) {
        for (int x=r.x-1;x<=r.x+r.w;++x) { f(x,r.y-1); f(x,r.y+r.h); }
        for (int y=r.y;y<r.y+r.h;++y) { f(r.x-1,y); f(r.x+r.w,y); }
    }
};
#endif
