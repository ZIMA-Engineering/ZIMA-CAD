#include <zima/render/renderer.hpp>
#include <zima/render/spot_cone.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace zima::render {
namespace {
using V=kernel::Vec3;
constexpr double pi=std::numbers::pi;
V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V operator-(V a){return {-a.x,-a.y,-a.z};}
V operator*(V a,double b){return {a.x*b,a.y*b,a.z*b};}
V operator*(double b,V a){return a*b;}
V operator/(V a,double b){return a*(1/b);}
V product(V a,V b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double length(V a){return std::hypot(a.x,a.y,a.z);}
V unit(V a){const auto n=length(a);return n>1e-30?a/n:V{0,0,1};}
V mix(V a,V b,double t){return a*(1-t)+b*t;}
V from(const std::array<double,3>& a){return {a[0],a[1],a[2]};}
double component(V a,unsigned axis){return axis==0?a.x:axis==1?a.y:a.z;}
V lower(V a,V b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
V upper(V a,V b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
double linear(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
V linear(V a){return {linear(a.x),linear(a.y),linear(a.z)};}
double srgb(double x){return x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;}
V rotate(const document::Placement& p,V a) {
    const double x=p.rotation_x*pi/180,y=p.rotation_y*pi/180,z=p.rotation_z*pi/180;
    a={a.x,std::cos(x)*a.y-std::sin(x)*a.z,std::sin(x)*a.y+std::cos(x)*a.z};
    a={std::cos(y)*a.x+std::sin(y)*a.z,a.y,-std::sin(y)*a.x+std::cos(y)*a.z};
    return {std::cos(z)*a.x-std::sin(z)*a.y,std::sin(z)*a.x+std::cos(z)*a.y,a.z};
}
V position(const document::RenderObject& o){return {o.placement.x,o.placement.y,o.placement.z};}
struct Random {
    std::uint64_t state;
    double next(){state+=0x9e3779b97f4a7c15ULL;auto x=state;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;return double((x^(x>>31))>>11)*0x1.0p-53;}
};
V basis(V normal,V local) {
    const V tangent=unit(cross(std::abs(normal.z)<.9?V{0,0,1}:V{1,0,0},normal));
    return tangent*local.x+cross(normal,tangent)*local.y+normal*local.z;
}
struct Bounds {
    V low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
    void add(V p){low=lower(low,p);high=upper(high,p);}
    bool intersects(const Ray& ray,double minimum,double maximum) const {
        for(unsigned i=0;i<3;++i) {
            const auto o=component(ray.origin,i),d=component(ray.direction,i);
            if(std::abs(d)<1e-30){if(o<component(low,i)||o>component(high,i))return false;continue;}
            auto a=(component(low,i)-o)/d,b=(component(high,i)-o)/d;if(a>b)std::swap(a,b);
            minimum=std::max(minimum,a);maximum=std::min(maximum,b);if(maximum<minimum)return false;
        }
        return true;
    }
};
struct Material {V color;double roughness,metallic,opacity{1};};
Material material(const kernel::SurfaceStyle& style) {
    if((style.color.size()!=7&&style.color.size()!=9)||style.color[0]!='#'||!std::isfinite(style.roughness)||!std::isfinite(style.metallic))
        throw std::invalid_argument("Invalid Render appearance.");
    const auto channel=[&](unsigned i){std::size_t end{};const auto n=std::stoul(style.color.substr(i,2),&end,16);
        if(end!=2)throw std::invalid_argument("Invalid Render appearance.");return n/255.;};
    const unsigned offset=style.color.size()==9?3:1;
    return {linear(V{channel(offset),channel(offset+2),channel(offset+4)}),std::clamp(style.roughness,0.,1.),std::clamp(style.metallic,0.,1.),offset==3?channel(1):1.};
}
struct Triangle {
    V a,b,c,normal,na,nb,nc;
    Material material;
    std::shared_ptr<const kernel::SurfaceGeometry> surface;
    Bounds bounds;
    V center() const{return (a+b+c)/3;}
};
struct Node {Bounds bounds;unsigned first{},count{},left{},right{};};
struct Hit {double t{INFINITY},u{},v{};unsigned triangle{};};
struct Sample {V radiance;bool covered{};};
bool triangle_hit(const Triangle& t,const Ray& ray,double minimum,Hit& hit,unsigned index) {
    const auto ab=t.b-t.a,ac=t.c-t.a,p=cross(ray.direction,ac);
    const double det=dot(ab,p);if(std::abs(det)<1e-14*length(ab)*length(ac))return false;
    const auto s=ray.origin-t.a;const double u=dot(s,p)/det;if(u<0||u>1)return false;
    const auto q=cross(s,ab);const double v=dot(ray.direction,q)/det;if(v<0||u+v>1)return false;
    const double distance=dot(ac,q)/det;if(distance<minimum||distance>=hit.t)return false;
    hit={distance,u,v,index};return true;
}
V shading_normal(const Triangle& t,const Hit& hit,V point) {
    auto normal=unit(t.na*(1-hit.u-hit.v)+t.nb*hit.u+t.nc*hit.v);
    if(t.surface) {
        const auto& s=*t.surface;const auto axis=unit(s.axis),relative=point-s.origin;
        const auto radial=relative-axis*dot(relative,axis);
        switch(s.kind) {
        case kernel::SurfaceGeometry::Kind::Plane:normal=axis;break;
        case kernel::SurfaceGeometry::Kind::Cylinder:normal=unit(radial);break;
        case kernel::SurfaceGeometry::Kind::Cone:normal=unit(radial)*std::cos(s.semi_angle)-axis*std::sin(s.semi_angle);break;
        case kernel::SurfaceGeometry::Kind::Torus:normal=unit(relative-unit(radial)*s.radius);break;
        }
    }
    return dot(normal,t.normal)<0?-normal:normal;
}
double distribution(double nh,double alpha) {
    const double a2=alpha*alpha,d=nh*nh*(a2-1)+1;return a2/(pi*d*d);
}
double masking(double cosine,double alpha) {
    return 2*cosine/(cosine+std::sqrt(alpha*alpha+(1-alpha*alpha)*cosine*cosine));
}
V brdf(const Material& m,V n,V view,V light) {
    const auto nv=std::max(0.,dot(n,view)),nl=std::max(0.,dot(n,light));if(nv<=0||nl<=0)return {};
    const auto h=unit(view+light);const double vh=std::max(0.,dot(view,h)),nh=std::max(0.,dot(n,h));
    const auto f0=mix({.04,.04,.04},m.color,m.metallic);
    const auto f=f0+(V{1,1,1}-f0)*std::pow(1-vh,5);
    const auto alpha=std::max(.025,m.roughness*m.roughness);
    const auto spec=f*(distribution(nh,alpha)*masking(nv,alpha)*masking(nl,alpha)/(4*nv*nl));
    return product(V{1,1,1}-f,m.color)*((1-m.metallic)/pi)+spec;
}
V bounce_direction(const Material& m,V n,V view,Random& random,double& pdf) {
    const auto alpha=std::max(.025,m.roughness*m.roughness),p=.25+.5*m.metallic;
    const double choice=random.next(),a=random.next(),b=random.next();V direction;
    if(choice<p) {
        const double cosine=std::sqrt((1-a)/(1+(alpha*alpha-1)*a)),sine=std::sqrt(std::max(0.,1-cosine*cosine));
        const auto half=basis(n,{sine*std::cos(2*pi*b),sine*std::sin(2*pi*b),cosine});
        direction=half*(2*dot(view,half))-view;
    } else {
        const double r=std::sqrt(a);direction=basis(n,{r*std::cos(2*pi*b),r*std::sin(2*pi*b),std::sqrt(1-a)});
    }
    const double nl=std::max(0.,dot(n,direction));const auto h=unit(view+direction);
    const auto nh=std::max(0.,dot(n,h)),vh=std::max(1e-15,dot(view,h));
    pdf=p*distribution(nh,alpha)*nh/(4*vh)+(1-p)*nl/pi;return unit(direction);
}
} // namespace

struct Scene::Data {
    document::RenderDefinition definition;
    document::RenderObject camera;
    struct Light {document::RenderObject object;V spot_direction;SpotCone cone;};
    std::vector<Light> lights;
    std::vector<Triangle> triangles;
    std::vector<unsigned> indices;
    std::vector<Node> nodes;
    double epsilon{};
    unsigned build(unsigned first,unsigned count,std::stop_token stop) {
        if(stop.stop_requested())throw std::runtime_error("Render cancelled.");
        const auto index=static_cast<unsigned>(nodes.size());nodes.emplace_back();Bounds bounds,centers;
        for(unsigned i=first;i<first+count;++i){const auto& t=triangles[indices[i]];bounds.add(t.bounds.low);bounds.add(t.bounds.high);centers.add(t.center());}
        nodes[index].bounds=bounds;
        if(count<=8){nodes[index].first=first;nodes[index].count=count;return index;}
        const auto ext=centers.high-centers.low;const unsigned axis=ext.x>=ext.y&&ext.x>=ext.z?0:ext.y>=ext.z?1:2;
        const auto middle=first+count/2;
        std::nth_element(indices.begin()+first,indices.begin()+middle,indices.begin()+first+count,
            [&](auto a,auto b){return component(triangles[a].center(),axis)<component(triangles[b].center(),axis);});
        const auto left=build(first,count/2,stop),right=build(middle,count-count/2,stop);
        nodes[index].left=left;nodes[index].right=right;return index;
    }
    bool intersect(const Ray& ray,Hit& hit,std::uint64_t& rays,bool any=false) const {
        ++rays;unsigned stack[64],size=1;stack[0]=0;bool found=false;
        while(size) {
            const auto& node=nodes[stack[--size]];if(!node.bounds.intersects(ray,epsilon,hit.t))continue;
            if(node.count)for(unsigned i=node.first;i<node.first+node.count;++i) {
                if(triangle_hit(triangles[indices[i]],ray,epsilon,hit,indices[i])){if(any)return true;found=true;}
            } else {stack[size++]=node.left;stack[size++]=node.right;}
        }
        return found;
    }
    V background(V direction) const {
        const double t=std::clamp(.5+.5*direction.z,0.,1.);
        return mix(linear(from(definition.settings.background_bottom)),linear(from(definition.settings.background_top)),t);
    }
    double visibility(Ray ray,double distance,std::uint64_t& rays,std::stop_token stop) const {
        double transmission=1;
        while(!stop.stop_requested()) {
            Hit hit;hit.t=distance;
            if(!intersect(ray,hit,rays))break;
            transmission*=1-triangles[hit.triangle].material.opacity;
            if(transmission==0)break;
            ray.origin=ray.origin+ray.direction*(hit.t+epsilon*2);
            distance-=hit.t+epsilon*2;if(distance<=epsilon)break;
        }
        return transmission;
    }
    Sample trace(Ray ray,Random& random,std::uint64_t& rays,std::stop_token stop) const {
        V sum{},weight{1,1,1};const auto& settings=definition.settings;bool covered=false;
        for(unsigned bounce=0;bounce<settings.bounces;) {
            if(stop.stop_requested())return {};
            Hit hit;
            if(!intersect(ray,hit,rays)) {
                // Composite primary misses with the screen-space View gradient
                // after exposure/tone mapping. Reflections still see the HDR
                // environment and use its independently authored intensity.
                if(covered)sum=sum+product(weight,background(ray.direction))*settings.environment_strength;break;
            }
            const auto& triangle=triangles[hit.triangle];const auto point=ray.origin+ray.direction*hit.t;
            if(triangle.material.opacity<1&&random.next()>=triangle.material.opacity) {
                ray.origin=point+ray.direction*(epsilon*2);continue;
            }
            covered=true;
            auto ng=triangle.normal,n=shading_normal(triangle,hit,point);
            if(dot(ng,ray.direction)>0){ng=-ng;n=-n;}
            const auto view=-ray.direction;const auto start=point+ng*epsilon;
            for(const auto& prepared:lights) {
                const auto& light=prepared.object;
                V direction,radiance;double distance=INFINITY;
                const double a=random.next(),b=random.next(),angle=2*pi*b;
                if(light.kind==document::RenderObjectKind::Sun) {
                    const auto axis=-rotate(light.placement,{1,0,0});
                    const double cosine=1-a*(1-std::cos(light.angular_radius_degrees*pi/180));
                    const double sine=std::sqrt(std::max(0.,1-cosine*cosine));
                    direction=basis(axis,{sine*std::cos(angle),sine*std::sin(angle),cosine});
                    radiance=from(light.color)*(light.intensity/1000.);
                } else {
                    const auto delta=position(light)-point;const double center_distance=length(delta);
                    if(center_distance<=epsilon)continue;
                    const double radius=light.radius_mm*std::sqrt(a);
                    const auto sampled=position(light)+basis(unit(delta),{radius*std::cos(angle),radius*std::sin(angle),0});
                    direction=sampled-start;distance=length(direction);direction=direction/distance;
                    radiance=from(light.color)*(light.intensity/(4*pi*std::max(1e-12,center_distance*center_distance*1e-6)*1000.));
                    if(light.kind==document::RenderObjectKind::SpotLight) {
                        const double falloff=prepared.cone.attenuation(dot(prepared.spot_direction,-delta/center_distance));
                        if(falloff==0)continue;
                        radiance=radiance*(4*pi*falloff/prepared.cone.solid_angle);
                    }
                }
                const double nl=dot(n,direction);if(nl<=0||dot(ng,direction)<=0)continue;
                const auto transmission=visibility({start,direction},distance-epsilon,rays,stop);
                sum=sum+product(weight,product(brdf(triangle.material,n,view,direction),radiance))*(nl*transmission);
            }
            double pdf{};const auto direction=bounce_direction(triangle.material,n,view,random,pdf);
            const double cosine=dot(n,direction);if(pdf<=1e-20||cosine<=0||dot(ng,direction)<=0)break;
            weight=product(weight,brdf(triangle.material,n,view,direction))*(cosine/pdf);
            if(bounce>=2) {
                const double chance=std::clamp(std::max({weight.x,weight.y,weight.z}),.05,.95);
                if(random.next()>chance)break;weight=weight/chance;
            }
            ray={start,direction};
            ++bounce;
        }
        return {sum,covered};
    }
};

Scene::Scene(SceneInput input,std::stop_token stop) {
    document::validate_render_definition(input.definition);
    auto data=std::make_shared<Data>();data->definition=std::move(input.definition);
    bool camera=false;
    for(const auto& o:data->definition.objects) {
        const bool used=o.id==data->definition.active_camera_id||(o.enabled&&o.kind!=document::RenderObjectKind::Camera);
        if(used&&!o.placement.reference_valid)throw std::invalid_argument("The Render object placement is unresolved.");
        if(o.id==data->definition.active_camera_id){data->camera=o;camera=true;}
        if(o.enabled&&o.kind!=document::RenderObjectKind::Camera)
            data->lights.push_back({o,rotate(o.placement,{1,0,0}),SpotCone(o.cone_angle_degrees,o.edge_softness)});
    }
    if(!camera)throw std::invalid_argument("Select an active Render camera.");
    std::map<std::string,kernel::SurfaceStyle> face_styles;
    for(const auto& group:input.appearance.groups)for(const auto& face:group.faces)face_styles[face]=group.style;
    for(const auto& source:input.sources) {
        const auto& mesh=source.body->mesh;
        if(mesh.triangles.size()%3)throw std::invalid_argument("Invalid Render source mesh.");
        // Average within a persisted face only. Coincident vertices on adjacent
        // sharp faces must not share a normal; exact analytic normals take priority.
        using Key=std::tuple<double,double,double,std::string,std::string>;
        std::map<Key,V> normals;
        const auto key=[](V p,const kernel::FaceReference& ref){return Key{p.x,p.y,p.z,ref.owner_id,ref.semantic_key};};
        const auto base=data->triangles.size();std::vector<kernel::FaceReference> references;
        for(std::size_t i=0;i<mesh.triangles.size();i+=3) {
            if((i&1023)==0&&stop.stop_requested())throw std::runtime_error("Render cancelled.");
            for(unsigned j=0;j<3;++j)if(mesh.triangles[i+j]>=mesh.vertices.size())throw std::invalid_argument("Invalid Render source mesh.");
            const auto a=mesh.vertices[mesh.triangles[i]],b=mesh.vertices[mesh.triangles[i+1]],c=mesh.vertices[mesh.triangles[i+2]];
            for(const auto v:{a,b,c})if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::invalid_argument("Invalid Render source mesh.");
            const auto cross_normal=cross(b-a,c-a);if(length(cross_normal)<1e-24)continue;
            const auto ref=i/3<mesh.triangle_references.size()?mesh.triangle_references[i/3]:kernel::FaceReference{};
            auto style=input.appearance.body;
            auto body_id=source.body_id;
            if(const auto it=input.appearance.owner_bodies.find(ref.owner_id);it!=input.appearance.owner_bodies.end())body_id=it->second;
            if(const auto it=input.appearance.bodies.find(body_id);it!=input.appearance.bodies.end())style=it->second;
            if(const auto it=face_styles.find(ref.owner_id+"::"+ref.semantic_key);it!=face_styles.end())style=it->second;
            if(!data->definition.settings.use_model_finish){style.roughness=data->definition.settings.roughness;style.metallic=data->definition.settings.metallic;}
            Triangle triangle{a,b,c,unit(cross_normal),{},{},{},material(style),ref.surface};
            triangle.bounds.add(a);triangle.bounds.add(b);triangle.bounds.add(c);
            data->triangles.push_back(triangle);references.push_back(ref);
            for(const auto p:{a,b,c})normals[key(p,ref)]=normals[key(p,ref)]+cross_normal;
        }
        for(std::size_t i=base;i<data->triangles.size();++i) {
            auto& t=data->triangles[i];const auto& ref=references[i-base];
            t.na=ref.valid()?unit(normals[key(t.a,ref)]):t.normal;
            t.nb=ref.valid()?unit(normals[key(t.b,ref)]):t.normal;
            t.nc=ref.valid()?unit(normals[key(t.c,ref)]):t.normal;
        }
    }
    if(data->triangles.empty())throw std::invalid_argument("No visible calculated Bodies to render.");
    data->indices.resize(data->triangles.size());std::iota(data->indices.begin(),data->indices.end(),0u);
    data->nodes.reserve(data->triangles.size()*2);data->build(0,static_cast<unsigned>(data->triangles.size()),stop);
    data->epsilon=std::max(1e-7,length(data->nodes[0].bounds.high-data->nodes[0].bounds.low)*1e-8);
    data_=std::move(data);
}
std::size_t Scene::triangle_count() const{return data_->triangles.size();}
V Scene::bounds_min() const{return data_->nodes[0].bounds.low;}
V Scene::bounds_max() const{return data_->nodes[0].bounds.high;}
Ray Scene::camera_ray(const document::RenderObject& camera,unsigned width,unsigned height,double x,double y) {
    const double aspect=double(width)/height,px=(2*x/width-1)*aspect,py=1-2*y/height;
    const auto forward=rotate(camera.placement,{1,0,0}),right=rotate(camera.placement,{0,-1,0}),up=rotate(camera.placement,{0,0,1});
    if(camera.orthographic)return {position(camera)+(right*px+up*py)*(camera.orthographic_height_mm*.5),forward};
    const auto scale=std::tan(camera.field_of_view_degrees*pi/360);
    return {position(camera),unit(forward+(right*px+up*py)*scale)};
}
Image Scene::render(std::stop_token stop,const std::function<void(const Image&)>& progress,unsigned workers) const {
    const auto& settings=data_->definition.settings;
    Image image;image.width=settings.width;image.height=settings.height;
    const auto count=std::size_t(image.width)*image.height;image.rgba.resize(count*4);std::vector<V> sums(count);std::vector<unsigned> coverage(count);
    workers=std::clamp(workers?workers:std::thread::hardware_concurrency(),1u,32u);
    constexpr unsigned tile=16;const auto columns=(image.width+tile-1)/tile,rows=(image.height+tile-1)/tile;
    std::vector<std::uint64_t> rays(workers);
    const double exposure=std::exp2(settings.exposure_ev);
    const auto encode=[&](double x){
        x=std::max(0.,x*exposure);const double tone=x*(2.51*x+.03)/(x*(2.43*x+.59)+.14);
        return srgb(std::clamp(tone,0.,1.));
    };
    for(unsigned sample=0;sample<settings.samples&&!stop.stop_requested();++sample) {
        std::atomic<unsigned> next{};std::vector<std::jthread> threads;
        for(unsigned worker=0;worker<workers;++worker)threads.emplace_back([&,worker]{
            for(auto t=next.fetch_add(1);t<columns*rows&&!stop.stop_requested();t=next.fetch_add(1)) {
                const unsigned x0=(t%columns)*tile,y0=(t/columns)*tile;
                for(unsigned y=y0;y<std::min(y0+tile,image.height)&&!stop.stop_requested();++y)
                    for(unsigned x=x0;x<std::min(x0+tile,image.width);++x) {
                        const auto i=std::size_t(y)*image.width+x;
                        Random random{(i+1)*0xd1342543de82ef95ULL+sample*0x9e3779b97f4a7c15ULL};
                        const double dx=random.next(),dy=random.next();
                        const auto value=data_->trace(camera_ray(data_->camera,image.width,image.height,x+dx,y+dy),random,rays[worker],stop);
                        sums[i]=sums[i]+value.radiance;coverage[i]+=value.covered;
                    }
            }
        });
        threads.clear(); // Join before publishing pixels; no concurrent image access.
        if(stop.stop_requested())break;
        image.completed_samples=sample+1;
        for(std::size_t i=0;i<count;++i) {
            const auto color=coverage[i]?sums[i]/coverage[i]:V{};
            const auto background=mix(from(settings.background_top),from(settings.background_bottom),(double(i/image.width)+.5)/image.height);
            const auto display=mix(background,V{encode(color.x),encode(color.y),encode(color.z)},double(coverage[i])/image.completed_samples);
            for(unsigned channel=0;channel<3;++channel)image.rgba[4*i+channel]=static_cast<std::uint8_t>(std::lround(255*std::clamp(component(display,channel),0.,1.)));
            image.rgba[4*i+3]=255;
        }
        image.rays=std::accumulate(rays.begin(),rays.end(),std::uint64_t{});
        if(progress)progress(image);
    }
    image.cancelled=stop.stop_requested();return image;
}
} // namespace zima::render
