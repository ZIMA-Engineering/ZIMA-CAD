#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/surface_thicken.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void check_offsets(const TopoDS_Shape& skin,const TopoDS_Shape& solid,zima::kernel::SurfaceThicknessSide side) {
    const double first=side==zima::kernel::SurfaceThicknessSide::Symmetric?-.5:0.;
    const double last=side==zima::kernel::SurfaceThicknessSide::Second?-1.:
        side==zima::kernel::SurfaceThicknessSide::Symmetric?.5:1.;
    unsigned checked=0;
    for(TopExp_Explorer source(skin,TopAbs_FACE);source.More();source.Next()) {
        const auto face=TopoDS::Face(source.Current());const auto surface=BRep_Tool::Surface(face);
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        unsigned samples=0;
        for(double u:{.23,.51,.77})for(double v:{.23,.51,.77}) {
            const gp_Pnt2d uv(u0+(u1-u0)*u,v0+(v1-v0)*v);
            BRepClass_FaceClassifier classifier(face,uv,1e-9);
            if(classifier.State()!=TopAbs_IN)continue;
            gp_Pnt point;gp_Vec du,dv;surface->D1(uv.X(),uv.Y(),point,du,dv);
            auto normal=du.Crossed(dv);if(normal.Magnitude()<1e-12)continue;normal.Normalize();
            if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            const auto matches=[&](double distance) {
                const auto expected=point.Translated(normal*distance);
                for(TopExp_Explorer result(solid,TopAbs_FACE);result.More();result.Next()) {
                    const auto cap=TopoDS::Face(result.Current());
                    const auto geometry=BRep_Tool::Surface(cap);
                    if(geometry->Value(uv.X(),uv.Y()).Distance(expected)>1e-7)continue;
                    BRepClass_FaceClassifier trim(cap,uv,1e-7);
                    if(trim.State()==TopAbs_IN||trim.State()==TopAbs_ON)return true;
                }
                return false;
            };
            check(matches(first)&&matches(last),"FORM cap does not satisfy the authored normal offset");
            ++samples;
        }
        check(samples>0,"FORM offset equation has an unverified surface patch");checked+=samples;
    }
    check(checked>0,"FORM offset equation has no verified points");
}
}
int main(){try {
    const std::filesystem::path library="config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz";
    std::vector<zima::kernel::BodyResult> cached;
    auto part=zima::document::PartDocument::load(library,&cached);
    const auto definition=zima::document::sheet_form_definition(part,cached);
    // Role names, never Body-list positions, select a library definition.
    // Exercise the actual native file loader and embedded-copy path for every
    // order, including FORM_CUT authored after the outer FORM surface.
    const auto reordered_path=std::filesystem::absolute("build/form-diagnostic")/
        ("FormRoleOrder-"+zima::kernel::make_stable_id()+".prtz");
    std::filesystem::create_directories(reordered_path.parent_path());
    std::array<unsigned,4> order{0,1,2,3};unsigned permutations=0;
    zima::kernel::OcctKernel order_kernel;
    do {
        auto reordered=part;auto graph=reordered.body_history;
        for(std::size_t index=0;index<order.size();++index)
            graph.move_body(definition.bodies[order[index]],index);
        reordered.set_body_history(std::move(graph));
        for(std::size_t index=0;index<order.size();++index)
            check(reordered.body_history.bodies()[index].scope.id==definition.bodies[order[index]],
                "FORM order fixture did not reorder actual Bodies");
        const auto reordered_calculated=order_kernel.evaluate_history_incremental(reordered.kernel_operations(),cached);
        check(!reordered_calculated.empty()&&reordered_calculated.back().calculation_errors.empty(),
            "Reordered FORM definition failed its explicit native calculation");
        reordered.save(reordered_path,reordered_calculated);
        const auto loaded=zima::document::read_sheet_form_definition(reordered_path);
        check(loaded.bodies==definition.bodies&&loaded.cut_sketch==definition.cut_sketch&&
            loaded.flat_sketch==definition.flat_sketch&&loaded.symbol_sketch==definition.symbol_sketch&&
            loaded.surface==definition.surface,"Body order changed a loaded FORM role or shell anchor");
        const auto parameters=zima::document::copy_sheet_form_definition(loaded);
        const auto embedded=zima::document::stored_sheet_form_definition(parameters);
        check(embedded.bodies==definition.bodies&&embedded.cut_sketch==definition.cut_sketch&&
            embedded.surface==definition.surface,"Body order changed an independent FORM copy");
        const auto request=zima::document::sheet_form_request(embedded,{}, {},{0,1,0},{1,0,0},1.);
        check(request.cut_body==definition.bodies[0]&&request.shape_body==definition.bodies[1]&&
            request.surface_snapshot,"Body order lost the copied FORM operands or calculated surface");
        ++permutations;
    }while(std::next_permutation(order.begin(),order.end()));
    check(permutations==24,"FORM role-order matrix is incomplete");
    std::filesystem::remove(reordered_path);
    std::cout<<"FORM native file/copy role lookup passed all 24 Body orders\n";
    const auto copied=zima::document::PartDocument::from_serialized(part.serialized({},
        {zima::kernel::make_stable_id(),library,library.parent_path()/"InsertedCopy.prtz"}));
    check(copied.document_id!=part.document_id,"Independent FORM copy reused source document identity");
    check(!definition.cut_sketch.empty()&&definition.flat_sketch.empty()&&!definition.symbol_sketch.empty(),"Ventilation Window role Sketches incorrect");
    check(definition.surface.valid()&&definition.surface.surface_result,"FORM has no persisted shell anchor");
    for(const auto& sketch:copied.sketches)for(const auto& reference:sketch.external_references)
        check(reference.source_document_id.empty()||reference.source_document_id==copied.document_id,"Library copy retained source document reference");
    for(const auto& feature:copied.history)zima::document::visit_feature_sketches(feature,[&](const auto& data,std::size_t) {
        const auto sketch=zima::sketcher::Sketch::from_serialized(data);
        for(const auto& reference:sketch.external_references)
            check(reference.source_document_id.empty()||reference.source_document_id==copied.document_id,"Owned Sketch retained source document reference");
    });
    const auto reject=[&](auto bad,const auto& calculation) {
        bool rejected=false;try{static_cast<void>(zima::document::sheet_form_definition(std::move(bad),calculation));}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid FORM definition was accepted");
    };
    auto bad=part;auto body=*bad.body_history.find(definition.bodies[0]);body.name="Missing role";bad.body_history.update_body(body);reject(bad,cached);
    bad=part;body=*bad.body_history.find(definition.bodies[2]);body.name="FORM_CUT";bad.body_history.update_body(body);reject(bad,cached);
    bad=part;bad.body_history.activate(definition.bodies[0]);body=*bad.body_history.find(definition.bodies[3]);body.suppressed=true;bad.body_history.update_body(body);reject(bad,cached);
    bad=part;body=*bad.body_history.find(definition.bodies[1]);body.cursor=0;bad.body_history.update_body(body);reject(bad,cached);
    reject(part,std::vector<zima::kernel::BodyResult>{});
    auto failed=cached;failed.back().calculation_errors["failure"]="test";reject(part,failed);
    bool drawing_rejected=false;try{static_cast<void>(zima::document::read_sheet_form_definition("Projects/FORM.drwz"));}
    catch(const std::invalid_argument&){drawing_rejected=true;}
    check(drawing_rejected,"FORM accepted a Drawing file");
    // A cold native calculation must recover the same semantic shell anchor;
    // the library does not depend on a live source document or a shape sidecar.
    zima::kernel::OcctKernel kernel;
    const auto cold=kernel.evaluate_history(part.kernel_operations());
    const auto recalculated=zima::document::sheet_form_definition(part,cold);
    check(definition.bodies==recalculated.bodies&&definition.surface==recalculated.surface,
        "Cold regeneration changed FORM role or shell ancestry");
    TopoDS_Shape skin;BRep_Builder builder;
    std::istringstream source_shape(cold.back().body_outputs.at(definition.bodies[1])->kernel_shape);
    BRepTools::Read(skin,source_shape,builder);
    const auto authored=part.serialized();
    for(const auto side:{zima::kernel::SurfaceThicknessSide::First,
        zima::kernel::SurfaceThicknessSide::Second,zima::kernel::SurfaceThicknessSide::Symmetric}) {
    auto inward=part;
    auto graph=inward.body_history;graph.activate(definition.bodies[1]);inward.set_body_history(std::move(graph));
    auto thicken=zima::document::create_surface_thicken();
    thicken.surface_thicken={definition.surface,1.,side};
    inward.insert_history_entry(zima::document::PartHistoryKind::Feature,thicken.id);
    inward.history.push_back(thicken);
    const auto thickened=kernel.evaluate_history(inward.kernel_operations());
    const auto& formed=thickened.back().body_outputs.at(definition.bodies[1]).get();
    check(formed.volume>0.&&!formed.kernel_shape.empty()&&formed.calculation_errors.empty(),
        "FORM outer shell could not be thickened");
    check(std::ranges::none_of(formed.mesh.triangle_references,[](const auto& face){return face.surface_result;}),
        "FORM thickness left an unconsumed surface patch");
    TopoDS_Shape solid;
    std::istringstream result_shape(formed.kernel_shape);BRepTools::Read(solid,result_shape,builder);
    check(BRepCheck_Analyzer(solid).IsValid(),"FORM thickness produced an invalid BRep");
    unsigned count=0;for(TopExp_Explorer it(solid,TopAbs_SOLID);it.More();it.Next())++count;
    check(count==1,"FORM thickness did not consume the complete connected shell");
    GProp_GProps properties;const double volume_error=BRepGProp::VolumePropertiesGK(solid,properties,1e-12,false,true);
    check(std::isfinite(volume_error)&&volume_error>=0.,"FORM rational volume integration did not converge");
    if(std::abs(properties.Mass()-formed.volume)>=1e-6)
        std::cerr<<"FORM volume: independently integrated="<<properties.Mass()<<", stored="<<formed.volume<<std::endl;
    check(std::abs(properties.Mass()-formed.volume)<1e-6,"FORM native volume disagrees with its stored value");
    check_offsets(skin,solid,side);
    if(side!=zima::kernel::SurfaceThicknessSide::Symmetric)
        for(TopExp_Explorer input(skin,TopAbs_VERTEX);input.More();input.Next()) {
            const auto position=BRep_Tool::Pnt(TopoDS::Vertex(input.Current()));
            bool retained=false;
            for(TopExp_Explorer output(solid,TopAbs_VERTEX);output.More();output.Next())
                if(position.Distance(BRep_Tool::Pnt(TopoDS::Vertex(output.Current())))<1e-9){retained=true;break;}
            check(retained,"One-sided FORM thickness moved an original shell endpoint");
        }
    check(part.serialized()==authored,"FORM thickening changed the authored definition");
    std::cout<<"FORM thickness mode "<<static_cast<unsigned>(side)<<" passed, volume "<<formed.volume<<std::endl;
    }
    check(!std::filesystem::exists(library.parent_path()/"VentilationWindow.drwz"),"Drawing was copied into FORM library");
    std::cout<<"Native FORM definition roles, independent references and cold regeneration passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
