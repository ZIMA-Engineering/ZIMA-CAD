#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <unordered_map>

namespace zima::kernel {

// Surface generators retain their original parameterization. Converting a
// circle to rational spline control points changes its parameter; doing that
// alone would invalidate the face's UV trimming curves.
struct ExactSurfaceGenerator {
    enum class Kind { Line, Circle, Ellipse, Parabola, Hyperbola, Spline, Offset };
    Kind kind{Kind::Spline};
    Vec3 origin, x_axis{1,0,0}, y_axis{0,1,0}, z_axis{0,0,1};
    double radius{}, minor_radius{}, offset{};
    BSplineGeometry spline;
    std::shared_ptr<const ExactSurfaceGenerator> basis;
    void validate() const;
};

// Immutable source mathematics, captured during explicit body calculation.
// Parameters retain the native surface parameterization, including that of
// trimming curves. Neither display vertices nor triangle indices occur here.
struct ExactSurface {
    enum class Kind { Plane, Cylinder, Cone, Sphere, Torus, Spline, Extrusion, Revolution, Offset };
    Kind kind{Kind::Plane};
    Vec3 origin, x_axis{1,0,0}, y_axis{0,1,0}, z_axis{0,0,1};
    double radius{}, minor_radius{}, semi_angle{}, offset{};
    unsigned u_degree{1}, v_degree{1}, u_count{}, v_count{};
    std::vector<double> u_knots, v_knots, weights;
    std::vector<Vec3> poles; // u-major: poles[u*v_count+v]
    std::shared_ptr<const ExactSurfaceGenerator> generator;
    std::shared_ptr<const ExactSurface> basis;
    void validate() const;
};

struct ExactSurfaceGeometry {
    std::shared_ptr<const ExactSurface> support;
    // Affine occurrence frame. Keep reflections and scale explicitly instead
    // of rebuilding the third axis with a cross product.
    Vec3 origin, x_axis{1,0,0}, y_axis{0,1,0}, z_axis{0,0,1};
    double u_min{}, u_max{1}, v_min{}, v_max{1};
    bool reversed{};
    // Oriented closed loops in the original UV parameterization, z=0.
    std::vector<std::vector<BSplineGeometry>> trim_loops;
    void validate() const;
};

struct SurfaceEvaluation {
    // normal retains the oriented source-surface side even if an offset passes
    // a focal radius. Derivatives describe the actual offset surface.
    Vec3 point, du, dv, duu, duv, dvv, normal;
    bool regular{};
};

// Evaluation and derivatives are native C++, independent of OCCT. Offsets
// differentiate the oriented unit normal; they are not translated triangles.
[[nodiscard]] SurfaceEvaluation evaluate_exact_surface(
    const ExactSurfaceGeometry&, double u, double v, double offset=0.);
[[nodiscard]] std::optional<Vec3> exact_surface_point(
    const ExactSurfaceGeometry&, double u, double v, double offset=0.);

struct SurfaceContact {
    double u{}, v{};
    Vec3 point, normal;
};
struct SurfaceContactSolutions {
    std::vector<SurfaceContact> contacts;
    bool discrete{};
};
// Plane equations are n.point=rhs in the occurrence frame. The selected
// contact is the valid solution nearest the last resolved position.
[[nodiscard]] SurfaceContactSolutions solve_exact_surface_contact(
    const ExactSurfaceGeometry&, Vec3 seed, double offset,
    const std::vector<std::pair<Vec3,double>>& planes={}, bool alternatives=false);
[[nodiscard]] bool exact_surface_domain_contains(const ExactSurfaceGeometry&, double u, double v);
[[nodiscard]] bool same_exact_surface(const std::shared_ptr<const ExactSurfaceGeometry>&,
    const std::shared_ptr<const ExactSurfaceGeometry>&);

template<class Point, class Vector>
[[nodiscard]] ExactSurfaceGeometry transform_exact_surface(
        const ExactSurfaceGeometry& source, Point&& point, Vector&& vector) {
    auto result=source;
    result.origin=point(source.origin);
    result.x_axis=vector(source.x_axis);
    result.y_axis=vector(source.y_axis);
    result.z_axis=vector(source.z_axis);
    return result;
}

// One cache per actual transform. Retain source ownership while using its
// address as the key, so repeated triangle tags cannot cause address reuse.
class ExactSurfaceTransformCache {
    using Ptr=std::shared_ptr<const ExactSurfaceGeometry>;
    std::unordered_map<const ExactSurfaceGeometry*,std::pair<Ptr,Ptr>> entries_;
public:
    template<class Point,class Vector>
    void apply(Ptr& target,Point&& point,Vector&& vector) {
        if(!target)return;
        const auto source=target;
        auto [entry,inserted]=entries_.try_emplace(source.get(),source,nullptr);
        if(inserted)entry->second.second=std::make_shared<const ExactSurfaceGeometry>(
            transform_exact_surface(*source,std::forward<Point>(point),std::forward<Vector>(vector)));
        target=entry->second.second;
    }
};
}
