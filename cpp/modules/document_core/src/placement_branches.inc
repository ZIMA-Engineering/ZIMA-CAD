// Included beside the shared placement solver. All inputs are persisted ZIMA
// reference geometry; no kernel calculation or viewer tessellation is used.
struct PlacementBranchSet {
    const ConstructionReference* source{};
    std::vector<std::array<std::string,3>> sources;
    std::vector<std::pair<int,kernel::Vec3>> points;
};

std::array<std::string,3> placement_branch_key(const ConstructionReference& ref) {
    return {ref.instance_path,ref.owner_id,ref.semantic_key};
}

struct PlacementConic { kernel::Vec3 center, a, b; double scale; };
std::optional<PlacementConic> placement_conic(const kernel::BSplineGeometry& spline) {
    const auto* ellipse=&spline;
    if(ellipse->degree!=2 || ellipse->poles.size()!=9 || ellipse->weights.size()!=9 || ellipse->knots.size()!=12)return {};
    if(ellipse->weights[0]<=0)return {};
    const double start=ellipse->knots.front(),span=ellipse->knots.back()-start;
    if(span<=0)return {};
    const std::array<double,12> knots{0,0,0,.25,.25,.5,.5,.75,.75,1,1,1};
    for(std::size_t i=0;i<knots.size();++i)
        if(std::abs((ellipse->knots[i]-start)/span-knots[i])>1e-12)return {};
    const auto center=placement_vec_scale(placement_vec_add(ellipse->poles[0],ellipse->poles[4]),.5);
    const auto a=placement_vec_sub(ellipse->poles[0],center),b=placement_vec_sub(ellipse->poles[2],center);
    const std::array<std::pair<double,double>,9> coefficients{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1},{1,0}}};
    const double scale=std::max({1.,std::hypot(a.x,a.y,a.z),std::hypot(b.x,b.y,b.z)});
    for(std::size_t i=0;i<9;++i) {
        const auto expected=placement_vec_add(center,placement_vec_add(placement_vec_scale(a,coefficients[i].first),placement_vec_scale(b,coefficients[i].second)));
        const auto delta=placement_vec_sub(expected,ellipse->poles[i]);
        if(std::hypot(delta.x,delta.y,delta.z)>1e-10*scale ||
           std::abs(ellipse->weights[i]/ellipse->weights[0]-(i%2?std::sqrt(.5):1.))>1e-12)return {};
    }
    const auto area=placement_vec_cross(a,b);
    if(std::hypot(area.x,area.y,area.z)<1e-12)return {};
    return PlacementConic{center,a,b,scale};
}

std::optional<PlacementConic> placement_circle(const kernel::BSplineGeometry& s) {
    if(const auto conic=placement_conic(s)) {
        const auto& [center,a,b,scale]=*conic;
        const double ra=std::hypot(a.x,a.y,a.z),rb=std::hypot(b.x,b.y,b.z);
        if(std::abs(ra-rb)<=1e-10*scale && std::abs(placement_vec_dot(a,b))<=1e-10*ra*rb)return conic;
        return {};
    }
    // Persisted body circles use three exact rational 120-degree arcs.
    // Validate the full representation; do not fit arbitrary curved edges.
    if(s.degree!=2 || s.poles.size()!=7 || s.weights.size()!=7 || s.knots.size()!=10 || s.weights[0]<=0)return {};
    const double span=s.knots.back()-s.knots.front();if(span<=0)return {};
    const std::array<double,10> knots{0,0,0,1./3,1./3,2./3,2./3,1,1,1};
    for(std::size_t i=0;i<knots.size();++i)
        if(std::abs((s.knots[i]-s.knots.front())/span-knots[i])>1e-12)return {};
    const auto center=placement_vec_scale(placement_vec_add(placement_vec_add(s.poles[0],s.poles[2]),s.poles[4]),1./3);
    const auto a=placement_vec_sub(s.poles[0],center),v=placement_vec_sub(s.poles[2],center);
    const double radius=std::hypot(a.x,a.y,a.z),scale=std::max(1.,radius);if(radius<1e-12)return {};
    const auto normal=placement_vec_normalized(placement_vec_cross(a,v));
    const auto b=placement_vec_cross(normal,a);
    for(std::size_t i=0;i<7;++i) {
        const double angle=static_cast<double>(i)*std::numbers::pi/3;
        const auto expected=placement_vec_add(center,placement_vec_scale(placement_vec_add(
            placement_vec_scale(a,std::cos(angle)),placement_vec_scale(b,std::sin(angle))),i%2?2.:1.));
        const auto delta=placement_vec_sub(expected,s.poles[i]);
        if(std::hypot(delta.x,delta.y,delta.z)>1e-10*scale || std::abs(s.weights[i]/s.weights[0]-(i%2?.5:1.))>1e-12)return {};
    }
    return PlacementConic{center,a,b,scale};
}

