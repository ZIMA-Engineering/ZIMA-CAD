#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::document {
// Disposable analytical preview. Supports come from authored profile curves;
// neither topology nor solid geometry is reconstructed through OCCT here.
class DraftPreview {
    using V=kernel::Vec3;
    using R=kernel::ExtrusionRequest;
    struct P { double x{},y{}; P operator+(P b)const{return{x+b.x,y+b.y};}
        P operator-(P b)const{return{x-b.x,y-b.y};} P operator*(double s)const{return{x*s,y*s};} };
    static double dot(P a,P b){return a.x*b.x+a.y*b.y;}
    static double cross(P a,P b){return a.x*b.y-a.y*b.x;}
    static double norm(P a){return std::sqrt(dot(a,a));}
    static P left(P a){return{-a.y,a.x};}
    static double dot3(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static V cross3(V a,V b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    struct Support {
        P a,b,center,normal;double radius{},sense{1},base{},material{1},sweep{};
        double distance(P p)const{return radius?sense*(radius-norm(p-center)):dot(p-a,normal);}
        P gradient(P p)const{return radius?(center-p)*(sense/norm(p-center)):normal;}
        bool contains(P p)const {
            if(!radius){const double t=dot(p-a,b-a)/dot(b-a,b-a),margin=1e-6+100*std::abs(base)/norm(b-a);return t>=-margin&&t<=1+margin;}
            if(std::abs(sweep)>2*std::numbers::pi-1e-8)return true;
            double t=std::atan2(cross(a-center,p-center),dot(a-center,p-center));
            if(std::abs(t)<1e-8)t=0;
            if(sweep>0&&t<0)t+=2*std::numbers::pi;
            if(sweep<0&&t>0)t-=2*std::numbers::pi;
            return std::abs(t)<=std::abs(sweep)+1e-6+100*std::abs(base)/radius;
        }
    };
    V x_,y_;bool open_{};std::vector<Support> supports_;
    P project(V p)const{return{dot3(p,x_),dot3(p,y_)};}
    static void fail(){throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");}
    void loop(const R::ProfileLoop& profile,double material,double base=0) {
        std::vector<Support> curves;
        const auto line=[&](V a,V b){Support s;s.a=project(a);s.b=project(b);
            const double l=norm(s.b-s.a);if(l<1e-12)fail();s.normal=left(s.b-s.a)*(1/l);curves.push_back(s);};
        const auto arc=[&](V a,V m,V b){Support s;s.a=project(a);s.b=project(b);const auto mid=project(m);
            const P u=mid-s.a,v=s.b-s.a;const double det=2*cross(u,v);if(std::abs(det)<1e-14)fail();
            s.center=s.a+P{(dot(u,u)*v.y-dot(v,v)*u.y)/det,(u.x*dot(v,v)-v.x*dot(u,u))/det};
            s.radius=norm(s.a-s.center);s.sense=cross(u,v)>0?1:-1;
            s.sweep=std::atan2(cross(s.a-s.center,s.b-s.center),dot(s.a-s.center,s.b-s.center));
            if(s.sense*s.sweep<=0)s.sweep+=s.sense*2*std::numbers::pi;curves.push_back(s);};
        std::visit([&](const auto& p){using T=std::decay_t<decltype(p)>;
            if constexpr(std::is_same_v<T,R::PolygonProfile>){for(std::size_t i=0;i<p.vertices.size();++i)line(p.vertices[i],p.vertices[(i+1)%p.vertices.size()]);}
            else if constexpr(std::is_same_v<T,R::CircleProfile>){Support s;s.center=project(p.center);s.radius=p.radius;s.a=s.b=s.center+P{p.radius,0};s.sweep=2*std::numbers::pi;curves.push_back(s);}
            else if constexpr(std::is_same_v<T,R::CurvedProfile>){for(const auto& c:p.curves)std::visit([&](const auto& c){using C=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<C,R::LineCurve>)line(c.start,c.end);
                else if constexpr(std::is_same_v<C,R::ArcCurve>)arc(c.start,c.middle,c.end);
                else throw std::runtime_error("Draft supports profiles made of lines and circular arcs.");},c);}
            else throw std::runtime_error("Draft supports profiles made of lines and circular arcs.");
        },profile);
        double area=0;
        for(const auto& c:curves)area+=cross(c.a,c.b)+(c.radius?c.radius*c.radius*(c.sweep-std::sin(c.sweep)):0);
        if(!open_&&std::abs(area)<1e-12)fail();
        for(auto c:curves){if(!open_&&area<0){c.normal=c.normal*(-1);c.sense=-c.sense;}
            c.base=base;c.material=material;supports_.push_back(c);}
    }
public:
    DraftPreview(const R& r) {
        if(r.surface_result&&!r.open_profile_end_id.empty())throw std::runtime_error("Surface draft requires a closed profile.");
        open_=!r.open_profile_end_id.empty()||(r.wall&&!r.wall->end_point_id.empty());
        const double size=std::sqrt(dot3(r.direction,r.direction));V n{r.direction.x/size,r.direction.y/size,r.direction.z/size};
        if(r.wall&&!r.first_cap_is_start)n={-n.x,-n.y,-n.z};
        x_=cross3(std::abs(n.z)<.9?V{0,0,1}:V{0,1,0},n);const double lx=std::sqrt(dot3(x_,x_));
        x_={x_.x/lx,x_.y/lx,x_.z/lx};y_=cross3(n,x_);
        if(r.wall){loop(r.outer_profile,1,r.wall->first_offset);loop(r.outer_profile,-1,r.wall->second_offset);
            if(open_){const auto first=supports_.front(),last=supports_[supports_.size()/2-1];
                for(bool end:{false,true}){const auto& source=end?last:first;const P p=end?source.b:source.a,g=source.gradient(p);
                    Support cap;cap.a=p+g*r.wall->first_offset;cap.b=p+g*r.wall->second_offset;
                    cap.normal=left(g)*(end?1:-1);supports_.push_back(cap);}
            }
        }
        else {loop(r.outer_profile,1);for(const auto& p:r.inner_profiles)loop(p,-1);}
        for(const auto& region:r.additional_profile_regions){loop(region.outer_profile,1);for(const auto& p:region.inner_profiles)loop(p,-1);}
    }
    V offset(V source,double amount)const {
        if(amount==0)return source;
        const P p=project(source);std::vector<const Support*> selected;
        double nearest=1e100;
        for(const auto& c:supports_)if(c.contains(p))nearest=std::min(nearest,std::abs(c.distance(p)-c.base));
        for(const auto& c:supports_)if(c.contains(p)&&std::abs(c.distance(p)-c.base)<=nearest+1e-7)selected.push_back(&c);
        if(selected.empty()||nearest>.02)fail();
        P q=p;
        for(int iteration=0;iteration<40;++iteration){
            const auto& a=*selected.front();const P ga=a.gradient(q)*a.material;
            const double fa=a.material*(a.distance(q)-a.base)-amount;
            if(selected.size()==1){q=q-ga*fa;if(std::abs(fa)<1e-9)break;}
            else {const auto& b=*selected.back();const P gb=b.gradient(q)*b.material;
                const double fb=b.material*(b.distance(q)-b.base)-amount,det=cross(ga,gb);
                if(std::max(std::abs(fa),std::abs(fb))<1e-9)break;
                if(std::abs(det)<1e-10){if(dot(ga,gb)<.999||std::abs(fa-fb)>1e-7)fail();q=q-ga*fa;}
                else q=q-P{(fa*gb.y-ga.y*fb)/det,(ga.x*fb-fa*gb.x)/det};
            }
        }
        for(const auto* c:selected)if(!std::isfinite(q.x)||!std::isfinite(q.y)||std::abs(c->material*(c->distance(q)-c->base)-amount)>1e-7)fail();
        const auto d=q-p;return{source.x+x_.x*d.x+y_.x*d.y,source.y+x_.y*d.x+y_.y*d.y,source.z+x_.z*d.x+y_.z*d.y};
    }
};
}
