#pragma once
#include <zima/kernel/surface_wrap.hpp>

namespace zima::kernel {
// A decal is clipped to the target's existing display triangles, including its
// holes. It shares their depth and never covers a missing portion of the face.
// Only texture coordinates need subdivision; native geometry remains unchanged.
inline ViewerImage wrapped_image(const ViewerImage& source,const WrappingRequest& request,
        const SurfaceWrap& map,const ViewerMesh& target,bool allow_empty=false,bool bounded_chart=false) {
    using namespace wrap_detail;
    ViewerImage result=source;result.surface_vertices.clear();result.surface_uv.clear();
    const auto local=[&](Vec3 p){p=sub(p,request.origin);return std::array{dot(p,request.x_axis),dot(p,request.y_axis)};};
    const auto a=local(source.corners[0]),b=local(source.corners[1]),d=local(source.corners[3]);
    if(!bounded_chart)map.validate_image_domain({a,b,local(source.corners[2]),d});
    const double ux=b[0]-a[0],uy=b[1]-a[1],vx=d[0]-a[0],vy=d[1]-a[1],det=ux*vy-uy*vx;
    if(!std::isfinite(det)||std::abs(det)<1e-20)throw std::runtime_error("Invalid wrapped image mesh.");
    const double center_u=a[0]+(ux+vx)*.5,center_v=a[1]+(uy+vy)*.5;
    const auto image_center=map.point(center_u,center_v);
    const double image_radius=.5*std::hypot(std::hypot(ux,uy),std::hypot(vx,vy));
    for(auto& corner:result.corners){const auto p=local(corner);corner=map.point(p[0],p[1]);}
    const auto uv=[&](Vec3 p) {
        const auto reference=bounded_chart?map.source_anchor():std::pair{center_u,center_v};
        const auto [u,v]=map.source_coordinates(p,reference.first,reference.second);
        return std::array{((u-a[0])*vy-(v-a[1])*vx)/det,(ux*(v-a[1])-uy*(u-a[0]))/det};
    };
    struct Vertex {Vec3 point;std::array<double,2> uv;};
    const auto interpolate=[](const Vertex& a,const Vertex& b,double t) {
        return Vertex{add(a.point,scale(sub(b.point,a.point),t)),
            {a.uv[0]+t*(b.uv[0]-a.uv[0]),a.uv[1]+t*(b.uv[1]-a.uv[1])}};
    };
    const auto append_polygon=[&](std::vector<Vertex> polygon) {
        for(int axis=0;axis<2;++axis)for(int upper=0;upper<2;++upper) {
            if(polygon.empty())return;
            std::vector<Vertex> clipped;const double bound=upper?1.:0.;
            auto previous=polygon.back();bool previous_in=upper?previous.uv[axis]<=1:previous.uv[axis]>=0;
            for(const auto& current:polygon) {
                const bool inside=upper?current.uv[axis]<=1:current.uv[axis]>=0;
                if(inside!=previous_in)clipped.push_back(interpolate(previous,current,(bound-previous.uv[axis])/(current.uv[axis]-previous.uv[axis])));
                if(inside)clipped.push_back(current);previous=current;previous_in=inside;
            }
            polygon=std::move(clipped);
        }
        for(std::size_t i=1;i+1<polygon.size();++i)for(const auto& vertex:{polygon[0],polygon[i],polygon[i+1]}) {
            result.surface_vertices.push_back(vertex.point);result.surface_uv.push_back(vertex.uv);
        }
    };
    const auto triangle=[&](auto&& self,Vec3 pa,Vec3 pb,Vec3 pc,unsigned depth)->void {
        const auto center=scale(add(add(pa,pb),pc),1./3);
        const auto radius=std::max({length(sub(pa,center)),length(sub(pb,center)),length(sub(pc,center))});
        // These maps have local metric norm <= 1. This geometric rejection
        // also keeps the inverse chart cut on the far side out of the image.
        if(length(sub(center,image_center))>image_radius+radius)return;
        std::array<Vertex,3> vertices;
        try {vertices={Vertex{pa,uv(pa)},Vertex{pb,uv(pb)},Vertex{pc,uv(pc)}};}
        catch(const std::invalid_argument&) {
            // A target may include the antipode or cone apex even when the
            // authored image does not. Resolve its neighbourhood first.
            if(depth==12)throw std::runtime_error("Wrapped image crosses a singularity or overlaps itself.");
            const auto ab=scale(add(pa,pb),.5),bc=scale(add(pb,pc),.5),ca=scale(add(pc,pa),.5);
            self(self,pa,ab,ca,depth+1);self(self,ab,pb,bc,depth+1);self(self,ca,bc,pc,depth+1);self(self,ab,bc,ca,depth+1);return;
        }
        std::array<Vertex,3> middle;double error=0;
        for(int i=0;i<3;++i) {
            const auto linear=interpolate(vertices[i],vertices[(i+1)%3],.5);
            middle[i]={linear.point,uv(linear.point)};
            error=std::max(error,std::hypot(middle[i].uv[0]-linear.uv[0],middle[i].uv[1]-linear.uv[1]));
        }
        // Common exterior half-space, including midpoint curvature bounds.
        for(int axis=0;axis<2;++axis)for(int upper=0;upper<2;++upper) {
            const auto outside=[&](const Vertex& v){return upper?v.uv[axis]>1+error:v.uv[axis]<-error;};
            if(std::ranges::all_of(vertices,outside)&&std::ranges::all_of(middle,outside))return;
        }
        if(error>1./4096) {
            if(depth==12)throw std::runtime_error("Wrapped image crosses a singularity or overlaps itself.");
            self(self,pa,middle[0].point,middle[2].point,depth+1);
            self(self,middle[0].point,pb,middle[1].point,depth+1);
            self(self,middle[2].point,middle[1].point,pc,depth+1);
            self(self,middle[0].point,middle[1].point,middle[2].point,depth+1);return;
        }
        append_polygon({vertices.begin(),vertices.end()});
    };
    for(std::size_t i=0;i+2<target.triangles.size();i+=3)
        triangle(triangle,target.vertices.at(target.triangles[i]),target.vertices.at(target.triangles[i+1]),target.vertices.at(target.triangles[i+2]),0);
    if(result.surface_vertices.empty()&&!allow_empty)throw std::runtime_error("Wrapped image does not overlap the selected face.");
    return result;
}

// Follow only persisted Boolean ancestry, never proximity or coincident
// surfaces. A later cut may split the selected face into several descendants.
inline bool wrapped_image_face_matches(const FaceReference& face,const FaceReference& target,unsigned depth=0) {
    if(face==target)return true;
    if(depth>=64||face.instance_path!=target.instance_path)return false;
    std::string_view key=face.semantic_key;
    if(!key.starts_with("boolean:"))return false;
    const auto marker=key.find(':',8);
    if(marker==std::string_view::npos)return false;
    key.remove_prefix(marker);
    if(key.starts_with(":split-face:from:"))key.remove_prefix(17);
    else if(key.starts_with(":merged-face:from:"))key.remove_prefix(18);
    else return false;
    const auto field=[](std::string_view& input)->std::optional<std::string_view> {
        const auto colon=input.find(':');if(colon==std::string_view::npos)return {};
        std::size_t length{};const auto [end,error]=std::from_chars(input.data(),input.data()+colon,length);
        if(error!=std::errc{}||end!=input.data()+colon||length>input.size()-colon-1)return {};
        input.remove_prefix(colon+1);const auto value=input.substr(0,length);input.remove_prefix(length);return value;
    };
    while(!key.empty()&&!key.starts_with(":boundary:")) {
        const auto encoded=field(key);if(!encoded)return false;
        auto parent=*encoded;const auto owner=field(parent),semantic=field(parent),path=field(parent);
        if(!owner||!semantic||!path||!parent.empty())return false;
        FaceReference ancestor{std::string(*owner),std::string(*semantic),path->empty()?face.instance_path:std::string(*path)};
        if(wrapped_image_face_matches(ancestor,target,depth+1))return true;
    }
    return false;
}
}