std::optional<PlacementBranchSet> placement_branches(
    const std::vector<std::reference_wrapper<const ConstructionReference>>& references,
    const kernel::ViewerReferenceGeometry& geometry, kernel::Vec3 seed) {
    using kernel::Vec3;
    const ConstructionReference* curved{};
    const kernel::SurfaceGeometry* cylinder{};
    const kernel::BSplineGeometry* ellipse{};
    struct Plane { const ConstructionReference* ref; Vec3 normal; double rhs; };
    std::vector<Plane> planes;
    for(const auto& wrapped:references) {
        const auto& ref=wrapped.get();
        if(ref.orientation_only)continue;
        const auto* surface=placement_surface(ref,geometry);
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
            return e.exact_spline && placement_reference_matches(e.reference,ref);
        });
        if(!ref.use_axis && surface && surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
            if(curved)return {};curved=&ref;cylinder=surface;
        } else if(edge!=geometry.edges.end() && !ref.use_axis) {
            if(curved)return {};curved=&ref;ellipse=&*edge->exact_spline;
        } else if(construction_reference_is_planar_face(ref,geometry)) {
            const auto p=placement_reference_plane(ref,geometry);
            if(!p)return {};
            planes.push_back({&ref,p->normal,placement_vec_dot(p->normal,p->point)+ref.offset});
        } else return {};
    }
    if(!curved || planes.empty())return {};
    if(curved->solution_branch) {
        std::ranges::sort(planes,{},[&](const Plane& p) {
            return std::ranges::find(curved->branch_sources,placement_branch_key(*p.ref))-curved->branch_sources.begin();
        });
    } else std::ranges::sort(planes,{},[](const Plane& p){return placement_branch_key(*p.ref);});
    PlacementBranchSet result;result.source=curved;
    for(const auto& p:planes)result.sources.push_back(placement_branch_key(*p.ref));
    const auto add=[&](int branch,Vec3 point) {
        if(std::ranges::all_of(planes,[&](const Plane& p) {
            return std::abs(placement_vec_dot(p.normal,point)-p.rhs)<=1e-7;
        }))result.points.push_back({branch,point});
    };
    if(ellipse) {
        if(curved->offset!=0)return {};
        // Exact four rational quadratic quarters, as used by native circles
        // and ellipses. Verify the representation before using conic algebra;
        // arbitrary splines keep their existing solver.
        const auto conic=placement_conic(*ellipse);
        if(!conic)return {};
        const auto [center,a,b,scale]=*conic;
        for(const auto& p:planes) {
            const double x=placement_vec_dot(p.normal,a),y=placement_vec_dot(p.normal,b);
            const double radius=std::hypot(x,y),rhs=p.rhs-placement_vec_dot(p.normal,center);
            if(radius<1e-12*scale) {if(std::abs(rhs)>1e-7)return result;continue;}
            if(std::abs(rhs)>radius+1e-10*scale)return result;
            const double phi=std::atan2(y,x),delta=std::acos(std::clamp(rhs/radius,-1.,1.));
            for(int branch:{-1,1}) {
                const double angle=phi+branch*delta;
                add(branch,placement_vec_add(center,placement_vec_add(placement_vec_scale(a,std::cos(angle)),placement_vec_scale(b,std::sin(angle)))));
            }
            return result;
        }
        return {}; // All planes contain the complete curve: still free.
    }
    // The planes define a line. A single plane parallel to the cylinder axis
    // defines two generators; retain the free axial station in that case.
    std::vector<std::pair<Vec3,double>> basis;
    for(const auto& p:planes) {
        auto normal=p.normal;double rhs=p.rhs;
        for(const auto& [n,d]:basis) {
            const double q=placement_vec_dot(normal,n);normal=placement_vec_sub(normal,placement_vec_scale(n,q));rhs-=q*d;
        }
        const double m=std::hypot(normal.x,normal.y,normal.z);
        if(m<1e-10) {if(std::abs(rhs)>1e-7)return result;continue;}
        basis.push_back({placement_vec_scale(normal,1/m),rhs/m});
    }
    if(basis.size()==1) {
        if(std::abs(placement_vec_dot(basis[0].first,cylinder->axis))>1e-10)return {};
        basis.push_back({cylinder->axis,placement_vec_dot(cylinder->axis,seed)});
    }
    if(basis.size()!=2)return {};
    const auto line=placement_vec_normalized(placement_vec_cross(basis[0].first,basis[1].first));
    auto base=placement_vec_add(placement_vec_scale(basis[0].first,basis[0].second),placement_vec_scale(basis[1].first,basis[1].second));
    const auto radial=[&](Vec3 v){return placement_vec_sub(v,placement_vec_scale(cylinder->axis,placement_vec_dot(v,cylinder->axis)));};
    const auto d=radial(line),q=radial(placement_vec_sub(base,cylinder->origin));
    const double a=placement_vec_dot(d,d);
    if(a<1e-20)return {}; // Parallel generator: no two isolated branches.
    const double radius=cylinder->radius+(cylinder->reversed?-curved->offset:curved->offset);
    if(radius<=0)return result;
    const double middle=-placement_vec_dot(q,d)/a;
    const auto closest=placement_vec_add(q,placement_vec_scale(d,middle));
    double square=(radius*radius-placement_vec_dot(closest,closest))/a;
    if(square < -1e-10*std::max(1.,radius*radius)/a)return result;
    square=std::max(0.,square);
    for(int branch:{-1,1})add(branch,placement_vec_add(base,placement_vec_scale(line,middle+branch*std::sqrt(square))));
    return result;
}

std::optional<kernel::Vec3> selected_placement_branch(const PlacementBranchSet& set,kernel::Vec3 seed) {
    const auto& ref=*set.source;
    if(ref.solution_branch && ref.branch_sources!=set.sources)return {};
    std::optional<kernel::Vec3> best;double distance=std::numeric_limits<double>::infinity();
    for(const auto& [branch,point]:set.points) {
        if(ref.solution_branch && ref.solution_branch!=branch)continue;
        const auto d=placement_vec_sub(point,seed);const double squared=placement_vec_dot(d,d);
        if(squared<distance){best=point;distance=squared;}
    }
    return best;
}
