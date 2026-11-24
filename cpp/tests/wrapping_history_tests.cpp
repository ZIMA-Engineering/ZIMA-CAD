#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/surface_wrap.hpp>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <STEPControl_Writer.hxx>
#include <filesystem>
#include <iostream>
#include <set>

namespace k=zima::kernel;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void valid(const k::BodyResult& result) {
    for(const auto& [owner,message]:result.calculation_errors)throw std::runtime_error(owner+": "+message);
    require(BRepCheck_Analyzer(k::read_calculated_body_shape(result)).IsValid(),"Wrap produced invalid BRep");
}
int main(){try {
    const auto directory=std::filesystem::temp_directory_path()/"zima-wrapping-native-tests";
    std::filesystem::create_directories(directory);
    for(auto kind:{k::SurfaceGeometry::Kind::Cylinder,k::SurfaceGeometry::Kind::Cone,k::SurfaceGeometry::Kind::Sphere}) {
        const TopoDS_Shape shape=kind==k::SurfaceGeometry::Kind::Cylinder?BRepPrimAPI_MakeCylinder(30,40).Shape():
            kind==k::SurfaceGeometry::Kind::Cone?BRepPrimAPI_MakeCone(30,20,40).Shape():BRepPrimAPI_MakeSphere(30).Shape();
        const auto path=directory/(std::to_string(static_cast<int>(kind))+".step");
        STEPControl_Writer writer;require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone,"Fixture STEP transfer failed");
        require(writer.Write(path.string().c_str())==IFSelect_RetDone,"Fixture STEP write failed");
        k::OcctKernel kernel;k::StepRequest step;step.source_path=path.string();
        const k::HistoryOperation base{"base",step};
        const auto initial=kernel.evaluate_history({base}).back();valid(initial);
        k::FaceReference target;
        for(const auto& face:initial.mesh.triangle_references) {
            const auto surface=face.surface?face.surface:face.contour_surface;
            if(surface&&surface->kind==kind)target=face;
        }
        require(target.valid(),"Missing analytic wrap target metadata");
        for(bool polar:{false,true}) {
        if(polar&&kind!=k::SurfaceGeometry::Kind::Sphere)continue;
        for(auto output:{k::WrappingResult::Curves,k::WrappingResult::Surface,k::WrappingResult::Relief})
        for(bool inward:{false,true}) {
            k::WrappingRequest request;request.sketch_id="wrap-sketch";request.target=target;request.output=output;
            request.origin=kind==k::SurfaceGeometry::Kind::Sphere?k::Vec3{30,0,0}:k::Vec3{kind==k::SurfaceGeometry::Kind::Cone?25.:30.,0,20};
            request.x_axis={0,1,0};request.y_axis={0,0,1};request.normal={1,0,0};
            if(polar){request.origin={0,0,30};request.x_axis={1,0,0};request.y_axis={0,1,0};request.normal={0,0,1};}
            request.height=1;request.tolerance=1e-6;request.inward=inward;request.reverse_u=inward;
            auto& profile=request.profiles.emplace_back();
            const auto p=[&](double u,double v){return k::wrap_detail::add(request.origin,k::wrap_detail::add(k::wrap_detail::scale(request.x_axis,u),k::wrap_detail::scale(request.y_axis,v)));};
            profile.outer_profile=k::ExtrusionRequest::PolygonProfile{{p(-4,-3),p(4,-3),p(4,3),p(-4,3)}};
            profile.outer_edge_source_ids={"bottom","right","top","left"};profile.outer_vertex_source_ids={"a","b","c","d"};
            profile.direction=request.normal;profile.profile_region_id="rectangle";profile.outer_boundary_id="outer";
            k::ViewerImage image;image.reference={"wrap-sketch","artwork",{}};image.data_base64="embedded-native-bytes";
            image.corners={p(-4,3),p(4,3),p(4,-3),p(-4,-3)};request.images.push_back(image);
            const k::HistoryOperation wrapping{"wrap",request,inward&&output==k::WrappingResult::Relief?k::BooleanOperation::Subtract:k::BooleanOperation::Add};
            std::cout<<"kind="<<static_cast<int>(kind)<<" output="<<static_cast<int>(output)<<" inward="<<inward<<" polar="<<polar<<std::endl;
            const std::vector<k::HistoryOperation> history{base,wrapping};
            const auto result=kernel.evaluate_history(history).back();valid(result);
            require(result.mesh.images.size()==1&&!result.mesh.images.front().surface_vertices.empty(),"Wrapped image was not published");
            require(result.mesh.images.front().surface_vertices.size()==result.mesh.images.front().surface_uv.size(),"Wrapped image UV mismatch");
            for(const auto& uv:result.mesh.images.front().surface_uv)require(uv[0]>=-1e-10&&uv[0]<=1+1e-10&&uv[1]>=-1e-10&&uv[1]<=1+1e-10,"Image clipping failed");
            if(output==k::WrappingResult::Relief)require(inward?result.volume<initial.volume:result.volume>initial.volume,"Relief changed the wrong material side");
            else require(std::abs(result.volume-initial.volume)<1e-5,"Nonmaterial Wrap changed volume");
            std::set<std::string> edges;
            for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="wrap")edges.insert(edge.reference.semantic_key);
            require(edges.size()>=4,"Wrapped curves lost authored ancestry");
            const auto repeated=kernel.evaluate_history(history).back();valid(repeated);
            require(repeated.mesh.images.size()==1&&repeated.mesh.images.front().surface_vertices==result.mesh.images.front().surface_vertices,"Cached wrap lost its decal");
            require(result.source_fingerprint==repeated.source_fingerprint&&std::abs(result.volume-repeated.volume)<1e-8,"Unchanged Wrap did not reuse equivalent geometry");
            if(output==k::WrappingResult::Curves&&!polar) {
                k::ExtrusionRequest cut;cut.outer_profile=k::ExtrusionRequest::PolygonProfile{{{0,-1,-50},{0,1,-50},{0,1,60},{0,-1,60}}};
                cut.profile_region_id="slot-region";cut.outer_boundary_id="slot-boundary";
                cut.outer_edge_source_ids={"slot-a","slot-b","slot-c","slot-d"};
                cut.outer_vertex_source_ids={"slot-p","slot-q","slot-r","slot-s"};
                cut.direction={40,0,0};
                auto cut_history=history;cut_history.push_back({"cut",cut,k::BooleanOperation::Subtract});
                const auto trimmed=kernel.evaluate_history(cut_history).back();valid(trimmed);
                require(trimmed.mesh.images.size()==1,"A later cut removed the whole decal");
                for(const auto& point:trimmed.mesh.images.front().surface_vertices)
                    require(std::abs(point.y)>=1-1e-6,"Decal bridges a later material cut");
                const auto cached_cut=kernel.evaluate_history(cut_history).back();valid(cached_cut);
                require(cached_cut.mesh.images.front().surface_vertices==trimmed.mesh.images.front().surface_vertices,"Cached cut changed decal clipping");
                auto artwork_only=request;artwork_only.profiles.clear();
                const auto artwork_result=kernel.evaluate_history({base,{"artwork",artwork_only}}).back();valid(artwork_result);
                require(artwork_result.mesh.images.size()==1&&std::abs(artwork_result.volume-initial.volume)<1e-5,"Image-only Wrap changed material or lost its image");
                auto open=request;open.images.clear();auto& edge=open.profiles.front();
                edge.outer_profile=k::ExtrusionRequest::CurvedProfile{{k::ExtrusionRequest::LineCurve{p(-4,-3),p(4,3)}}};
                edge.outer_edge_source_ids={"diagonal"};edge.outer_vertex_source_ids={"start"};edge.open_profile_end_id="end";
                const auto open_result=kernel.evaluate_history({base,{"open",open}}).back();valid(open_result);
                require(std::abs(open_result.volume-initial.volume)<1e-5,"Open Wrap curve changed material");
                auto changed=request;changed.reverse_v=!changed.reverse_v;
                const auto changed_result=kernel.evaluate_history({base,{"wrap",changed}}).back();valid(changed_result);
                require(changed_result.source_fingerprint!=result.source_fingerprint,"A changed direction reused stale Wrap output");
            } else if(output==k::WrappingResult::Surface&&!inward) {
                std::cout<<"checking inner circle"<<std::endl;
                auto holed=request;holed.images.clear();auto& region=holed.profiles.front();
                region.inner_profiles.push_back(k::ExtrusionRequest::CircleProfile{p(0,0),1});
                region.inner_boundary_ids={"hole"};region.inner_edge_source_ids={{"hole-curve"}};
                region.inner_vertex_source_ids={{}};
                const auto with_hole=kernel.evaluate_history({base,{"hole",holed}}).back();valid(with_hole);
                require(std::abs(with_hole.volume-initial.volume)<1e-5,"Surface with a hole changed material");
            }
        }
        }
    }
    std::cout<<"Wrap native geometry matrix passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
