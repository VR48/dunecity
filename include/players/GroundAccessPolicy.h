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
        parent.assign(w*h, -1); units.clear(); exits.clear(); outside = -1;
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
        outside = anchor;
        queue.clear(); parent[anchor]=anchor; queue.push_back(anchor);
        for (size_t q=0; q<queue.size(); ++q) neighbours(queue[q], [&](int n) {
            if (open[n] && parent[n]<0) { parent[n]=queue[q]; queue.push_back(n); }
        });
    }
    void protectUnit(int x, int y) {
        if (inside(x,y) && parent[y*w+x]>=0) units.push_back(y*w+x);
    }
    void protectExits(Rect r) {
        std::vector<int> edge;
        perimeter(r,[&](int x,int y) {
            if (inside(x,y) && parent[y*w+x]>=0) edge.push_back(y*w+x);
        });
        // Already isolated producers must not freeze unrelated construction.
        if (!edge.empty()) exits.push_back(std::move(edge));
    }
    bool allows(Rect r, bool needsExit) const {
        if (r.w<=0 || r.h<=0 || !inside(r.x,r.y) || !inside(r.x+r.w-1,r.y+r.h-1)) return false;
        auto covered=[&](int i) { return contains(r,i%w,i/w); };
        for (int i:units) if (covered(i)) return false;
        for (const auto& group:exits)
            if (std::none_of(group.begin(),group.end(),[&](int i){return !covered(i);})) return false;

        // Only the original outside component matters. Removing a footprint
        // cannot reconnect an existing isolated courtyard.
        std::vector<int> edge;
        perimeter(r,[&](int x,int y) {
            if (inside(x,y) && parent[y*w+x]>=0) edge.push_back(y*w+x);
        });
        if (edge.empty()) return !needsExit;

        // Cheap common case: if the perimeter reconnects around the footprint,
        // all old routes can detour locally. No whole-map search is needed.
        std::vector<int> ring{edge.front()};
        for (size_t q=0;q<ring.size();++q) neighbours(ring[q],[&](int n) {
            if (std::find(edge.begin(),edge.end(),n)!=edge.end()
                && std::find(ring.begin(),ring.end(),n)==ring.end()) ring.push_back(n);
        });
        if (ring.size()==edge.size()) return true;

        // A broken local ring may still have a longer route around other
        // buildings. Flood the remaining static graph, not a fixed path tree.
        std::vector<uint8_t> reached(w*h,0);
        std::vector<int> queue;
        std::vector<uint8_t> boundary(w*h,0);
        for (int i:edge) boundary[i]=1;
        auto flood=[&](int seed, bool stopWhenConnected=false) {
            size_t remaining=edge.size()-boundary[seed];
            queue.clear(); queue.push_back(seed); reached[seed]=1;
            for (size_t q=0;q<queue.size() && (!stopWhenConnected || remaining);++q) neighbours(queue[q],[&](int n) {
                if (parent[n]>=0 && !covered(n) && !reached[n]) {
                    reached[n]=1; queue.push_back(n);
                    remaining-=boundary[n];
                }
            });
            return remaining==0;
        };
        if (flood(edge.front(),true)) return true;
        if (outside>=0 && !covered(outside)) {
            if (!reached[outside]) {
                std::fill(reached.begin(),reached.end(),0);
                flood(outside);
            }
        }
        else {
            // Building over the old anchor is legal; use the largest remaining
            // component instead of permanently reserving that arbitrary tile.
            std::vector<int> largest=queue;
            for (int i=0;i<w*h;++i) if (parent[i]>=0 && !covered(i) && !reached[i]) {
                flood(i);
                if (queue.size()>largest.size()) largest=queue;
            }
            std::fill(reached.begin(),reached.end(),0);
            for (int i:largest) reached[i]=1;
        }
        for (int i:units) if (!reached[i]) return false;
        for (const auto& group:exits)
            if (std::none_of(group.begin(),group.end(),[&](int i){return reached[i]!=0;})) return false;
        return !needsExit || std::any_of(edge.begin(),edge.end(),[&](int i){return reached[i]!=0;});
    }

private:
    int w=0,h=0;
    std::vector<uint8_t> open;
    std::vector<int> parent;
    int outside=-1;
    std::vector<int> units;
    std::vector<std::vector<int>> exits;
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
