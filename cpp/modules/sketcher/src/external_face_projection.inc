// Exact side-on generators of a full or angularly bounded analytic band.
// Boundary certification uses persisted triangles; no kernel call or new IDs.
static std::array<std::optional<std::array<kernel::Vec3,2>>,2>
analytic_band_generators(const kernel::ViewerReferenceGeometry& source,
        const kernel::FaceReference& face,kernel::Vec3 normal) {
    using V=kernel::Vec3;
    std::array<std::optional<std::array<V,2>>,2> result;
    const auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto sub=[](V a,V b){return V{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto unit=[&](V v)->std::optional<V>{const double l=std::sqrt(dot(v,v));if(l<1e-12)return {};return V{v.x/l,v.y/l,v.z/l};};
    if(source.triangles.size()!=3*source.triangle_references.size())return result;
    const kernel::SurfaceGeometry* surface{};
    std::vector<V> points;
    std::map<std::array<long long,3>,unsigned> welded;
    std::map<std::pair<unsigned,unsigned>,unsigned> boundary;
    for(std::size_t i=0;i<source.triangle_references.size();++i) {
        const auto& reference=source.triangle_references[i];if(reference!=face)continue;
        if(!surface)surface=(reference.contour_surface?reference.contour_surface:reference.surface).get();
        std::array<unsigned,3> ids;
        for(unsigned j=0;j<3;++j) {
            const auto index=source.triangles[3*i+j];if(index>=source.vertices.size())return result;
            const auto p=source.vertices[index];
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>9e10)return result;
            const std::array key{std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)};
            auto [it,added]=welded.try_emplace(key,static_cast<unsigned>(points.size()));
            if(added)points.push_back(p);ids[j]=it->second;
        }
        for(unsigned j=0;j<3;++j)if(ids[j]!=ids[(j+1)%3])++boundary[std::minmax(ids[j],ids[(j+1)%3])];
    }
    if(!surface||(surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder&&surface->kind!=kernel::SurfaceGeometry::Kind::Cone))return result;
    const auto& s=*surface;const auto n=unit(normal);
    if(!n||std::abs(dot(*n,s.axis))>1e-10)return result;
    const auto radial=unit(cross(*n,s.axis));if(!radial)return result;
    const auto axial=[&](V p){return dot(sub(p,s.origin),s.axis);};
    const auto ray=[&](V p)->std::optional<V>{const auto z=axial(p);const auto d=sub(p,s.origin);return unit({d.x-z*s.axis.x,d.y-z*s.axis.y,d.z-z*s.axis.z});};
    std::array<std::vector<std::pair<V,V>>,2> caps;
    for(const auto& [indices,count]:boundary)if(count!=2) {
        if(count!=1)return result;
        const auto a=points[indices.first],b=points[indices.second];
        const double za=axial(a),zb=axial(b);
        const auto ra=ray(a),rb=ray(b);if(!ra||!rb)return result;
        const bool low=std::abs(za-s.axial_min)<1e-7&&std::abs(zb-s.axial_min)<1e-7;
        const bool high=std::abs(za-s.axial_max)<1e-7&&std::abs(zb-s.axial_max)<1e-7;
        if(low||high) {
            // Adjacent samples must certify the local angular arc unambiguously.
            if(dot(*ra,*rb)<0)return result;
            caps[high?1:0].push_back({*ra,*rb});
        } else if(dot(*ra,*rb)<1.-1e-10)return result;
    }
    const auto contains=[&](const auto& arcs,V q) {
        return std::ranges::any_of(arcs,[&](const auto& arc) {
            const auto angle=[&](V p){return std::atan2(dot(s.axis,cross(arc.first,p)),dot(arc.first,p));};
            const double sweep=angle(arc.second),part=angle(q);
            return sweep>=0 ? part>=-1e-10&&part<=sweep+1e-10 : part<=1e-10&&part>=sweep-1e-10;
        });
    };
    for(unsigned i=0;i<2;++i) {
        const double sign=i?1.:-1.;const V direction{sign*radial->x,sign*radial->y,sign*radial->z};
        if(!contains(caps[0],direction)||!contains(caps[1],direction))continue;
        const auto point=[&](double z){const double r=s.radius+(s.kind==kernel::SurfaceGeometry::Kind::Cone?z*std::tan(s.semi_angle):0.);
            return V{s.origin.x+z*s.axis.x+r*direction.x,s.origin.y+z*s.axis.y+r*direction.y,s.origin.z+z*s.axis.z+r*direction.z};};
        result[i]=std::array{point(s.axial_min),point(s.axial_max)};
    }
    return result;
}

