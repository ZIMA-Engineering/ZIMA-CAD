#include <zima/workspace/drawing_projection.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <algorithm>
#include <cctype>
#include <zima/kernel/surface_results.hpp>
#include <zima/document/native_read_capture.hpp>
namespace zima::workspace {
namespace {
bool same_number(double a,double b) {return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool same_vector(kernel::Vec3 a,kernel::Vec3 b) {return same_number(a.x,b.x)&&same_number(a.y,b.y)&&same_number(a.z,b.z);}
bool same_surface(const kernel::SurfaceGeometry& a,const kernel::SurfaceGeometry& b) {
    return a.kind==b.kind&&a.reversed==b.reversed&&same_vector(a.origin,b.origin)&&same_vector(a.axis,b.axis)&&same_vector(a.radial,b.radial)&&
        same_number(a.radius,b.radius)&&same_number(a.semi_angle,b.semi_angle)&&same_number(a.axial_min,b.axial_min)&&same_number(a.axial_max,b.axial_max);
}
bool same_spline(const kernel::BSplineGeometry& a,const kernel::BSplineGeometry& b) {
    return a.degree==b.degree&&std::ranges::equal(a.poles,b.poles,same_vector)&&
        std::ranges::equal(a.knots,b.knots,same_number)&&std::ranges::equal(a.weights,b.weights,same_number);
}
bool same_face(const kernel::FaceReference& a,const kernel::FaceReference& b) {
    return a==b&&a.display_owner_id==b.display_owner_id&&a.measured_area==b.measured_area&&
        a.surface_result==b.surface_result&&a.sheet_role==b.sheet_role&&a.sheet_thickness==b.sheet_thickness&&a.sheet_owner==b.sheet_owner&&
        ((!a.surface&&!b.surface)||(a.surface&&b.surface&&same_surface(*a.surface,*b.surface)));
}
bool same_edge(const kernel::ViewerEdge& a,const kernel::ViewerEdge& b) {
    return std::ranges::equal(a.points,b.points,same_vector)&&a.reference==b.reference&&a.reference.display_owner_id==b.reference.display_owner_id&&
        a.construction==b.construction&&a.overlay==b.overlay&&a.infinite==b.infinite&&a.dash_dot==b.dash_dot&&
        a.parameter_seam==b.parameter_seam&&a.surface_result==b.surface_result&&a.display_owner_id==b.display_owner_id&&
        a.edge_treatment_owner_ids==b.edge_treatment_owner_ids&&std::ranges::equal(a.edge_treatment_side_directions,b.edge_treatment_side_directions,
            [](const auto& x,const auto& y){return std::ranges::equal(x,y,same_vector);})&&
        std::ranges::equal(a.edge_treatment_side_references,b.edge_treatment_side_references,same_face)&&
        a.edge_treatment_endpoint_references==b.edge_treatment_endpoint_references&&a.color==b.color&&a.filled_text==b.filled_text&&
        a.measured_length==b.measured_length&&
        ((!a.exact_spline&&!b.exact_spline)||(a.exact_spline&&b.exact_spline&&same_spline(*a.exact_spline,*b.exact_spline)));
}
// Exact comparison of every input consumed by project_edges/project_triangles.
// Measurements and annotations are always recaptured from the fresh source.
bool same_projection_input(const kernel::ViewerMesh& a,const kernel::ViewerMesh& b) {
    return kernel::has_surface_results(a)==kernel::has_surface_results(b)&&
        std::ranges::equal(a.vertices,b.vertices,same_vector)&&a.triangles==b.triangles&&
        std::ranges::equal(a.original_references.vertices,b.original_references.vertices,same_vector)&&a.original_references.triangles==b.original_references.triangles&&
        std::ranges::equal(a.triangle_references,b.triangle_references,same_face)&&
        std::ranges::equal(a.edges,b.edges,same_edge)&&
        std::ranges::equal(a.original_references.edges,b.original_references.edges,same_edge)&&
        std::ranges::equal(a.original_references.triangle_references,b.original_references.triangle_references,same_face);
}
}
struct DrawingProjection::Impl {
    struct Entry {
        Source source;
        bool metadata_ready{};
        std::shared_ptr<const kernel::ViewerMesh> display_source;
        std::map<std::array<double,9>,std::pair<std::vector<drawing::ProjectedEdge>,std::vector<drawing::ProjectedTriangle>>> cameras;
        std::map<std::array<double,9>,drawing::DrawingView> interactive_cameras;
    };
    const Workspace* workspace;
    const Impl* previous{};
    std::size_t calculated_cameras{};
    std::size_t source_loads{},calculated_interactive_cameras{};
    std::filesystem::path drawing_path;
    std::map<std::pair<std::string,std::filesystem::path>,std::shared_ptr<Entry>> sources;
    std::optional<Workspace> loaded;
    document::NativeReadCapture native_reads;
    using LiveStamp=std::tuple<std::string,std::filesystem::path,std::shared_ptr<const int>,std::uint64_t>;
    std::vector<LiveStamp> live_stamps;
    bool reuse_checked{},reuse_valid{};
    static std::vector<LiveStamp> stamps(const Workspace* live) {
        std::vector<LiveStamp> result;
        if(live)for(const auto& state:live->documents())std::visit([&](const auto& item) {
            if constexpr(requires{item.session;})result.emplace_back(item.session.document().document_id,
                item.path,item.runtime_identity,item.session.data_generation());
        },state);
        return result;
    }

