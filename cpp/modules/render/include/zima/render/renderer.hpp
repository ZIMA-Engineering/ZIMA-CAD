#pragma once
#include <zima/document/render_definition.hpp>
#include <zima/kernel/appearance.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <vector>

namespace zima::render {
struct Ray { kernel::Vec3 origin,direction; };
struct Image {
    unsigned width{},height{},completed_samples{};
    std::vector<std::uint8_t> rgba;
    bool cancelled{};
    std::uint64_t rays{};
};
struct Source { std::string body_id;kernel::BodySnapshot body; };
struct SceneInput {
    std::vector<Source> sources;
    kernel::Appearance appearance;
    document::RenderDefinition definition;
};
// Immutable BVH and shading data. Construction consumes calculated mesh packets
// only; it neither evaluates CAD nor changes their geometry/reference identity.
class Scene {
public:
    explicit Scene(SceneInput,std::stop_token={});
    [[nodiscard]] std::size_t triangle_count() const;
    [[nodiscard]] kernel::Vec3 bounds_min() const;
    [[nodiscard]] kernel::Vec3 bounds_max() const;
    [[nodiscard]] Image render(std::stop_token={},
        const std::function<void(const Image&)>& progress={},unsigned workers=0) const;
    // Pixel coordinates include the subpixel sample and are measured from the
    // image's top-left corner. Local +X looks forward; local +Z points up.
    [[nodiscard]] static Ray camera_ray(const document::RenderObject&,
        unsigned width,unsigned height,double pixel_x,double pixel_y);
private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
} // namespace zima::render