// Orthographic outlines consume calculated reference triangles and analytic
// supports. The sketch origin never participates in a plane intersection.
std::optional<std::vector<std::vector<std::array<double, 2>>>>
Sketch::external_face_reference_paths(const zima::kernel::ViewerReferenceGeometry& source,
    const zima::kernel::FaceReference& face) const {
    using P=std::array<double,2>;
    using V=zima::kernel::Vec3;
    if(is_face_curve_key(face.semantic_key)) {
        auto support=face;support.semantic_key=face_curve_source_key(face.semantic_key);
        const auto tag=std::string_view(face.semantic_key).substr(face_curve_prefix.size(),face.semantic_key.find(':',face_curve_prefix.size())-face_curve_prefix.size());
        if(tag!="negative"&&tag!="positive")return std::nullopt;
        const auto o=world_point(0,0),x=world_point(1,0),y=world_point(0,1);
        const V a{x.x-o.x,x.y-o.y,x.z-o.z},b{y.x-o.x,y.y-o.y,y.z-o.z};
        const V normal{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        const auto generators=analytic_band_generators(source,support,normal);
        const auto& selected=generators[tag=="positive"?1:0];
        if(!selected)return std::nullopt;
        return std::vector<std::vector<P>>{{local_point((*selected)[0]),local_point((*selected)[1])}};
    }
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
        found=true;if(ref.contour_surface||ref.surface)surface=(ref.contour_surface?ref.contour_surface:ref.surface).get();
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

std::vector<kernel::ViewerEdge> external_face_curve_edges(
    const kernel::ViewerReferenceGeometry& source,kernel::Vec3 origin,
    kernel::Vec3 x_axis,kernel::Vec3 y_axis) {
    Sketch frame;frame.owner_container_id="projection-frame";
    frame.resolved_origin=origin;frame.resolved_x_axis=x_axis;frame.resolved_y_axis=y_axis;
    frame.resolved_normal={x_axis.y*y_axis.z-x_axis.z*y_axis.y,
        x_axis.z*y_axis.x-x_axis.x*y_axis.z,x_axis.x*y_axis.y-x_axis.y*y_axis.x};
    using Identity=std::tuple<std::string,std::string,std::string>;
    std::map<Identity,std::vector<std::size_t>> groups;
    for(std::size_t i=0;i<source.triangle_references.size();++i) {
        const auto& face=source.triangle_references[i];
        const auto support=face.contour_surface?face.contour_surface:face.surface;
        if(!support || (support->kind!=kernel::SurfaceGeometry::Kind::Cylinder&&
            support->kind!=kernel::SurfaceGeometry::Kind::Cone))continue;
        if(std::abs(support->axis.x*frame.resolved_normal.x+
            support->axis.y*frame.resolved_normal.y+support->axis.z*frame.resolved_normal.z)>1e-10)continue;
        groups[{face.owner_id,face.semantic_key,face.instance_path}].push_back(i);
    }
    std::vector<kernel::ViewerEdge> result;
    for(const auto& [identity,slots]:groups) {
        // Bound work to each face, not all triangles for every candidate face.
        kernel::ViewerReferenceGeometry band;std::map<std::uint32_t,std::uint32_t> indices;
        for(const auto slot:slots) {
            if(3*slot+2>=source.triangles.size())continue;
            bool valid=true;for(unsigned j=0;j<3;++j)valid&=source.triangles[3*slot+j]<source.vertices.size();
            if(!valid)continue;
            band.triangle_references.push_back(source.triangle_references[slot]);
            for(unsigned j=0;j<3;++j) {
                const auto old=source.triangles[3*slot+j];
                auto [it,added]=indices.try_emplace(old,static_cast<std::uint32_t>(band.vertices.size()));
                if(added)band.vertices.push_back(source.vertices[old]);
                band.triangles.push_back(it->second);
            }
        }
        if(band.triangle_references.empty())continue;
        const auto& face=band.triangle_references.front();
        const auto generators=analytic_band_generators(band,face,frame.resolved_normal);
        for(unsigned i=0;i<2;++i)if(generators[i]) {
            kernel::ViewerEdge edge;
            edge.reference={face.owner_id,face_curve_key(face.semantic_key,i?1:-1),face.instance_path};
            // Offer the actual source generator, preserving its world depth.
            // Only the accepted reference cache is projected onto the Sketch.
            edge.points={(*generators[i])[0],(*generators[i])[1]};
            edge.overlay=true;result.push_back(std::move(edge));
        }
    }
    return result;
}