    Entry& get(const drawing::DrawingView& view,bool metadata=true) {
        auto path=view.source_path;
        if(!path.empty()&&path.is_relative()&&!drawing_path.empty())path=drawing_path.parent_path()/path;
        path=path.lexically_normal();
        const auto key=std::make_pair(view.source_document_id,path);
        if(previous&&!reuse_checked) {
            reuse_checked=true;
            reuse_valid=live_stamps==previous->live_stamps&&previous->native_reads.unchanged();
            if(reuse_valid)native_reads=previous->native_reads;
        }
        if(reuse_valid&&!sources.contains(key))if(const auto found=previous->sources.find(key);
                found!=previous->sources.end()&&found->second->metadata_ready)
            sources.emplace(key,found->second);
        document::NativeReadCapture::Scope capture(native_reads);
        const auto finish=[&](Entry& entry)->Entry& {
            if(metadata&&!entry.metadata_ready) {
                auto& source=entry.source;
                source.bom=build_bom_rows_for_source(source.document_id,source.path,&*loaded);
                source.annotations=drawing_annotation_sources(&*loaded,source.document_id,source.path);
                source.sections=source_sections(&*loaded,source.document_id,source.path);
                entry.metadata_ready=true;
            }
            return entry;
        };
        if(const auto found=sources.find(key);found!=sources.end())return finish(*found->second);
        if(!loaded){
            loaded.emplace();
            // Projection reads model sources, never Drawing history. Keep live
            // Part/Assembly states without copying every drawing Undo record.
            if(workspace)for(const auto& state:workspace->documents())
                if(!std::holds_alternative<DrawingState>(state))loaded->documents().push_back(state);
        }
        // Read the root once per edit session. The private workspace preserves
        // live unsaved sources and never opens tabs or publishes changes.
        if(!loaded->find(view.source_document_id)) {
            auto extension=path.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(extension==".asmz") {
                auto assembly=read_family_assembly(&*loaded,path,view.source_document_id);
                if(!loaded->find(assembly.document_id))loaded->add_assembly(std::move(assembly),path);
            }
            else if(extension==".prtz") {
                std::vector<kernel::BodyResult> boundaries;
                auto part=read_family_part(&*loaded,path,view.source_document_id,boundaries);
                if(boundaries.empty()&&!part.kernel_operations().empty())throw DrawingOperationError("uncalculated_source","The Part has no saved calculated model. Regenerate and save it first.");
                if(!loaded->find(part.document_id))loaded->add_part(std::move(part),std::move(boundaries),path);
            }
        }
        auto [id,mesh]=read_drawing_source(&*loaded,path,view.source_document_id);
        ++source_loads;
        auto& entry=*sources.emplace(key,std::make_shared<Entry>(Entry{Source{std::move(id),std::move(path),std::move(mesh)}})).first->second;
        if(previous)if(const auto found=previous->sources.find(key);found!=previous->sources.end()&&!found->second->cameras.empty()) {
            // Compare full persisted viewer packets, including spline geometry,
            // analytic face metadata and exact occurrence identities. Topology
            // identity equality alone does not mean equal geometry.
            if(same_projection_input(found->second->source.mesh,entry.source.mesh))
                entry.cameras=found->second->cameras;
        }
        return finish(entry);
    }
};
DrawingProjection::DrawingProjection(const Workspace* workspace,std::filesystem::path path,const DrawingProjection* previous):impl_(std::make_unique<Impl>()) {
    impl_->workspace=workspace;
    impl_->drawing_path=std::move(path);
    impl_->previous=previous?previous->impl_.get():nullptr;
    impl_->live_stamps=Impl::stamps(workspace);
}
DrawingProjection::~DrawingProjection()=default;
std::size_t DrawingProjection::calculated_camera_count() const {return impl_->calculated_cameras;}
std::size_t DrawingProjection::source_load_count() const {return impl_->source_loads;}
std::size_t DrawingProjection::calculated_interactive_camera_count() const {return impl_->calculated_interactive_cameras;}
const DrawingProjection::Source& DrawingProjection::source(const drawing::DrawingView& view) {
    return impl_->get(view).source;
}
const kernel::ViewerMesh& DrawingProjection::placement_source(const drawing::DrawingView& view) {
    return impl_->get(view,false).source.mesh;
}
void DrawingProjection::project(drawing::DrawingView& view,Options options) {
    auto& cached=impl_->get(view);
    const auto& source=cached.source;
    if(options.require_geometry&&source.mesh.edges.empty()&&source.mesh.triangles.empty())
        throw DrawingOperationError("uncalculated_source","The source has no calculated geometry. Regenerate it first.");
    view.source_document_id=source.document_id;
    // Preserve the document's stored relative path; cache keys resolve it once.
    if(!view.section_id.empty()) {
        const auto section=std::ranges::find(source.sections,view.section_id,&document::SectionDefinition::id);
        if(section==source.sections.end())
            throw DrawingOperationError("section_not_found","The source Section no longer exists. Edit the drawing view first.");
        const auto pending=view.section_snapshot;
        view.section_snapshot=*section;
        if(options.pending_hatch&&pending&&pending->id==section->id)view.section_snapshot->components=pending->components;
        drawing::refresh_view_geometry(view,source.mesh);
    }else if(options.interactive) {
        if(!cached.display_source)cached.display_source=std::make_shared<const kernel::ViewerMesh>(source.mesh);
        const auto& c=view.camera;
        const std::array key{c.horizontal.x,c.horizontal.y,c.horizontal.z,c.vertical.x,c.vertical.y,c.vertical.z,c.depth.x,c.depth.y,c.depth.z};
        const auto found=cached.interactive_cameras.find(key);
        if(found==cached.interactive_cameras.end()) {
            drawing::prepare_interactive_view(view,cached.display_source);
            ++impl_->calculated_interactive_cameras;
            cached.interactive_cameras.emplace(key,view);
        } else {
            view.output_source=found->second.output_source;
            view.projected_edges=found->second.projected_edges;view.projected_triangles=found->second.projected_triangles;
            view.measurement_geometry=found->second.measurement_geometry;
        }
    }else{
        view.output_source.reset();
        const auto& c=view.camera;
        const std::array key{c.horizontal.x,c.horizontal.y,c.horizontal.z,c.vertical.x,c.vertical.y,c.vertical.z,c.depth.x,c.depth.y,c.depth.z};
        auto found=cached.cameras.find(key);
        if(found==cached.cameras.end()) {
            found=cached.cameras.emplace(key,std::make_pair(drawing::project_edges(source.mesh,c),drawing::project_triangles(source.mesh,c))).first;
            ++impl_->calculated_cameras;
        }
        drawing::capture_measurement_geometry(view,source.mesh);
        view.projected_edges=found->second.first;
        view.projected_triangles=found->second.second;
    }
    if(options.refresh_markers)for(auto& marker:view.section_markers) {
        const auto current=std::ranges::find(source.sections,marker.id,&document::SectionDefinition::id);
        if(current==source.sections.end())
            throw DrawingOperationError("section_not_found","A source section trace no longer exists. Edit the drawing view first.");
        marker=*current;
    }
    drawing::refresh_model_annotations(view,source.annotations);
    auto measuring=*view.measurement_geometry;
    for(auto& curve:measuring.curves)if(curve.source.semantic_key.starts_with("thread:boundary:"))
        for(const auto& packet:source.annotations)if(packet.instance_path==curve.source.instance_path)
            if(const auto thread=packet.threads.find(curve.source.owner_id);thread!=packet.threads.end())curve.thread=thread->second;
    view.measurement_geometry=drawing::share_measurement_geometry(std::move(measuring));
}
}
