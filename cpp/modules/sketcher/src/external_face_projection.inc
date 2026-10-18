// Orthographic outlines consume calculated reference triangles and analytic
// supports. The sketch origin never participates in a plane intersection.
std::optional<std::vector<std::vector<std::array<double, 2>>>>
Sketch::external_face_reference_paths(const zima::kernel::ViewerReferenceGeometry& source,
    const zima::kernel::FaceReference& face) const {
    using P=std::array<double,2>;
    using V=zima::kernel::Vec3;
    if(!face.valid() || source.triangles.size()!=3*source.triangle_references.size())return std::nullopt;
    const auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto sub=[](V a,V b){return V{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto origin=world_point(0,0);
    auto normal=cross(sub(world_point(1,0),origin),sub(world_point(0,1),origin));
    const double nl=std::sqrt(dot(normal,normal));if(nl<=1e-12)return std::nullopt;
    normal={normal.x/nl,normal.y/nl,normal.z/nl};
    std::vector<V> vertices;
    std::map<std::array<long long,3>,std::uint32_t> welded;
    const auto vertex=[&](V p){
        const std::array key{std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)};
        auto [i,inserted]=welded.try_emplace(key,static_cast<std::uint32_t>(vertices.size()));
        if(inserted)vertices.push_back(p);return i->second;
    };
    struct Edge {unsigned count{};bool front{},back{},edge_on{};};
    std::map<std::pair<std::uint32_t,std::uint32_t>,Edge> edges;
    const zima::kernel::SurfaceGeometry* surface=nullptr;
    std::optional<V> source_normal;
    bool found=false;
    for(std::size_t i=0;i<source.triangle_references.size();++i) {
        const auto& ref=source.triangle_references[i];if(ref!=face)continue;
        found=true;if(ref.surface)surface=ref.surface.get();
        std::array<std::uint32_t,3> indices;
        for(int j=0;j<3;++j) {
            const auto index=source.triangles[3*i+j];if(index>=source.vertices.size())return std::nullopt;
            const auto point=source.vertices[index];
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||
                std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)})>9e10)return std::nullopt;
            indices[j]=vertex(point);
        }
        const auto n=cross(sub(vertices[indices[1]],vertices[indices[0]]),sub(vertices[indices[2]],vertices[indices[0]]));
        if(!source_normal && dot(n,n)>1e-24)source_normal=n;
        const double facing=dot(n,normal);
        for(int j=0;j<3;++j) {
            if(indices[j]==indices[(j+1)%3])continue;
            auto& e=edges[std::minmax(indices[j],indices[(j+1)%3])];++e.count;
            e.front|=facing>1e-14;e.back|=facing<-1e-14;e.edge_on|=std::abs(facing)<=1e-14;
        }
    }
    if(!found)return std::nullopt;
    // A complete cylindrical/conical band seen from the side has exact
    // straight generators. Certify its cap-only boundary before using the
    // analytic support; a trimmed band must retain its real boundary.
    if(surface && (surface->kind==zima::kernel::SurfaceGeometry::Kind::Cylinder ||
        surface->kind==zima::kernel::SurfaceGeometry::Kind::Cone) && std::abs(dot(surface->axis,normal))<1e-10) {
        bool complete=true,low=false,high=false;
        const auto axial=[&](V p){return dot(sub(p,surface->origin),surface->axis);};
        for(const auto& [key,e]:edges)if(e.count!=2) {
            const double a=axial(vertices[key.first]),b=axial(vertices[key.second]);
            const bool lo=std::abs(a-surface->axial_min)<1e-7&&std::abs(b-surface->axial_min)<1e-7;
            const bool hi=std::abs(a-surface->axial_max)<1e-7&&std::abs(b-surface->axial_max)<1e-7;
            complete&=e.count==1&&(lo||hi);low|=lo;high|=hi;
        }
        if(complete&&low&&high) {
            auto radial=cross(normal,surface->axis);const double length=std::sqrt(dot(radial,radial));
            radial={radial.x/length,radial.y/length,radial.z/length};
            const auto point=[&](double z,double side){
                const double r=surface->radius+(surface->kind==zima::kernel::SurfaceGeometry::Kind::Cone?z*std::tan(surface->semi_angle):0);
                return local_point({surface->origin.x+z*surface->axis.x+side*r*radial.x,
                    surface->origin.y+z*surface->axis.y+side*r*radial.y,
                    surface->origin.z+z*surface->axis.z+side*r*radial.z});
            };
            return std::vector<std::vector<P>>{{point(surface->axial_min,-1),point(surface->axial_max,-1),
                point(surface->axial_max,1),point(surface->axial_min,1),point(surface->axial_min,-1)}};
        }
    }
    // Planar faces retain their ordered outer and hole contours.
    if(const auto paths=project_external_face(source,face)) {
        bool planar=true;
        std::optional<V> plane_point,plane_normal;
        for(std::size_t i=0;i<source.triangle_references.size();++i)if(source.triangle_references[i]==face) {
            const auto a=source.vertices[source.triangles[3*i]],b=source.vertices[source.triangles[3*i+1]],c=source.vertices[source.triangles[3*i+2]];
            if(!plane_normal){auto n=cross(sub(b,a),sub(c,a));const double l=std::sqrt(dot(n,n));if(l>1e-12){plane_point=a;plane_normal=V{n.x/l,n.y/l,n.z/l};}}
            if(plane_normal)for(auto p:{a,b,c})planar&=std::abs(dot(sub(p,*plane_point),*plane_normal))<1e-7;
        }
        if(planar)return paths;
    }
    // Other faces use their calculated outer boundaries and apparent contour,
    // never the triangle diagonals or a section through the sketch origin.
    std::vector<P> projected;
    std::map<std::array<long long,2>,std::size_t> points;
    const auto index=[&](V p){const auto q=local_point(p);const std::array k{std::llround(q[0]*1e8),std::llround(q[1]*1e8)};
        auto [it,added]=points.try_emplace(k,projected.size());if(added)projected.push_back(q);return it->second;};
    std::set<std::pair<std::size_t,std::size_t>> unused;
    std::map<std::size_t,std::vector<std::size_t>> adjacent;
    for(const auto& [key,e]:edges)if(e.count==1||(e.front&&e.back)||(e.edge_on&&(e.front||e.back))) {
        const auto a=index(vertices[key.first]),b=index(vertices[key.second]);if(a==b)continue;
        if(unused.insert(std::minmax(a,b)).second){adjacent[a].push_back(b);adjacent[b].push_back(a);}
    }
    std::vector<std::vector<P>> paths;
    while(!unused.empty()) {
        auto [first,current]=*unused.begin();
        if(adjacent[first].size()==2&&adjacent[current].size()!=2)std::swap(first,current);
        unused.erase(std::minmax(first,current));std::vector<P> path{projected[first],projected[current]};
        while(current!=first&&adjacent[current].size()==2) {
            const auto next=std::ranges::find_if(adjacent[current],[&](auto n){return unused.contains(std::minmax(current,n));});
            if(next==adjacent[current].end())break;
            unused.erase(std::minmax(current,*next));current=*next;path.push_back(projected[current]);
        }
        paths.push_back(std::move(path));
    }
    if(paths.empty())return std::nullopt;
    // An edge-on planar face remains an oriented straight support. Derive
    // direction from its persisted triangle orientation and the Sketch normal,
    // so signed dimensions retain their side through frame rotation and refresh.
    SketchExternalReference straight;straight.kind=ExternalReferenceKind::Face;
    straight.cached_paths=paths;
    if(source_normal && external_reference_line(straight)) {
        auto direction=cross(*source_normal,normal);const double length=std::sqrt(dot(direction,direction));
        if(length>1e-12) {
            direction={direction.x/length,direction.y/length,direction.z/length};
            const auto local=local_point({origin.x+direction.x,origin.y+direction.y,origin.z+direction.z});
            const auto [first,last]=std::ranges::minmax_element(projected,{},[&](const auto& p){return p[0]*local[0]+p[1]*local[1];});
            return std::vector<std::vector<P>>{{*first,*last}};
        }
    }
    return paths;
}
