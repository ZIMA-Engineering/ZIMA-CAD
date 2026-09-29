// General surfaces deliberately use the persisted source triangulation. The
// user approved this bounded approximation on 2026-09-29. Analytic surfaces
// keep their existing solver. Additional point/axis/plane references are
// solved against each finite patch, not against an infinite tangent plane.
struct PlacementMeshSolutions {
    std::vector<kernel::Vec3> points;
    bool discrete{};
};
std::optional<PlacementMeshSolutions> placement_mesh_solutions(
        const std::vector<std::reference_wrapper<const ConstructionReference>>& refs,
        const kernel::ViewerReferenceGeometry& geometry,kernel::Vec3 seed,bool alternatives=false) {
    const ConstructionReference* mesh{};
    for(const auto& wrapped:refs)if(placement_mesh::is_surface(wrapped.get(),geometry)) {
        if(mesh)return PlacementMeshSolutions{};
        mesh=&wrapped.get();
    }
    if(!mesh)return {};
    PlacementMeshSolutions result;
    if(!std::isfinite(mesh->offset))return result;
    std::vector<std::pair<kernel::Vec3,double>> equations;
    const auto axis_equations=[&](const PlacementReferenceAxis& axis) {
        const auto normal=placement_vec_normalized(placement_vec_cross(axis.direction,
            std::abs(axis.direction.x)<.8?kernel::Vec3{1,0,0}:kernel::Vec3{0,1,0}));
        const auto second=placement_vec_cross(axis.direction,normal);
        equations.emplace_back(normal,placement_vec_dot(normal,axis.point));
        equations.emplace_back(second,placement_vec_dot(second,axis.point));
    };
    for(const auto& wrapped:refs) {
        const auto& ref=wrapped.get();if(&ref==mesh)continue;
        if(const auto p=placement_reference_point(ref,geometry)) {
            equations.insert(equations.end(),{{{1,0,0},p->x},{{0,1,0},p->y},{{0,0,1},p->z}});
        } else if(const auto axis=placement_reference_axis(ref,geometry,seed,true)) {
            if(ref.offset!=0)return result;
            const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e){return placement_reference_matches(e.reference,ref);});
            if(!ref.use_axis && edge!=geometry.edges.end() && edge->exact_spline &&
                    !placement_straight_direction(edge->exact_spline->poles))return result;
            axis_equations(*axis);
        } else if(construction_reference_is_planar_face(ref,geometry)) {
            const auto plane=placement_reference_plane(ref,geometry);if(!plane)return result;
            equations.emplace_back(plane->normal,placement_vec_dot(plane->normal,plane->point)+ref.offset);
        } else return result; // Never accept a locally linearized incompatible constraint.
    }
    std::vector<kernel::Vec3> basis;
    for(const auto& [normal,rhs]:equations) {
        auto n=normal;for(const auto b:basis)n=placement_vec_sub(n,placement_vec_scale(b,placement_vec_dot(n,b)));
        if(!placement_vec_is_zero(n))basis.push_back(placement_vec_normalized(n));
    }
    result.discrete=basis.size()>=2;
    double best_distance=std::numeric_limits<double>::infinity();
    std::optional<kernel::Vec3> best;
    placement_mesh::visit(*mesh,geometry,[&](const placement_mesh::Triangle& triangle) {
        placement_mesh::Polygon polygon;polygon.count=3;
        for(int i=0;i<3;++i)polygon.points[i]=placement_vec_add(triangle.points[i],placement_vec_scale(triangle.normal,mesh->offset));
        for(const auto& [normal,rhs]:equations) {
            polygon=placement_mesh::intersect(polygon,normal,rhs);
            if(!polygon.count)return;
        }
        const auto point=placement_mesh::closest(polygon,triangle.normal,seed);
        for(const auto& [normal,rhs]:equations)
            if(std::abs(placement_vec_dot(normal,point)-rhs)>placement_mesh::tolerance)return;
        // A line contained in the surface still leaves one degree of freedom.
        if(polygon.count>1)result.discrete=false;
        const double distance=placement_mesh::square(placement_mesh::sub(point,seed));
        if(distance<best_distance || (distance==best_distance && best &&
                std::tie(point.x,point.y,point.z)<std::tie(best->x,best->y,best->z))) {best_distance=distance;best=point;}
        if(alternatives && result.discrete)result.points.push_back(point);
    });
    if(!alternatives || !result.discrete) {
        result.points.clear();if(best)result.points.push_back(*best);
    } else {
        std::ranges::sort(result.points,[](const auto& a,const auto& b){return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);});
        // Adjacent triangles may share the same contact; do not offer it twice.
        std::vector<kernel::Vec3> unique;
        for(const auto p:result.points) {
            if(std::ranges::none_of(unique,[&](auto q){return placement_mesh::square(placement_mesh::sub(p,q))<=1e-12;}))unique.push_back(p);
        }
        result.points=std::move(unique);
    }
    return result;
}

std::optional<PlacementReferencePlane> placement_contact_plane(
        const ConstructionReference& ref,const kernel::ViewerReferenceGeometry& geometry,
        kernel::Vec3 at={}) {
    if(placement_mesh::is_surface(ref,geometry)) {
        const auto contact=placement_mesh::project(ref,geometry,at);if(!contact)return {};
        const auto normal=contact->normal;
        const auto front=placement_vec_normalized(placement_vec_cross(
            std::abs(normal.x)<.8?kernel::Vec3{1,0,0}:kernel::Vec3{0,1,0},normal));
        return PlacementReferencePlane{contact->point,normal,front,placement_vec_cross(normal,front)};
    }
    return placement_reference_plane(ref,geometry,at);
}
