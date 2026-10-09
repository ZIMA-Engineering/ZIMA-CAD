#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sheet_form_operations.hpp>
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <nlohmann/json.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepLib.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Result.hxx>
#include <ShapeFix_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <fstream>
#include <iostream>
#include <chrono>
#include <set>
#include <numbers>
#include <future>
#include <atomic>
#include <limits>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include "profile_solid_fixture.hpp"
using namespace zima;
namespace {
class ProbeShellThickener:public BRepOffsetAPI_MakeThickSolid {
public:
    void run(const TopoDS_Shape& shape,double thickness,GeomAbs_JoinType join) {
        NotDone();myLastUsedAlgo=OffsetAlgo_JOIN;
        myOffsetShape.Initialize(shape,thickness,1e-7,BRepOffset_Skin,false,false,join,true,false);
        myOffsetShape.MakeOffsetShape();
        std::cout<<"Joined error="<<int(myOffsetShape.Error())<<std::endl;
        if(myOffsetShape.IsDone()){myShape=myOffsetShape.Shape();Done();}
    }
};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6) {
    if(std::abs(a-b)>tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));
}
void valid(const kernel::BodyResult& result) {
    for(const auto& [owner,error]:result.calculation_errors)std::cerr<<owner<<": "<<error<<std::endl;
    check(result.calculation_errors.empty(),"FORM calculation failed");
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
    if(!shape.IsNull()&&!BRepCheck_Analyzer(shape,true,false,true).IsValid())
    {
        BRepTools::Write(shape,"build/form-diagnostic/form-native-invalid.brep");
        BRepCheck_Analyzer analyzer(shape,true,false,true);
        for(const auto kind:{TopAbs_EDGE,TopAbs_FACE,TopAbs_SHELL})for(TopExp_Explorer it(shape,kind);it.More();it.Next()) {
            const auto checked=analyzer.Result(it.Current());
            if(checked.IsNull())continue;
            for(checked->InitContextIterator();checked->MoreShapeInContext();checked->NextShapeInContext())
                for(const auto status:checked->StatusOnShape())if(status!=BRepCheck_NoError)
                    std::cerr<<"Invalid BRep kind="<<int(kind)<<" context="<<int(checked->ContextualShape().ShapeType())<<" status="<<int(status)<<'\n';
        }
    }
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape,true,false,true).IsValid(),"FORM produced an invalid BRep");
    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    check(solids==1,"FORM is not connected to the sheet");
    GProp_GProps properties;const auto error=BRepGProp::VolumePropertiesGK(shape,properties,1e-12,false,true);
    check(std::isfinite(error)&&error>=0.,"FORM volume integration failed");near(result.volume,properties.Mass());
    if(result.volume_integrals) {
        GProp_GProps tensor;const auto tensor_error=BRepGProp::VolumeProperties(shape,tensor,1e-12);
        check(std::isfinite(tensor_error)&&tensor_error>=0.,"Independent FORM tensor integration failed");
        const auto center=tensor.CentreOfMass();const auto expected=result.volume_integrals->centroid;
        near(center.X(),expected.x);near(center.Y(),expected.y);near(center.Z(),expected.z);
        const auto inertia=tensor.MatrixOfInertia();
        Bnd_Box bounds;BRepBndLib::Add(shape,bounds);
        double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        const auto span2=(xmax-xmin)*(xmax-xmin)+(ymax-ymin)*(ymax-ymin)+(zmax-zmin)*(zmax-zmin);
        const auto integration_error=8.*std::max(1e-12,tensor_error)*std::abs(tensor.Mass())*span2;
        double tensor_scale=0.;
        for(int row=0;row<3;++row)for(int column=0;column<3;++column)
            tensor_scale=std::max({tensor_scale,std::abs(inertia.Value(row+1,column+1)),
                std::abs(result.volume_integrals->inertia[3*row+column])});
        for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
            const auto actual=result.volume_integrals->inertia[3*row+column];
            const auto expected=inertia.Value(row+1,column+1);
            // Combined Body tensors use the parallel-axis theorem, while the
            // independent check integrates the complete BRep. At large values
            // one representable double already exceeds the absolute floor.
            // Products of inertia can cancel large terms, so use the scale of
            // the whole tensor rather than only the near-zero output entry.
            const auto rounding=8.*std::numeric_limits<double>::epsilon()*tensor_scale;
            near(expected,actual,1e-6+rounding+integration_error);
        }
    }
}
void verify_corner_transition_radii(const kernel::BodyResult& result,const std::string& owner,double thickness) {
    struct Sample {std::array<gp_Pnt,3> points;kernel::SheetFaceRole side;};
    std::vector<Sample> samples;std::set<std::string> seen;
    for(std::size_t i=0;i<result.mesh.triangle_references.size();++i) {
        const auto& ref=result.mesh.triangle_references[i];
        if(ref.owner_id!=owner||!ref.semantic_key.starts_with("fillet:face:")||
            !seen.insert(ref.semantic_key).second)continue;
        Sample sample;sample.side=ref.sheet_role;
        for(unsigned j=0;j<3;++j) {
            const auto& p=result.mesh.vertices.at(result.mesh.triangles.at(3*i+j));
            sample.points[j]=gp_Pnt(p.x,p.y,p.z);
        }
        samples.push_back(sample);
    }
    TopoDS_Shape body;BRep_Builder builder;std::istringstream data(result.kernel_shape);
    BRepTools::Read(body,data,builder);unsigned cavity=0,opposite=0;
    // Test-only exact geometry inspection: match three published face vertices
    // to the real trimmed surface, then measure curvature independently of
    // the requested Fillet values. No topology identities are created here.
    for(TopExp_Explorer it(body,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        const auto u=(surface.FirstUParameter()+surface.LastUParameter())*.5;
        const auto v=(surface.FirstVParameter()+surface.LastVParameter())*.5;
        BRepLProp_SLProps properties(surface,u,v,2,1e-9);
        if(!properties.IsCurvatureDefined())continue;
        const auto a=std::abs(properties.MinCurvature()),b=std::abs(properties.MaxCurvature());
        if(std::max(a,b)<1e-8)continue;
        for(const auto& sample:samples) {
            if(!std::ranges::all_of(sample.points,[&](const auto& point) {
                BRepExtrema_DistShapeShape distance(it.Current(),BRepBuilderAPI_MakeVertex(point).Shape());
                return distance.IsDone()&&distance.Value()<1e-6;
            }))continue;
            // Rolling-ball canal patches have one principal radius equal to
            // the circle radius, also along a curved spine. Multi-edge corner
            // fill patches need not have constant principal curvature.
            const auto expected=sample.side==kernel::SheetFaceRole::SideA?thickness:.25*thickness;
            if(std::min(a>1e-8?std::abs(1./a-expected):1e30,
                        b>1e-8?std::abs(1./b-expected):1e30)<1e-6) {
                if(sample.side==kernel::SheetFaceRole::SideA)++cavity;
                else if(sample.side==kernel::SheetFaceRole::SideB)++opposite;
            }
            break;
        }
    }
    check(cavity>0&&opposite>0,"Missing independently verified cavity/opposite circular transitions");
}

void verify_corner_axis(const document::PartDocument& part,const document::HistoryContainer& feature,
        const kernel::BodyResult& result) {
    using namespace kernel::sheet_material;
    std::vector<kernel::ViewerAxis> axes;
    for(const auto& axis:result.mesh.axes)if(axis.reference.owner_id==feature.id&&
        axis.reference.semantic_key.starts_with("centerline:from:centroid:form:"))axes.push_back(axis);
    check(axes.size()==1,"Corner requires one whole-profile centroid axis");
    const auto& axis=axes.front();const auto operation=document::sheet_form_operation(part,feature);
    const auto& request=std::get<kernel::SheetFormRequest>(operation.primitive);
    const auto source_z=cross(request.source_x,request.source_normal),world_z=cross(request.x_direction,request.normal);
    const auto source=[&](kernel::Vec3 point) {
        const auto relative=sub(point,request.position);
        return add(request.source_origin,add(mul(request.source_x,dot(relative,request.x_direction)),
            add(mul(request.source_normal,dot(relative,request.normal)),mul(source_z,dot(relative,world_z)))));
    };
    const auto a=source(add(axis.point,mul(axis.direction,(axis.display_length-2.)*.5)));
    const auto b=source(sub(axis.point,mul(axis.direction,(axis.display_length-2.)*.5)));
    for(const auto& face:request.solid_opening_faces) {
        const auto& plane=*face.surface;
        near(std::min(std::abs(dot(sub(a,plane.origin),plane.axis)),std::abs(dot(sub(b,plane.origin),plane.axis))),0.);
    }
    near(dot(sub(source(axis.point),request.source_origin),request.source_x),0.);
    check(std::ranges::none_of(result.mesh.points,[&](const auto& point) {
        return point.reference.owner_id==feature.id&&point.reference.semantic_key.starts_with("profile:path-point:");
    }),"Corner centroid axis added picking endpoints");
}

}
#include "planar_solid_form_experiment.inc"
int main(int argc,char** argv){try {
    if(argc==3&&std::string_view(argv[1])=="--probe-corner-surface-form")
        return corner_surface_form_experiment(argv[2]);
    if(argc==3&&std::string_view(argv[1])=="--probe-planar-solid-form")
        return planar_solid_form_experiment(argv[2]);
    if(argc==3&&(std::string_view(argv[1])=="--compare-copy-batch"||
        std::string_view(argv[1])=="--compare-ordinary-copy-batch")) {
        auto part=document::PartDocument::create_default();
        if(std::string_view(argv[1])=="--compare-ordinary-copy-batch") {
            const auto mode=std::string_view(argv[2]);
            const bool large=mode.starts_with("large");
            auto stock=test::rectangular_feature(part,{large?1000.:100.,40,4});
            auto source=test::rectangular_feature(part,{6,6,8});source.placement.x=large?-300.:-30.;
            source.combine_mode=mode.ends_with("cut")?document::CombineMode::Subtract:document::CombineMode::Add;
            part.history={stock,source};document::BodyHistoryGraph bodies;
            static_cast<void>(bodies.create_body("Batch fixture"));
            bodies.insert({document::PartHistoryKind::Feature,stock.id});bodies.insert({document::PartHistoryKind::Feature,source.id});part.set_body_history(bodies);
            kernel::OcctKernel kernel;auto previous=workspace::calculate_part_with_resolved_references(kernel,part);
            workspace::Workspace live;live.add_part(part,previous);
            auto edit=workspace::prepare_derived_copy_edit(live,part.document_id,{},true);auto value=edit.initial;
            value.parameters.source_id=source.id;value.parameters.pattern->linear[0].count=large?50:6;
            value.parameters.pattern->linear[0].spacing=mode.starts_with("overlap")?2.:mode.starts_with("touch")?6.:12.;
            check(workspace::commit_derived_copy(live,kernel,edit,value),"Ordinary batch fixture failed");
            part=live.open_part(part.document_id)->session.document();
        }else part=document::PartDocument::load(argv[2]);
        const auto environment=[](const char* value) {
#ifdef _WIN32
            _putenv_s("ZIMA_CPP_COPY_SEQUENTIAL",value);
#else
            if(*value)setenv("ZIMA_CPP_COPY_SEQUENTIAL",value,1);else unsetenv("ZIMA_CPP_COPY_SEQUENTIAL");
#endif
        };
        std::vector<kernel::BodyResult> results(2);
        const bool batch_first=std::getenv("ZIMA_CPP_BATCH_FIRST")!=nullptr;
        for(const bool sequential:{!batch_first,batch_first}) {
            environment(sequential?"1":"");kernel::OcctKernel kernel;
            const auto started=std::chrono::steady_clock::now();
            auto candidate=part;auto calculated=workspace::calculate_part_with_resolved_references(kernel,candidate);
            std::cout<<(sequential?"Sequential":"Batch")<<" cold calculation milliseconds="<<
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
            valid(calculated.back());results[sequential?0:1]=std::move(calculated.back());
        }
        environment("");const auto& a=results[0];const auto& b=results[1];
        near(a.volume,b.volume);near(a.surface_area,b.surface_area);
        check(a.volume_integrals.has_value()==b.volume_integrals.has_value(),"Batch lost volume integrals");
        if(a.volume_integrals) {
            near(a.volume_integrals->centroid.x,b.volume_integrals->centroid.x);
            near(a.volume_integrals->centroid.y,b.volume_integrals->centroid.y);
            near(a.volume_integrals->centroid.z,b.volume_integrals->centroid.z);
            for(unsigned i=0;i<9;++i)near(a.volume_integrals->inertia[i],b.volume_integrals->inertia[i],1e-6+std::abs(a.volume_integrals->inertia[i])*1e-10);
        }
        const auto references=[](const kernel::BodyResult& result) {
            const auto packet=document::serialize_body_result(result);
            std::set<std::string> refs;
            for(const auto& kind:{"faces","edges","vertices"})for(auto reference:packet.at("kernel_bindings").at(kind)) {
                reference.erase("locator");refs.insert(std::string(kind)+reference.dump());
            }
            return refs;
        };
        check(references(a)==references(b),"Batch changed topology reference identities or sheet sides");
        check(document::serialize_body_result(a).at("axes")==document::serialize_body_result(b).at("axes"),"Batch changed centroid axis identities or geometry");
        TopoDS_Shape shapes[2];BRep_Builder builder;
        for(unsigned i=0;i<2;++i){std::istringstream stream(results[i].kernel_shape);BRepTools::Read(shapes[i],stream,builder);}
        for(unsigned i=0;i<2;++i) {
            BRepAlgoAPI_Cut difference(shapes[i],shapes[1-i]);difference.Build();
            check(difference.IsDone(),"Batch geometric equivalence comparison failed");
            GProp_GProps properties;BRepGProp::VolumePropertiesGK(difference.Shape(),properties,1e-12,false,true);
            near(properties.Mass(),0.);
        }
        std::cout<<"Sequential/batch exact topology identity, side, centroid axis, mass properties and two-way geometric difference passed\n";
        return 0;
    }
    if((argc==3||argc==4)&&(std::string_view(argv[1])=="--verify-workspace-corner-copies"||
        std::string_view(argv[1])=="--verify-workspace-corner-copy-matrix")) {
        const bool matrix=std::string_view(argv[1])=="--verify-workspace-corner-copy-matrix";
        std::vector<kernel::BodyResult> previous;auto part=document::PartDocument::load(argv[2],&previous);
        const auto form=std::ranges::find_if(part.history.rbegin(),part.history.rend(),[&](const auto& f){return f.feature_kind==document::FeatureKind::SheetForm&&(argc==3||f.id==argv[3]);});
        check(form!=part.history.rend(),"Missing corner Form");
        const auto source=form->id;part.body_history.activate(part.body_owner_for_object(source)->scope.id);
        const auto unchanged_input=document::serialize_body_result(previous.back());
        for(unsigned scenario=0;scenario<(matrix?6u:2u);++scenario) {
            const bool pattern=matrix?scenario<5:scenario==0;
            if(std::getenv("ZIMA_CPP_CORNER_MIRROR_ONLY")&&pattern)continue;
            workspace::Workspace live;live.add_part(part,previous);kernel::OcctKernel kernel;
            const auto edit=workspace::prepare_derived_copy_edit(live,part.document_id,{},pattern);
            auto value=edit.initial;value.parameters.source_id=source;
            if(pattern) {value.parameters.pattern->linear[0].count=2;
                value.parameters.pattern->linear[0].spacing=30.;
                auto& direction=value.parameters.pattern->linear[0];
                if(matrix&&scenario==1){direction.count=4;direction.spacing=20.;}
                if(matrix&&scenario==2)direction.distribution=kernel::PatternDistribution::Reverse;
                if(matrix&&scenario==3)direction.distribution=kernel::PatternDistribution::Both;
                if(matrix&&scenario==4){direction.distribution=kernel::PatternDistribution::Symmetric;direction.count=3;}
            }
            else {value.parameters.reference={{},value.id+":origin","origin:plane:yz"};value.placement.x=15.;}
            if(!pattern)std::cout<<"Mirror pending source="<<source<<" x="<<value.placement.x<<" reference="<<value.parameters.reference.semantic_key<<std::endl;
            const auto started=std::chrono::steady_clock::now();
            check(workspace::commit_derived_copy(live,kernel,edit,value),"Copy did not commit");
            std::cout<<(pattern?"Pattern":"Mirror")<<" workspace milliseconds="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
            auto* state=live.open_part(part.document_id);const auto& result=state->session.calculated_boundaries().back();valid(result);
            verify_corner_axis(state->session.document(),*state->session.document().find_container(source),result);
            check(std::ranges::any_of(result.mesh.axes,[&](const auto& axis){return axis.reference.owner_id==value.id&&axis.reference.semantic_key.starts_with("centerline:from:centroid:copy:");}),"Copied Form centroid axis missing");
            const auto annotations=workspace::drawing_annotation_sources(&live,part.document_id,{});
            drawing::DrawingView view;view.id="corner-axis-view";view.source_document_id=part.document_id;
            drawing::refresh_model_annotations(view,annotations);
            check(std::ranges::any_of(view.model_annotations,[&](const auto& item) {
                return item.kind==drawing::ModelAnnotationKind::Axis&&item.source.owner_id==value.id&&
                    item.source.semantic_id.starts_with("centerline:from:centroid:copy:");
            }),"Drawing did not offer copied Form centroid axis");
            for(const auto& item:view.model_annotations)if(item.kind==drawing::ModelAnnotationKind::Axis&&item.source.owner_id==value.id) {
                const auto axis=std::ranges::find_if(result.mesh.axes,[&](const auto& value){return value.reference.owner_id==item.source.owner_id&&value.reference.semantic_key==item.source.semantic_id;});
                check(axis!=result.mesh.axes.end()&&item.model_axis.has_value(),"Drawing axis lost its exact model geometry");
                const auto delta=kernel::sheet_material::sub((*item.model_axis)[1],(*item.model_axis)[0]);
                near(std::sqrt(kernel::sheet_material::dot(delta,delta)),axis->display_length);
            }
            const auto reuse_started=std::chrono::steady_clock::now();
            const auto reused=kernel.evaluate_history_incremental(state->session.document().kernel_operations(),state->session.calculated_boundaries());
            check(document::serialize_body_result(reused.back())==document::serialize_body_result(result),
                "Unchanged corner-copy reuse changed geometry, properties or references");
            std::cout<<"Unchanged corner-copy reuse milliseconds="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-reuse_started).count()<<std::endl;
            const auto revision=state->session.revision();
            const auto unchanged=workspace::prepare_derived_copy_edit(live,part.document_id,value.id,pattern);
            check(!workspace::commit_derived_copy(live,kernel,unchanged,unchanged.initial)&&state->session.revision()==revision,"Unchanged copy changed geometry or history");
            const auto path=std::filesystem::path("build/form-diagnostic")/(pattern?"workspace-corner-pattern.prtz":"workspace-corner-mirror.prtz");
            state->session.document().save(path,state->session.calculated_boundaries());
            std::vector<kernel::BodyResult> reopened;auto saved=document::PartDocument::load(path,&reopened);valid(reopened.back());near(reopened.back().volume,result.volume);
            if(matrix)state->session.document().save(std::filesystem::path("build/form-diagnostic")/("corner-copy-case-"+std::to_string(scenario)+".prtz"),state->session.calculated_boundaries());
            auto calculated=workspace::calculate_part_with_resolved_references(kernel,saved,&reopened);valid(calculated.back());near(calculated.back().volume,result.volume);
            check(state->session.undo()&&!state->session.document().find_container(value.id),"Corner copy Undo failed");
            check(state->session.redo()&&state->session.document().find_container(value.id),"Corner copy Redo failed");
            if(scenario==0) {
                auto changed=workspace::prepare_derived_copy_edit(live,part.document_id,value.id,true);auto increased=changed.initial;
                increased.parameters.pattern->linear[0].count=3;
                const auto old_volume=state->session.calculated_boundaries().back().volume;
                const auto input_document=state->session.document().serialized();const auto input_revision=state->session.revision();
                bool edited=false;
                try {edited=workspace::commit_derived_copy(live,kernel,changed,increased);}
                catch(const std::exception&) {
                    check(state->session.document().serialized()==input_document&&state->session.revision()==input_revision,
                        "Rejected overlapping copy edit changed the document");
                    std::cout<<"Increasing count intersected another Form; atomic rejection verified, retrying reverse direction\n";
                    increased.parameters.pattern->linear[0].distribution=kernel::PatternDistribution::Reverse;
                    edited=workspace::commit_derived_copy(live,kernel,changed,increased);
                }
                check(edited,"Changed copy incorrectly reused previous result");
                valid(state->session.calculated_boundaries().back());
                check(std::abs(state->session.calculated_boundaries().back().volume-old_volume)>1e-5,"Changed copy count did not change geometry");
                check(state->session.undo(),"Changed copy Undo failed");
                auto unfold=document::PartDocument::create_sketch_container();unfold.feature_kind=document::FeatureKind::Unbend;unfold.sheet_state.all=true;
                check(workspace::commit_sheet_state(live,kernel,part.document_id,unfold),"Corner-copy Unbend did not commit");
                const auto& flat=state->session.calculated_boundaries().back();valid(flat);
                check(std::ranges::none_of(state->session.body_context_mesh().axes,[](const auto& axis){return kernel::sheet_material::is_form_centroid_axis(axis.reference);}),"Flat Body context resurrected a persisted corner centroid axis");
                check(std::ranges::none_of(flat.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_form_centroid_axis(axis.reference);}),"Flat View retained a corner centroid axis");
                check(std::ranges::none_of(flat.mesh.edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("form:symbol:");}),"Flat View retained a corner manufacturing symbol");
                const auto flat_path=std::filesystem::path("build/form-diagnostic/workspace-corner-flat.prtz");
                state->session.document().save(flat_path,state->session.calculated_boundaries());
                std::vector<kernel::BodyResult> flat_cache;auto flat_part=document::PartDocument::load(flat_path,&flat_cache);
                workspace::Workspace flat_live;flat_live.add_part(flat_part,flat_cache);
                const auto flat_annotations=workspace::drawing_annotation_sources(&flat_live,part.document_id,{});
                for(const auto& annotations:flat_annotations) {
                    check(std::ranges::none_of(annotations.axes,[](const auto& axis){return kernel::sheet_material::is_form_centroid_axis(axis.reference);}),"Flat Drawing offered a corner centroid axis");
                    check(std::ranges::none_of(annotations.symbols,[](const auto& symbol){return symbol.symbol.id.starts_with("form:symbol:");}),"Flat Drawing offered a corner manufacturing symbol");
                }
                drawing::refresh_model_annotations(view,flat_annotations);
                check(std::ranges::all_of(view.model_annotations,[](const auto& item) {
                    return !item.source.semantic_id.starts_with("centerline:from:centroid:")||item.unresolved;
                }),"Flat Drawing retained a resolved spatial corner axis");
                auto restore=document::PartDocument::create_sketch_container();restore.feature_kind=document::FeatureKind::BendBack;restore.sheet_state.all=true;
                check(workspace::commit_sheet_state(live,kernel,part.document_id,restore),"Corner-copy Bend Back did not commit");
                verify_corner_axis(state->session.document(),*state->session.document().find_container(source),state->session.calculated_boundaries().back());
                drawing::refresh_model_annotations(view,workspace::drawing_annotation_sources(&live,part.document_id,{}));
                check(std::ranges::any_of(view.model_annotations,[&](const auto& item) {
                    return item.source.owner_id==value.id&&item.source.semantic_id.starts_with("centerline:from:centroid:copy:")&&!item.unresolved;
                }),"Bend Back did not restore the copied Drawing axis");
                check(state->session.undo()&&state->session.undo(),"Corner-copy material state Undo failed");
                auto bad=workspace::prepare_derived_copy_edit(live,part.document_id,{},true);auto invalid=bad.initial;
                invalid.parameters.source_id=source;invalid.parameters.pattern->linear[0].spacing=10000.;
                const auto before=state->session.document().serialized();const auto revision_before=state->session.revision();bool rejected=false;
                try {static_cast<void>(workspace::commit_derived_copy(live,kernel,bad,invalid));}catch(const std::exception&){rejected=true;}
                check(rejected&&before==state->session.document().serialized()&&revision_before==state->session.revision(),"Disconnected copy changed the document");
            }
            check(document::serialize_body_result(previous.back())==unchanged_input,"Copy workflow mutated shared source boundary cache");
        }
        return 0;
    }
    if(argc==5&&std::string_view(argv[1])=="--replace-corner-definition") {
        std::vector<kernel::BodyResult> previous;auto part=document::PartDocument::load(argv[2],&previous);
        const auto source=document::read_sheet_form_definition(argv[3]);
        auto found=std::ranges::find_if(part.history.rbegin(),part.history.rend(),[](const auto& f){return f.feature_kind==document::FeatureKind::SheetForm;});
        check(found!=part.history.rend()&&source.corner(),"Missing native corner definition or feature");
        const auto original=*found;auto replacement=original;
        replacement.sheet_form=document::copy_sheet_form_definition(source);
        replacement.sheet_form.support=original.sheet_form.support;replacement.sheet_form.thickness=original.sheet_form.thickness;
        workspace::Workspace live;live.add_part(part,previous);kernel::OcctKernel kernel;
        check(workspace::commit_sheet_form(live,kernel,part.document_id,replacement),"Corner definition replacement did not commit");
        const auto* state=live.open_part(part.document_id);valid(state->session.calculated_boundaries().back());
        verify_corner_transition_radii(state->session.calculated_boundaries().back(),original.id,original.sheet_form.thickness);
        check(state->session.document().find_container(original.id)->placement.references==original.placement.references,"Definition replacement changed native placement references");
        state->session.document().save(argv[4],state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened;const auto native=document::PartDocument::load(argv[4],&reopened);valid(reopened.back());
        verify_corner_transition_radii(reopened.back(),original.id,original.sheet_form.thickness);
        check(native.find_container(original.id)->sheet_form.source_name==source.part.name,"Replacement did not persist independent definition");
        std::cout<<"Native corner definition replacement and exact BRep/save/reopen verified\n";return 0;
    }
    if(argc==4&&std::string_view(argv[1])=="--finalize-solid-definition") {
        const auto source=document::read_sheet_form_definition(argv[2]);auto part=source.part;
        auto symbol=sketcher::Sketch::create_default();
        const auto old=std::ranges::find(part.sketches,source.symbol_sketch,&sketcher::Sketch::id);
        check(old!=part.sketches.end(),"Missing corner symbol Sketch");
        symbol.id=old->id;symbol.owner_container_id=old->owner_container_id;symbol.name="Corner gusset symbol";
        symbol.plane=sketcher::SketchPlane::XY;symbol.plane_auto=false;
        std::set<std::pair<std::string,std::string>> seen;
        for(const auto& edge:source.calculated->back().body_outputs.at(source.bodies[1])->mesh.edges)
            if(edge.reference.valid()&&!edge.parameter_seam&&edge.points.size()>1&&
               std::ranges::all_of(edge.points,[](const auto& point){return std::abs(point.z)<1e-8;})&&
               seen.emplace(edge.reference.owner_id,edge.reference.semantic_key).second) {
                auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
                reference.source_document_id=part.document_id;reference.source_owner_id=edge.reference.owner_id;
                reference.source_semantic_key=edge.reference.semantic_key;reference.body_edge=true;
                reference.exact_spline=symbol.project_external_spline(edge);
                for(const auto& point:edge.points)reference.cached_points.push_back({point.x,point.y});
                symbol.add_external_reference(reference);static_cast<void>(symbol.add_external_profile_geometry(reference.id));
            }
        check(!symbol.segments.empty()&&!symbol.bsplines.empty(),"Corner symbol lost its exact footprint");
        // The manufacturing symbol is an independent authored Sketch. Retire
        // the one-time projection markers while retaining its exact curves;
        // role lookup must not introduce a Body-order dependency.
        symbol.import_blocks.clear();symbol.external_references.clear();symbol.curve_supports.clear();
        symbol.constraints.clear();symbol.dimensions.clear();symbol.dimension_layouts.clear();
        *old=std::move(symbol);part.name="Corner gusset 90 degrees";
        for(auto& feature:part.history)if(part.body_history.owner(feature.id)->name=="FORM") {
            feature.name="Gusset solid";feature.feature.automatic_name=feature.name;
            for(auto& sketch:part.sketches)if(sketch.owner_container_id==feature.id)sketch.name="Gusset profile";
        }
        auto graph=part.body_history;
        for(const auto& old_body:part.body_history.bodies()) {
            auto body=old_body;body.dependencies.clear();graph.update_body(std::move(body));
        }
        // Explicit reference resolution below rebuilds actual dependencies;
        // discarded cutting/surface references must not survive as metadata.
        part.set_body_history(std::move(graph));
        kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part,source.calculated.get());
        check(calculated.back().calculation_errors.empty(),"Final corner definition failed regeneration");
        part.save(argv[3],calculated);const auto reopened=document::read_sheet_form_definition(argv[3]);
        check(reopened.solid_opening_faces.size()==2&&reopened.cut_sketch.empty(),"Final corner roles changed");
        std::cout<<"Closed corner definition and exact XY footprint symbol finalized\n";return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--inspect-definition") {
        const auto part=document::PartDocument::load(argv[2]);
        std::ofstream output("build/form-diagnostic/inspected-definition.json");output<<part.serialized().dump(2);
        check(bool(output),"Definition diagnostic export failed");return 0;
    }
    if(argc==5&&(std::string_view(argv[1])=="--scale-definition"||std::string_view(argv[1])=="--straight-definition")) {
        auto part=document::PartDocument::load(argv[2]);const auto factor=std::stod(argv[4]);
        check(factor>0.,"Invalid definition size");
        const auto body=std::ranges::find(part.body_history.bodies(),"FORM",&document::BodyHistory::name);
        check(body!=part.body_history.bodies().end()&&!body->entries.empty(),"Missing FORM shape");
        const auto owner=body->entries.front().id;
        for(auto& sketch:part.sketches)if(sketch.owner_container_id==owner) {
            for(auto& point:sketch.points){point.x*=factor;point.y*=factor;}
            for(auto& arc:sketch.arcs)arc.radius*=factor;
            for(auto& dimension:sketch.dimensions)dimension.value*=factor;
            if(std::string_view(argv[1])=="--straight-definition") {
                check(sketch.arcs.size()==1,"Expected one cap arc");
                auto& arc=sketch.arcs.front();const auto center=*sketch.find_point(arc.center_point_id);
                auto* a=sketch.find_point(arc.start_point_id);auto* b=sketch.find_point(arc.end_point_id);
                a->x=center.x-arc.radius;a->y=center.y;b->x=center.x+arc.radius;b->y=center.y;
                for(auto& point:sketch.points)if(std::abs(point.y)<1e-9&&std::abs(point.x)>1.)point.x=std::copysign(arc.radius,point.x);
                arc.start_angle=-std::numbers::pi;arc.end_angle=0.;
                for(auto& dimension:sketch.dimensions)if(dimension.kind==sketcher::DimensionKind::Distance)dimension.value=2.*arc.radius;
            }
            sketch.validate();
        }
        kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Resized definition failed regeneration");
        part.save(argv[3],calculated);return 0;
    }
    if(argc==4&&std::string_view(argv[1])=="--prepare-solid-definition") {
        auto part=document::PartDocument::load(argv[2]);auto graph=part.body_history;
        std::set<std::string> discarded;
        for(const auto& old:part.body_history.bodies()) {
            auto body=old;
            if(body.name=="FORM_CUT")for(const auto& entry:body.entries)discarded.insert(entry.id);
            if(body.name=="FORM")for(std::size_t i=1;i<body.entries.size();++i)discarded.insert(body.entries[i].id);
            std::erase_if(body.entries,[&](const auto& e){return discarded.contains(e.id);});body.cursor=body.entries.size();
            graph.update_body(std::move(body));
        }
        std::erase_if(part.history,[&](const auto& f){return discarded.contains(f.id);});
        std::erase_if(part.sketches,[&](const auto& s){return discarded.contains(s.owner_container_id);});part.set_body_history(std::move(graph));
        for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)->name=="FORM_SYMBOL") {
            // Retain the authored symbol curves as ordinary local geometry;
            // references to the discarded Surface conversion are retired.
            sketch.import_blocks.clear();sketch.external_references.clear();sketch.curve_supports.clear();
            sketch.constraints.clear();sketch.dimensions.clear();sketch.dimension_layouts.clear();
        }
        part.name="Corner gusset 90 degrees";
        kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        part.save(argv[3],calculated);
        std::cout<<"Solid definition calculated and saved\n";
        const auto definition=document::sheet_form_definition(part,calculated);
        std::cout<<"Solid definition recognized\n";
        check(definition.cut_sketch.empty()&&definition.solid_opening_faces.size()==2,"Solid corner recognition failed");
        const auto parameters=document::copy_sheet_form_definition(definition);
        std::cout<<"Solid definition independently copied\n";
        const auto copied=document::stored_sheet_form_definition(parameters);
        check(copied.solid_opening_faces.size()==2&&copied.cut_sketch.empty(),"Solid corner copy failed");
        part.save(argv[3],calculated);return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--probe-volume-parallel") {
        std::ifstream packet(argv[2]);nlohmann::json value;packet>>value;
        const auto result=document::load_body_result(value);
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
        check(!shape.IsNull(),"Missing diagnostic body");
        GProp_GProps tensor;const auto gauss_start=std::chrono::steady_clock::now();
        const auto gauss_error=BRepGProp::VolumeProperties(shape,tensor,1e-12);
        std::cout<<"Adaptive full tensor seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-gauss_start).count()<<" error="<<gauss_error<<std::endl;
        gp_XYZ sum(0,0,0);unsigned count=0;
        for(TopExp_Explorer it(shape,TopAbs_VERTEX);it.More();it.Next(),++count)sum+=BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).XYZ();
        check(count>0,"Missing body vertices");sum/=count;const gp_Pnt location(sum);
        std::vector<TopoDS_Face> faces;for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())faces.push_back(TopoDS::Face(it.Current()));
        for(unsigned workers:{1u,2u,4u}) {
            const auto start=std::chrono::steady_clock::now();
            std::vector<GProp_GProps> pieces(faces.size());std::vector<double> errors(faces.size());std::atomic_size_t cursor{0};
            std::vector<std::future<void>> jobs;
            for(unsigned worker=0;worker<workers;++worker)jobs.push_back(std::async(std::launch::async,[&] {
                for(auto index=cursor.fetch_add(1);index<faces.size();index=cursor.fetch_add(1)) {
                    BRepGProp_Face face(faces[index],true);BRepGProp_Domain domain(faces[index]);
                    BRepGProp_VinertGK properties;properties.SetLocation(location);
                    errors[index]=faces[index].NbChildren()==0?properties.Perform(face,1e-12,false,false):properties.Perform(face,domain,1e-12,false,false);
                    pieces[index]=properties;
                }
            }));
            for(auto& job:jobs)job.get();
            GProp_GProps total;
            for(std::size_t index=0;index<pieces.size();++index) {
                check(std::isfinite(errors[index])&&errors[index]>=0.,"Parallel GK integration failed");total.Add(pieces[index]);
            }
            near(total.Mass(),result.volume);
            std::cout<<"Parallel GK workers="<<workers<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" difference="<<total.Mass()-result.volume<<std::endl;
        }
        return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--probe-volume-directions") {
        std::ifstream packet(argv[2]);nlohmann::json value;packet>>value;
        const auto result=document::load_body_result(value);
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
        check(!shape.IsNull(),"Missing diagnostic body");
        for(const auto axis:{kernel::Vec3{1,0,0},kernel::Vec3{0,1,0},kernel::Vec3{0,0,1}}) {
            GProp_GProps properties;const auto start=std::chrono::steady_clock::now();
            const auto error=BRepGProp::VolumePropertiesGK(shape,properties,gp_Pln(gp_Pnt(0,0,0),gp_Dir(axis.x,axis.y,axis.z)),1e-12,false,true);
            check(std::isfinite(error)&&error>=0.,"Directed volume integration failed");near(properties.Mass(),result.volume);
            std::cout<<"Directed GK "<<axis.x<<','<<axis.y<<','<<axis.z<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" volume="<<properties.Mass()<<" error="<<error<<std::endl;
        }
        return 0;
    }
    if((argc==4||argc==5)&&std::string_view(argv[1])=="--prepare-inner-corner") {
        auto source=document::read_sheet_form_definition(argv[2]);auto part=source.part;
        auto graph=part.body_history;auto body=*graph.find(source.bodies[1]);
        body.scope.placement.rotation_x=180.;body.scope.placement.rotation_offset_x=180.;
        body.scope.placement.absolute_rotation_x=180.;graph.update_body(std::move(body));part.set_body_history(std::move(graph));
        kernel::OcctKernel kernel;
        auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        for(const auto& result:calculated)check(result.calculation_errors.empty(),"Inner corner source rotation failed");
        if(argc==5) {
            const auto factor=std::stod(argv[4]);check(factor>.5&&factor<1.,"Invalid inner corner inset");
            for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)->scope.id==source.bodies[0]) {
                sketch.import_blocks.clear();
                for(auto& point:sketch.points){point.x*=factor;point.y*=factor;}sketch.validate();
            }
            calculated=workspace::calculate_part_with_resolved_references(kernel,part,&calculated);
            for(const auto& result:calculated)check(result.calculation_errors.empty(),"Inner corner inset failed");
        }
        const auto definition=document::sheet_form_definition(part,calculated);
        check(definition.cut_sketches.size()==2,"Inner corner lost cutting roles");
        const auto cut=std::ranges::find(part.sketches,definition.cut_sketch,&sketcher::Sketch::id);
        check(cut!=part.sketches.end()&&cut->normal().z>1.-1e-10,"Inner corner rotated the attachment frame");
        const auto& surface=calculated.back().body_outputs.at(source.bodies[1])->mesh;
        for(const auto& p:surface.vertices)check(p.y>=-1e-7&&p.z>=-1e-7,"Inner corner source did not enter positive quadrant");
        part.save(argv[3],calculated);
        std::cout<<"Inner corner source placed in positive Y/Z; original CUT attachment frame and native IDs retained\n";return 0;
    }
    if((argc==4||argc==5)&&std::string_view(argv[1])=="--prepare-simple-corner") {
        auto part=document::PartDocument::load(argv[2]);
        const auto form=std::ranges::find(part.body_history.bodies(),"FORM",&document::BodyHistory::name);
        const auto cut=std::ranges::find(part.body_history.bodies(),"FORM_CUT",&document::BodyHistory::name);
        check(form!=part.body_history.bodies().end()&&cut!=part.body_history.bodies().end(),"Missing corner roles");
        const auto form_id=form->scope.id,cut_id=cut->scope.id,first_id=form->entries.front().id;
        std::string shell_id;for(const auto& entry:form->entries)if(part.find_container(entry.id)->feature_kind==document::FeatureKind::Shell)shell_id=entry.id;
        check(!shell_id.empty(),"Missing source surface conversion");
        kernel::OcctKernel kernel;std::vector<kernel::HistoryOperation> first_operations;
        for(const auto& operation:part.kernel_operations())if(operation.body.id==form_id){first_operations.push_back(operation);if(operation.owner_id==first_id)break;}
        const auto first=kernel.evaluate_history(first_operations);check(!first.empty(),"Missing first source sweep");
        std::map<std::pair<std::string,std::string>,kernel::FaceReference> caps;
        for(const auto& face:first.back().mesh.triangle_references)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            (argc==5||std::abs(face.surface->axis.y)>1.-1e-10||std::abs(face.surface->axis.z)>1.-1e-10))caps.emplace(std::pair{face.owner_id,face.semantic_key},face);
        check(caps.size()>=2,"First source sweep needs XY and XZ caps");
        std::set<std::string> discarded;for(const auto& entry:form->entries)if(entry.id!=first_id&&entry.id!=shell_id)discarded.insert(entry.id);
        auto graph=part.body_history;auto retained=*form;std::erase_if(retained.entries,[&](const auto& entry){return discarded.contains(entry.id);});retained.cursor=retained.entries.size();graph.update_body(std::move(retained));
        std::erase_if(part.history,[&](const auto& feature){return discarded.contains(feature.id);});
        std::erase_if(part.sketches,[&](const auto& sketch){return discarded.contains(sketch.owner_container_id);});part.set_body_history(std::move(graph));
        auto* shell=part.find_container(shell_id);shell->shell.removed_faces.clear();for(const auto& [key,face]:caps)shell->shell.removed_faces.push_back(face);
        for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)->scope.id==cut_id) {
            auto empty=sketcher::Sketch::create_default();empty.id=sketch.id;empty.owner_container_id=sketch.owner_container_id;
            empty.plane=sketch.plane;empty.plane_auto=sketch.plane_auto;empty.resolved_origin=sketch.resolved_origin;
            empty.resolved_normal=sketch.resolved_normal;empty.resolved_x_axis=sketch.resolved_x_axis;empty.resolved_y_axis=sketch.resolved_y_axis;sketch=std::move(empty);
        }
        auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Simple source sweep conversion failed");
        const auto& mesh=calculated.back().body_outputs.at(form_id)->mesh;
        for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)->scope.id==cut_id) {
            std::set<std::pair<std::string,std::string>> seen;
            for(const auto& edge:mesh.edges)if(edge.reference.valid()&&!edge.parameter_seam&&edge.points.size()>1&&
                std::ranges::all_of(edge.points,[&](const auto& p){return std::abs(kernel::sheet_material::dot(kernel::sheet_material::sub(p,sketch.resolved_origin),sketch.normal()))<1e-8;})&&seen.emplace(edge.reference.owner_id,edge.reference.semantic_key).second) {
                auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
                reference.source_document_id=part.document_id;reference.source_owner_id=edge.reference.owner_id;reference.source_semantic_key=edge.reference.semantic_key;reference.body_edge=true;
                reference.exact_spline=sketch.project_external_spline(edge);check(reference.exact_spline.has_value(),"Simple corner rim has no exact curve");
                for(const auto& p:edge.points){const auto local=sketch.local_point(p);reference.cached_points.push_back({local[0],local[1]});}
                sketch.add_external_reference(reference);static_cast<void>(sketch.add_external_profile_geometry(reference.id));
            }
            std::map<std::string,unsigned> degree;
            for(const auto& line:sketch.segments){++degree[line.first_point_id];++degree[line.second_point_id];}
            for(const auto& curve:sketch.bsplines){++degree[curve.control_point_ids.front()];++degree[curve.control_point_ids.back()];}
            std::vector<std::string> open;for(const auto& [id,count]:degree)if(count==1)open.push_back(id);
            if(open.size()==2){const auto a=*sketch.find_point(open[0]),b=*sketch.find_point(open[1]);static_cast<void>(sketch.add_segment(a.x,a.y,b.x,b.y,1e-9));}
            std::cout<<"Simple rim segments="<<sketch.segments.size()<<" splines="<<sketch.bsplines.size()<<std::endl;
            sketch.validate();
        }
        calculated=workspace::calculate_part_with_resolved_references(kernel,part,&calculated);check(calculated.back().calculation_errors.empty(),"Simple corner rim regeneration failed");
        part.save(argv[3],calculated);std::cout<<"Exact first-sweep corner surface and two linked cutting rims prepared\n";return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--probe-brep") {
        TopoDS_Shape shape;BRep_Builder builder;std::ifstream data(argv[2]);BRepTools::Read(shape,data,builder);
        BRepCheck_Analyzer check(shape,true,false,true);
        std::cout<<"BRep valid="<<check.IsValid()<<std::endl;
        for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next())if(!check.IsValid(it.Current())) {
            std::cout<<"Invalid edge status:";for(const auto status:check.Result(it.Current())->Status())std::cout<<' '<<int(status);std::cout<<std::endl;
        }
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())if(!check.IsValid(it.Current())) {
            std::cout<<"Invalid face status:";for(const auto status:check.Result(it.Current())->Status())std::cout<<' '<<int(status);std::cout<<std::endl;
        }
        BRepAlgoAPI_Check interference(shape,false,true);std::cout<<"Interference valid="<<interference.IsValid()<<std::endl;
        for(const auto& failure:interference.Result())std::cout<<"Interference status="<<int(failure.GetCheckStatus())<<std::endl;
        const auto copied=BRepBuilderAPI_Copy(shape,true,false).Shape();
        BRepLib::SameParameter(copied,1e-7,true);
        std::cout<<"SameParameter exact valid="<<BRepCheck_Analyzer(copied,true,false,true).IsValid()<<std::endl;
        ShapeFix_Shape repair(copied);repair.SetPrecision(1e-7);repair.SetMaxTolerance(1e-7);repair.Perform();
        std::cout<<"Shape repair exact valid="<<BRepCheck_Analyzer(repair.Shape(),true,false,true).IsValid()<<std::endl;
        return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--probe-corner-thickening") {
        const auto source=document::read_sheet_form_definition(argv[2]);
        const auto request=document::sheet_form_request(source,{}, {},{0,0,1},{1,0,0},1.);
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(request.surface_snapshot->kernel_shape);BRepTools::Read(shape,data,builder);
        for(TopExp_Explorer shell(shape,TopAbs_SHELL);shell.More();shell.Next()){shape=BRepBuilderAPI_Copy(shell.Current(),true,false).Shape();break;}
        std::cout<<"Source valid="<<BRepCheck_Analyzer(shape,true,false,true).IsValid()<<std::endl;
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);GProp_GProps area;BRepGProp::SurfaceProperties(face,area);
            const auto center=area.CentreOfMass();double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;surface.D1((u0+u1)*.5,(v0+v1)*.5,p,du,dv);auto normal=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();normal.Normalize();
            std::cout<<"Source face type="<<int(surface.GetType())<<" area="<<area.Mass()<<" center="<<center.X()<<','<<center.Y()<<','<<center.Z()<<" normal="<<normal.X()<<','<<normal.Y()<<','<<normal.Z()<<std::endl;
        }
        for(double thickness:{-1.,1.,-.1,.1}) {
            for(const auto join:{GeomAbs_Intersection,GeomAbs_Arc}) {
                ProbeShellThickener joined;joined.run(shape,thickness,join);
                std::cout<<"Joined thickness="<<thickness<<" join="<<int(join)<<" done="<<joined.IsDone()<<std::endl;
                if(joined.IsDone())std::cout<<"valid="<<BRepCheck_Analyzer(joined.Shape(),true,false,true).IsValid()<<" interference="<<BRepAlgoAPI_Check(joined.Shape(),false,true).IsValid()<<std::endl;
            }
            BRepOffsetAPI_MakeThickSolid thick;thick.MakeThickSolidBySimple(shape,thickness);
            std::cout<<"Simple thickness="<<thickness<<" done="<<thick.IsDone()<<std::endl;
            if(!thick.IsDone())continue;
            TopTools_IndexedMapOfShape solids;TopExp::MapShapes(thick.Shape(),TopAbs_SOLID,solids);
            std::cout<<"solids="<<solids.Extent()<<" valid="<<BRepCheck_Analyzer(thick.Shape(),true,false,true).IsValid()<<std::endl;
            if(solids.Extent()==1){auto solid=TopoDS::Solid(solids.FindKey(1));std::cout<<"orient="<<BRepLib::OrientClosedSolid(solid)<<" valid="<<BRepCheck_Analyzer(solid,true,false,true).IsValid()<<std::endl;}
            BRepTools::Write(thick.Shape(),("build/form-diagnostic/thick-"+std::to_string(thickness)+".brep").c_str());
        }
        return 0;
    }
    if((argc==3||argc==5)&&(std::string_view(argv[1])=="--verify-corner-bend"||std::string_view(argv[1])=="--compare-body-corner"||std::string_view(argv[1])=="--compare-native-corner-shell"||std::string_view(argv[1])=="--compare-shell-body-corner"||std::string_view(argv[1])=="--verify-solid-corner")) {
        using namespace kernel::sheet_material;
        const bool body_cut=std::string_view(argv[1])!="--verify-corner-bend";
        const bool solid_definition=std::string_view(argv[1])=="--verify-solid-corner";
        const bool shell_insert=solid_definition||std::string_view(argv[1])=="--compare-shell-body-corner";
        std::vector<kernel::FaceReference> opening_faces;
        const auto thickness=argc==5?std::stod(argv[3]):1.;
        const auto inner_radius=argc==5?std::stod(argv[4]):2.;
        check(thickness>0.&&inner_radius>0.,"Invalid corner sheet dimensions");
        const auto source=document::read_sheet_form_definition(argv[2]);check(solid_definition?source.solid_opening_faces.size()==2:source.cut_sketches.size()==2,"Expected corner FORM");
        std::shared_ptr<const kernel::BodyResult> closed_source;
        kernel::OcctKernel kernel;
        if(body_cut&&!solid_definition) {
            std::vector<kernel::HistoryOperation> local;
            for(auto operation:source.part.kernel_operations())if(operation.body.id==source.bodies[1]) {
                operation.body={};local.push_back(std::move(operation));
            }
            check(local.size()==2,"Expected one solid followed by Surface conversion");local.pop_back();
            auto calculated=kernel.evaluate_history(local);
            check(!calculated.empty()&&calculated.back().calculation_errors.empty()&&calculated.back().volume>0.,"Closed source calculation failed");
            closed_source=std::make_shared<const kernel::BodyResult>(calculated.back());
            if(shell_insert||std::string_view(argv[1])=="--compare-native-corner-shell") {
                kernel::ShellRequest shell;shell.thickness=thickness;
                std::set<std::pair<std::string,std::string>> selected;
                for(const auto& face:calculated.back().mesh.triangle_references)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                    (std::abs(face.surface->axis.y)>1.-1e-10||std::abs(face.surface->axis.z)>1.-1e-10)&&
                    std::abs(dot(face.surface->origin,face.surface->axis))<1e-8&&selected.emplace(face.owner_id,face.semantic_key).second)
                    shell.removed_faces.push_back(face);
                check(shell.removed_faces.size()==2,"Closed corner needs two native closing planes");
                opening_faces=shell.removed_faces;
                local.push_back({"corner-native-shell",shell});
                const auto start=std::chrono::steady_clock::now();
                auto wall=kernel.evaluate_history_incremental(local,calculated);
                std::cout<<"Native corner Shell thickness="<<thickness<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
                valid(wall.back());if(!shell_insert)return 0;
            }
        }
        auto part=document::PartDocument::create_default();static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Corner sheet"));
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        const auto half_span=solid_definition?60.:20.;
        auto line=sketcher::Sketch::create_default();line.owner_container_id=bend.id;static_cast<void>(line.add_segment(-half_span,0,half_span,0));
        bend.bend.sketch_id=line.id;bend.bend.angle_degrees=90.;bend.bend.radius_follows_thickness=false;bend.bend.radius=inner_radius;
        bend.bend.thickness_override=true;bend.bend.thickness=thickness;
        part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);part.history.push_back(bend);part.sketches.push_back(line);
        const auto frame=document::bend_material_definition(bend,line,document::sheet_metal_defaults(part));
        std::cout<<"Bend radial="<<frame.radial.x<<','<<frame.radial.y<<','<<frame.radial.z<<" tangent="<<frame.tangent.x<<','<<frame.tangent.y<<','<<frame.tangent.z<<std::endl;
        const auto bend_result=solid_definition?workspace::calculate_part_with_resolved_references(kernel,part):std::vector<kernel::BodyResult>{};
        for(bool second:{false,true}) {
            auto plate=document::PartDocument::create_sketch_container();plate.feature_kind=document::FeatureKind::Flat;
            plate.flat.thickness_override=true;plate.flat.thickness=thickness;plate.flat.direction=document::ExtrusionDirection::Forward;
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=plate.id;sketch.plane_auto=false;sketch.plane=sketcher::SketchPlane::XY;
            if(solid_definition) {
                const auto& geometry=bend_result.back().mesh.original_references;
                std::vector<document::ConstructionReference> references;
                for(const auto& edge:geometry.edges)try {
                    auto candidate=document::flat_sheet_references(edge);
                    if(candidate[1].semantic_key.starts_with(second?"sweep:cap:end:from:":"sweep:cap:start:from:")) {
                        references=std::move(candidate);break;
                    }
                }catch(const std::exception&){}
                check(!references.empty(),"Missing native outer Bend attachment edge");
                plate.flat.sheet_attachment=true;plate.flat.direction=document::ExtrusionDirection::Reverse;
                plate.placement.references=std::move(references);sketch.plane_auto=true;plate.flat.sketch_id=sketch.id;
                part.insert_history_entry(document::PartHistoryKind::Feature,plate.id);part.history.push_back(plate);part.sketches.push_back(sketch);
                part.resolve_constructions(geometry);
                std::cout<<"Attached Flat "<<second<<" origin "<<part.sketches.back().resolved_origin.x<<','<<part.sketches.back().resolved_origin.y<<','<<part.sketches.back().resolved_origin.z
                    <<" normal "<<part.sketches.back().resolved_normal.x<<','<<part.sketches.back().resolved_normal.y<<','<<part.sketches.back().resolved_normal.z
                    <<" y "<<part.sketches.back().resolved_y_axis.x<<','<<part.sketches.back().resolved_y_axis.y<<','<<part.sketches.back().resolved_y_axis.z<<std::endl;
                const auto& endpoints=part.sketches.back().external_references;
                check(endpoints.size()==2,"Attached Flat did not publish Bend endpoints");
                const auto a=endpoints[0].cached_points.front(),b=endpoints[1].cached_points.front();
                static_cast<void>(part.sketches.back().add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),50));
                continue;
            }
            sketch.resolved_normal=mul(second?frame.tangent:frame.radial,-1);sketch.resolved_x_axis=frame.along;
            sketch.resolved_y_axis=cross(sketch.resolved_normal,sketch.resolved_x_axis);
            sketch.resolved_origin=second?add(frame.origin,add(mul(frame.radial,-frame.radius),mul(frame.tangent,frame.radius))):frame.origin;
            // Persist the fixture's frame, rather than relying on cached Sketch
            // axes that reference regeneration correctly derives from Placement.
            plate.placement.x=sketch.resolved_origin.x;
            plate.placement.y=sketch.resolved_origin.y;
            plate.placement.z=sketch.resolved_origin.z;
            plate.placement.rotation_x=plate.placement.absolute_rotation_x=
                plate.placement.rotation_offset_x=second?180.:-90.;
            static_cast<void>(sketch.add_rectangle(-20,second?-20.:0.,20,second?0.:20.));plate.flat.sketch_id=sketch.id;
            part.insert_history_entry(document::PartHistoryKind::Feature,plate.id);part.history.push_back(plate);part.sketches.push_back(sketch);
        }
        const auto original=part.kernel_operations();const auto baseline=kernel.evaluate_history(original);valid(baseline.back());
        std::set<std::pair<std::string,std::string>> seen;unsigned accepted=0,rejected=0;
        std::map<bool,kernel::BodyResult> paired_results;
        for(const auto& face:baseline.back().mesh.triangle_references) {
            if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||
               (face.sheet_role!=kernel::SheetFaceRole::SideA&&face.sheet_role!=kernel::SheetFaceRole::SideB)||
               !seen.emplace(face.owner_id,face.semantic_key).second)continue;
            const auto normal=mul(face.surface->axis,face.surface->reversed?-1.:1.);
            const bool first=std::abs(dot(normal,frame.radial))>1.-1e-10;
            if(!first&&std::abs(dot(normal,frame.tangent))<1.-1e-10)continue;
            const auto level=dot(sub(face.surface->origin,frame.origin),first?frame.radial:frame.tangent);
            std::cout<<"Face normal="<<normal.x<<','<<normal.y<<','<<normal.z<<" level="<<level<<" origin="<<face.surface->origin.x<<','<<face.surface->origin.y<<','<<face.surface->origin.z<<std::endl;
            for(double other:{first?frame.radius:0.,first?frame.radius-frame.thickness:-frame.thickness})for(double direction:{-1.,1.}) {
                const auto position=add(frame.origin,add(mul(frame.radial,first?level:other),mul(frame.tangent,first?other:level)));
                auto request=document::sheet_form_request(source,face,position,normal,mul(frame.along,direction),thickness);
                if(body_cut&&!solid_definition) {
                    const auto& source_boundaries=source.calculated->back().body_boundaries.at(source.bodies[1]);
                    check(source_boundaries.size()==2&&source_boundaries.front().volume>0.,"Corner needs one closed solid before Surface conversion");
                    request.solid_cut_snapshot=closed_source;
                    request.surface_snapshot=std::make_shared<const kernel::BodyResult>(source_boundaries.back());
                    if(shell_insert)request.solid_opening_faces=opening_faces;
                    const bool inner=dot(normal,first?frame.radial:frame.tangent)<0.;
                    if(inner)request.source_origin=add(request.source_origin,mul(add(request.source_normal,cross(request.source_normal,request.source_x)),thickness));
                    else request.source_normal=mul(request.source_normal,-1.);
                }
                if(solid_definition&&dot(normal,first?frame.radial:frame.tangent)<0.){++rejected;continue;}
                kernel::HistoryOperation operation;operation.owner_id="corner-form";operation.primitive=request;operation.body=original.front().body;
                if(solid_definition) {
                    operation.boolean_tolerance=original.front().boolean_tolerance;
                    operation.mesh_deflection=original.front().mesh_deflection;
                }
                kernel::SheetMaterialDefinition material;material.kind=kernel::SheetMaterialDefinition::Kind::Form;material.owner_id=operation.owner_id;
                material.parent_owner_id=bend.id;material.origin=position;material.along=request.x_direction;material.radial=normal;material.tangent=cross(normal,request.x_direction);material.thickness=thickness;operation.sheet_material=material;
                auto history=original;history.push_back(operation);const auto start=std::chrono::steady_clock::now();
                std::cout<<"Try corner side "<<normal.x<<','<<normal.y<<','<<normal.z<<" other="<<other<<" along="<<direction<<std::endl;
                std::vector<kernel::BodyResult> result;
                try {result=kernel.evaluate_history_incremental(history,baseline);}
                catch(const std::exception& error){std::cout<<error.what()<<std::endl;++rejected;continue;}
                const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                std::cout<<"Corner side "<<normal.x<<','<<normal.y<<','<<normal.z<<" other="<<other<<" along="<<direction<<" seconds="<<seconds<<std::endl;
                if(!result.back().calculation_errors.empty()) {for(const auto& [owner,error]:result.back().calculation_errors)std::cout<<error<<std::endl;++rejected;continue;}
                valid(result.back());check(document::serialize_body_result(result.back()).value("sheet_cuts",nlohmann::json::array())==document::serialize_body_result(baseline.back()).value("sheet_cuts",nlohmann::json::array()),"Corner FORM entered Sheet Cut manufacturing records");++accepted;
                if(solid_definition)verify_corner_transition_radii(result.back(),operation.owner_id,thickness);
                if(body_cut) {
                    const auto previous=paired_results.find(first);
                    if(previous==paired_results.end())paired_results.emplace(first,result.back());
                    else {
                        std::cout<<"Paired volume "<<result.back().volume<<" vs "<<previous->second.volume<<" area "<<result.back().surface_area<<" vs "<<previous->second.surface_area<<std::endl;
                        near(result.back().volume,previous->second.volume);
                        near(result.back().surface_area,previous->second.surface_area);
                        TopoDS_Shape a,b;BRep_Builder builder;
                        std::istringstream a_data(result.back().kernel_shape),b_data(previous->second.kernel_shape);
                        BRepTools::Read(a,a_data,builder);BRepTools::Read(b,b_data,builder);
                        for(bool reversed:{false,true}) {
                            BRepAlgoAPI_Cut difference(reversed?a:b,reversed?b:a);difference.Build();
                            check(difference.IsDone(),"Inner/outer geometry comparison failed");
                            GProp_GProps properties;BRepGProp::VolumeProperties(difference.Shape(),properties);
                            near(properties.Mass(),0.);
                        }
                        std::cout<<"Inner/outer physical geometry equivalence passed\n";
                    }
                    if(first&&dot(normal,frame.radial)>0.&&thickness==1.&&inner_radius==2.) {
                        auto preview=part;const auto stock=workspace::calculate_part_with_resolved_references(kernel,preview);
                        preview.save("build/form-diagnostic/CornerBodyCutPreview.prtz",stock);
                        std::ofstream packet("build/form-diagnostic/CornerBodyCutPreview.json");packet<<document::serialize_body_result(result.back()).dump();packet.close();
                        check(bool(packet),"Diagnostic geometry packet could not be written");
                        if(!shell_insert) {
                        std::array<std::vector<kernel::EdgeReference>,2> transition;
                        for(const auto& edge:result.back().mesh.edges)if(edge.reference.valid()&&!edge.parameter_seam&&
                            std::ranges::any_of(edge.edge_treatment_side_references,[&](const auto& side){return side.owner_id==operation.owner_id;})) {
                            if(edge.edge_treatment_side_directions.size()!=2||edge.edge_treatment_side_directions[0].empty()||edge.edge_treatment_side_directions[1].empty())continue;
                            const auto a=unit(edge.edge_treatment_side_directions[0].front()),b=unit(edge.edge_treatment_side_directions[1].front());
                            if(std::abs(dot(a,b))>1.-1e-6)continue;
                            const auto stock=std::ranges::find_if(edge.edge_treatment_side_references,[&](const auto& side){return side.owner_id!=operation.owner_id;});
                            if(stock!=edge.edge_treatment_side_references.end()) {
                                if(stock->sheet_role==kernel::SheetFaceRole::ThicknessFace)continue;
                                transition[stock->sheet_role==kernel::SheetFaceRole::SideB?0:1].push_back(edge.reference);
                            }else {
                                const auto second_normal=cross(normal,request.x_direction);
                                const auto on_plane=[&](const auto& plane_normal) {
                                    return !edge.points.empty()&&std::ranges::all_of(edge.points,[&](const auto& point) {
                                        return std::abs(dot(sub(point,position),plane_normal))<1e-6;
                                    });
                                };
                                if(!on_plane(normal)&&!on_plane(second_normal))continue;
                                transition[1].push_back(edge.reference);
                            }
                            std::cout<<"Transition edge points="<<edge.points.size()<<" sides="<<edge.edge_treatment_side_references.size();
                            if(!edge.points.empty())std::cout<<" start="<<edge.points.front().x<<','<<edge.points.front().y<<','<<edge.points.front().z<<" end="<<edge.points.back().x<<','<<edge.points.back().y<<','<<edge.points.back().z;
                            for(const auto& side:edge.edge_treatment_side_references)std::cout<<" role="<<int(side.sheet_role)<<" form="<<(side.owner_id==operation.owner_id);
                            std::cout<<std::endl;
                        }
                        for(double radius:{thickness}) {
                            auto rounded_history=history;
                            const auto start=std::chrono::steady_clock::now();
                            try {
                                auto rounded=result;
                                for(unsigned side=0;side<2;++side) {
                                    kernel::HistoryOperation rounding;rounding.owner_id="corner-transition-fillet-"+std::to_string(side);rounding.body=operation.body;
                                    const auto blend_radius=side==0?radius:2.*radius;
                                    rounding.primitive=kernel::FilletRequest{transition[side],blend_radius};rounded_history.push_back(rounding);
                                    rounded=kernel.evaluate_history_incremental(rounded_history,rounded);valid(rounded.back());
                                    std::cout<<"Corner Fillet side="<<side<<" radius="<<blend_radius<<" edges="<<transition[side].size()<<" passed\n";
                                }
                                std::cout<<"Corner transition Fillet radius="<<radius<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" passed\n";
                            }catch(const std::exception& error){std::cout<<"Corner transition Fillet radius="<<radius<<" failed: "<<error.what()<<std::endl;}
                        }
                        }
                    }
                }
                if(!std::getenv("ZIMA_CPP_FORM_RANGE_ONLY")&&first&&((!body_cut&&thickness==1.&&inner_radius==2.)||solid_definition)) {
                    auto native=part;auto feature=document::create_sheet_form();feature.sheet_form=document::copy_sheet_form_definition(source);
                    feature.sheet_form.support=face;feature.sheet_form.thickness=thickness;
                    const auto other_normal=cross(normal,request.x_direction);
                    const auto other_face=std::ranges::find_if(baseline.back().mesh.triangle_references,[&](const auto& candidate) {
                        return candidate.surface&&candidate.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                            (candidate.sheet_role==kernel::SheetFaceRole::SideA||candidate.sheet_role==kernel::SheetFaceRole::SideB)&&
                            dot(mul(candidate.surface->axis,candidate.surface->reversed?-1.:1.),other_normal)>1.-1e-10&&
                            std::abs(dot(sub(position,candidate.surface->origin),other_normal))<1e-8;
                    });check(other_face!=baseline.back().mesh.triangle_references.end(),"Missing second native inner sheet face");
                    const auto origin=native.body_history.bodies().front().origin().id;
                    feature.placement.references={{{},face.owner_id,face.semantic_key,0.,true},
                        {{},other_face->owner_id,other_face->semantic_key,0.,true},{{},origin,"origin:point",0.,true}};
                    auto front=feature.placement.references.front();front.orientation_only=true;
                    front.orientation_drives_rotation=true;front.orientation_role="front";
                    feature.placement.references.push_back(front);
                    auto geometry=baseline.back().mesh.original_references;
                    const auto body_origins=native.body_origin_reference_geometry();
                    geometry.points.insert(geometry.points.end(),body_origins.points.begin(),body_origins.points.end());
                    check(document::resolve_sheet_form_feature_placement(feature.sheet_form,feature.placement,geometry),"Corner native reference placement failed");
                    near(feature.placement.x,position.x);near(feature.placement.y,position.y);near(feature.placement.z,position.z);
                    const kernel::Vec3 rotation{feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z};
                    near(dot(document::construction_direction_from_local_axis("x",rotation),request.x_direction),1.);
                    near(dot(document::construction_direction_from_local_axis("y",rotation),normal),1.);
                    native.insert_history_entry(document::PartHistoryKind::Feature,feature.id);native.history.push_back(feature);
                    const auto persisted=workspace::calculate_part_with_resolved_references(kernel,native);
                    std::cout<<"Persisted placement volume "<<persisted.back().volume<<" vs "<<result.back().volume<<std::endl;
                    valid(persisted.back());near(persisted.back().volume,result.back().volume);
                    if(solid_definition)verify_corner_transition_radii(persisted.back(),feature.id,thickness);
                    const auto path=std::filesystem::absolute("build/form-diagnostic/CornerGusset90Placed.prtz");native.save(path,persisted);
                    std::vector<kernel::BodyResult> reopened_result;auto reopened=document::PartDocument::load(path,&reopened_result);
                    check(reopened.history.back()==native.history.back(),"Corner native reopen changed placement or definition");
                    valid(reopened_result.back());near(reopened_result.back().volume,persisted.back().volume);
                    if(solid_definition)verify_corner_transition_radii(reopened_result.back(),feature.id,thickness);
                    const auto regenerated=workspace::calculate_part_with_resolved_references(kernel,reopened,&reopened_result);
                    valid(regenerated.back());near(regenerated.back().volume,persisted.back().volume);
                    if(solid_definition)verify_corner_transition_radii(regenerated.back(),feature.id,thickness);
                    std::cout<<"Corner native two-face placement, independent definition, save/reopen and regeneration passed\n";
                    if(solid_definition) {
                        auto wide=part;
                        const auto stock_ops=wide.kernel_operations();const auto stock_result=kernel.evaluate_history(stock_ops);
                        kernel::HistoryOperation stock_unfold{"stock-unfold",kernel::SheetStateRequest{true,true,{}}};stock_unfold.body=operation.body;
                        auto flat_stock_ops=stock_ops;flat_stock_ops.push_back(stock_unfold);
                        const auto flat_stock=kernel.evaluate_history(flat_stock_ops);valid(flat_stock.back());
                        for(bool mirror:{false,true}) {
                            auto placed=operation;auto posed=request;posed.position.x=-15.;placed.primitive=posed;
                            placed.sheet_material->origin=posed.position;
                            auto chain=stock_ops;chain.push_back(placed);
                            const auto single=kernel.evaluate_history(chain);valid(single.back());
                            std::cout<<"Shifted corner validated\n";
                            kernel::HistoryOperation copy;copy.owner_id=mirror?"corner-mirror":"corner-pattern";copy.body=operation.body;
                            kernel::BodyHistoryScope scope;scope.source_feature_id=placed.owner_id;
                            scope.combination=mirror?kernel::BodyCombination::Mirror:kernel::BodyCombination::Pattern;
                            scope.mirror_plane={{0,0,0},{1,0,0}};
                            scope.pattern.linear[0].local_axis=0;scope.pattern.linear[0].direction={1,0,0};
                            scope.pattern.linear[0].count=2;scope.pattern.linear[0].spacing=30.;copy.feature_copy=scope;chain.push_back(copy);
                            const auto copies=kernel.evaluate_history(chain);valid(copies.back());
                            std::cout<<"Rounded corner copies validated\n";
                            const auto tolerance=copies.back().surface_area*operation.boolean_tolerance;
                            near(copies.back().volume-stock_result.back().volume,
                                2.*(single.back().volume-stock_result.back().volume),tolerance);
                            kernel::HistoryOperation unfold{"corner-unfold",kernel::SheetStateRequest{true,true,{}}};unfold.body=operation.body;
                            chain.push_back(unfold);const auto flat=kernel.evaluate_history_incremental(chain,copies);valid(flat.back());
                            check(std::ranges::none_of(flat.back().mesh.axes,[](const auto& axis) {
                                return axis.reference.semantic_key.starts_with("centerline:from:centroid:form:")||
                                    axis.reference.semantic_key.starts_with("centerline:from:centroid:copy:");
                            }),"Unbend retained a spatial Form centroid axis");
                            std::cout<<"Corner Unbend validated\n";
                            near(flat.back().volume,flat_stock.back().volume,tolerance);
                            const auto symbols=kernel::sheet_material::form_symbol_edges(chain,kernel::sheet_material::regions_before(chain,chain.size()));
                            check(symbols.empty(),"Corner formed during bending retained a flat manufacturing symbol");
                            auto restore=unfold;restore.owner_id="corner-bend-back";restore.primitive=kernel::SheetStateRequest{false,true,{}};
                            chain.push_back(restore);const auto restored=kernel.evaluate_history_incremental(chain,flat);valid(restored.back());
                            near(restored.back().volume,copies.back().volume,tolerance);
                            for(const auto& axis:copies.back().mesh.axes)if(axis.reference.semantic_key.starts_with("centerline:from:centroid:"))
                                check(std::ranges::any_of(restored.back().mesh.axes,[&](const auto& value) {
                                    return value.reference==axis.reference&&value.point==axis.point&&
                                        value.direction==axis.direction&&value.display_length==axis.display_length;
                                }),
                                    "Bend Back changed Form centroid axis geometry or identity");
                            std::cout<<"Rounded corner "<<(mirror?"Mirror":"Pattern")<<", flat symbols, Unbend and Bend Back passed\n";
                            auto flat_chain=stock_ops;flat_chain.push_back(placed);flat_chain.push_back(unfold);
                            const auto single_flat=kernel.evaluate_history_incremental(flat_chain,single);
                            flat_chain.push_back(copy);const auto flat_copies=kernel.evaluate_history_incremental(flat_chain,single_flat);valid(flat_copies.back());
                            check(std::ranges::none_of(flat_copies.back().mesh.axes,[](const auto& axis) {
                                return axis.reference.semantic_key.starts_with("centerline:from:centroid:form:")||
                                    axis.reference.semantic_key.starts_with("centerline:from:centroid:copy:");
                            }),"Copy created after Unbend published a spatial axis");
                            flat_chain.push_back(restore);const auto folded_copies=kernel.evaluate_history_incremental(flat_chain,flat_copies);valid(folded_copies.back());
                            near(folded_copies.back().volume,copies.back().volume,tolerance);
                            for(const auto& axis:copies.back().mesh.axes)if(axis.reference.semantic_key.starts_with("centerline:from:centroid:")) {
                                const auto found=std::ranges::find(folded_copies.back().mesh.axes,axis.reference,&kernel::ViewerAxis::reference);
                                check(found!=folded_copies.back().mesh.axes.end(),"Copy created after Unbend lost its centroid-axis ancestry");
                                near(found->point.x,axis.point.x);near(found->point.y,axis.point.y);near(found->point.z,axis.point.z);
                                near(found->direction.x,axis.direction.x);near(found->direction.y,axis.direction.y);near(found->direction.z,axis.direction.z);
                                near(found->display_length,axis.display_length);
                            }
                            std::cout<<"Rounded corner copy after Unbend: geometry and folded centroid axes passed\n";
                        }
                    }
                }
            }
        }
        std::cout<<"Corner Bend thickness="<<thickness<<" inner radius="<<inner_radius<<" accepted="<<accepted<<" rejected="<<rejected<<std::endl;
        if(std::getenv("ZIMA_CPP_FORM_EXPECT_REJECTION"))
            check(solid_definition&&accepted==0&&rejected==16,"Unsupported corner unexpectedly accepted geometry");
        else check(accepted==((body_cut&&!solid_definition)?4:2)&&rejected==((body_cut&&!solid_definition)?12:14),"Corner FORM side placement matrix failed");return 0;
    }
    if((argc==4||argc==5)&&std::string_view(argv[1])=="--prepare-corner-definition") {
        std::vector<kernel::BodyResult> saved;
        auto part=document::PartDocument::load(argv[2],&saved);
        const auto body=std::ranges::find(part.body_history.bodies(),"FORM_CUT",&document::BodyHistory::name);
        check(body!=part.body_history.bodies().end()&&body->entries.size()==2,"Corner definition needs two cutting Sketches");
        for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)==&*body) {
            std::vector<std::string> endpoints;for(const auto& spline:sketch.bsplines) {
                endpoints.push_back(spline.control_point_ids.front());endpoints.push_back(spline.control_point_ids.back());
            }
            for(const auto& endpoint:endpoints) {
                const auto p=*sketch.find_point(endpoint);std::vector<std::string> matches;
                for(const auto& segment:sketch.segments)for(const auto& id:{segment.first_point_id,segment.second_point_id}) {
                    const auto* q=sketch.find_point(id);if(id!=endpoint&&std::hypot(p.x-q->x,p.y-q->y)<=1e-9)matches.push_back(id);
                }
                for(const auto& id:matches)if(sketch.find_point(id))static_cast<void>(sketch.merge_points(endpoint,id));
            }
            sketch.validate();
        }
        const auto cut=std::ranges::find_if(part.sketches,[&](const auto& s){return s.plane==sketcher::SketchPlane::XY&&part.body_history.owner(s.owner_container_id)==&*body;});
        check(cut!=part.sketches.end(),"Corner definition has no XY cutting Sketch");const auto primary=*cut;
        if(argc==5) {
            const double factor=std::stod(argv[4]);check(factor>.5&&factor<1.,"Invalid corner profile inset factor");
            for(auto& sketch:part.sketches)if(part.body_history.owner(sketch.owner_container_id)==&*body) {
                // Keep the exact projected sources as construction context;
                // the explicitly authored cutting contour needs a joining lip.
                sketch.import_blocks.clear();
                for(auto& point:sketch.points){point.x*=factor;point.y*=factor;}
                sketch.validate();
            }
        }
        check(std::ranges::none_of(part.body_history.bodies(),[](const auto& b){return b.name=="FORM_FLAT"||b.name=="FORM_SYMBOL";}),"Corner roles already exist");
        static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"FORM_FLAT"));
        const auto symbol_body=document::create_origin_bound_body(part.body_history,part.document_id,"FORM_SYMBOL");
        auto feature=document::PartDocument::create_sketch_container();auto symbol=sketcher::Sketch::create_default();
        symbol.owner_container_id=feature.id;symbol.name="Corner gusset symbol";feature.name=symbol.name;
        for(const auto& source:primary.external_references)if(source.kind==sketcher::ExternalReferenceKind::Edge) {
            auto reference=source;reference.id=kernel::make_stable_id();symbol.add_external_reference(reference);
            static_cast<void>(symbol.add_external_profile_geometry(reference.id));
        }
        for(const auto& segment:primary.segments) {
            const auto* a=primary.find_point(segment.first_point_id);const auto* b=primary.find_point(segment.second_point_id);
            if(std::ranges::none_of(symbol.segments,[&](const auto& line) {
                const auto* p=symbol.find_point(line.first_point_id);const auto* q=symbol.find_point(line.second_point_id);
                return (std::hypot(p->x-a->x,p->y-a->y)<1e-9&&std::hypot(q->x-b->x,q->y-b->y)<1e-9)||
                       (std::hypot(p->x-b->x,p->y-b->y)<1e-9&&std::hypot(q->x-a->x,q->y-a->y)<1e-9);
            }))static_cast<void>(symbol.add_segment(a->x,a->y,b->x,b->y,1e-9));
        }
        check(!symbol.segments.empty()&&!symbol.bsplines.empty(),"Corner symbol has no exact boundary geometry");
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);part.sketches.push_back(symbol);
        part.name="Corner gusset 90 degrees";
        kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part,&saved);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Corner native definition failed regeneration");
        const auto definition=document::sheet_form_definition(part,calculated);
        check(definition.cut_sketches.size()==2&&definition.flat_sketch.empty(),"Corner role classification failed");
        const auto request=document::sheet_form_request(definition,{}, {},{0,0,1},{1,0,0},1.);
        check(request.surface_snapshot&&request.symbol_edges.size()==primary.segments.size()+primary.bsplines.size(),"Corner lost its exact surface or symbol");
        const auto original=part.serialized(calculated);part.save(argv[3],calculated);
        const auto reopened=document::read_sheet_form_definition(argv[3]);
        check(reopened.part.serialized(*reopened.calculated)==original,"Corner native save/reopen changed geometry");
        std::cout<<"Corner definition prepared: two perpendicular profiles, exact projected symbol, empty FORM_FLAT and unchanged original topology\n";return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--verify-actual-part") {
        std::vector<kernel::BodyResult> saved;auto actual=document::PartDocument::load(argv[2],&saved);
        for(const auto& feature:actual.history)std::cout<<"Feature "<<feature.id<<" "<<feature.name<<" kind="<<int(feature.feature_kind)<<'\n';
        kernel::OcctKernel actual_kernel;
        const auto calculated=workspace::calculate_part_with_resolved_references(actual_kernel,actual,&saved);
        valid(calculated.back());
        const auto& last=actual.history.back();
        if(last.extrusion.sheet_cut)check(std::ranges::any_of(calculated.back().sheet_cuts,[&](const auto& cut){return cut.cut_owner==last.id;}),
            "Actual Sheet Cut lost its native material-space ancestry");
        std::vector<kernel::BodyResult> reopened;
        const auto roundtrip=document::PartDocument::from_serialized(actual.serialized(calculated),&reopened);
        check(roundtrip.history==actual.history&&roundtrip.sketches.size()==actual.sketches.size(),"Actual Part roundtrip changed authored feature definitions");
        valid(reopened.back());near(reopened.back().volume,calculated.back().volume);
        check(document::serialize_body_result(reopened.back()).at("sheet_cuts")==document::serialize_body_result(calculated.back()).at("sheet_cuts"),
            "Actual Part roundtrip changed native cut region identities");
        std::cout<<"Actual Part calculated without errors and passed native roundtrip, volume="<<calculated.back().volume<<'\n';return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--prepare-definition-xy") {
        const auto path=std::filesystem::path(argv[2]);
        const auto before=document::read_sheet_form_definition(path);auto part=before.part;
        unsigned changed=0,rotated_manual=0;
        for(auto& sketch:part.sketches) {
            auto* owner=part.find_container(sketch.owner_container_id);
            if(owner&&sketch.plane_auto&&sketch.plane==sketcher::SketchPlane::XZ&&
               document::placement_references_use_whole_origin(owner->placement.references)) {
                sketch.plane=sketcher::SketchPlane::XY;++changed;
            } else if(owner&&!sketch.plane_auto&&sketch.plane==sketcher::SketchPlane::YZ&&
                      document::placement_references_use_whole_origin(owner->placement.references)) {
                near(owner->placement.rotation_x,0.);near(owner->placement.rotation_y,0.);near(owner->placement.rotation_z,0.);
                owner->placement.rotation_offset_x=90.;owner->placement.absolute_rotation_x=90.;
                owner->placement.rotation_x=90.;++rotated_manual;
            }
        }
        check(changed==3,"FORM XY preparation expected three whole-Origin Sketches");
        check(rotated_manual==1,"FORM XY preparation expected one manual cutting profile");
        kernel::OcctKernel kernel;
        const auto boundaries=workspace::calculate_part_with_resolved_references(kernel,part,before.calculated.get());
        if(!boundaries.empty())for(const auto& [owner,error]:boundaries.back().calculation_errors)
            std::cerr<<owner<<": "<<error<<'\n';
        check(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"FORM XY source calculation failed");
        const auto after=document::sheet_form_definition(part,boundaries);
        check(before.bodies==after.bodies&&before.cut_sketch==after.cut_sketch&&before.surface==after.surface,
            "FORM XY preparation changed source identities");
        const auto old=document::sheet_form_request(before,{}, {},{0,1,0},{1,0,0},1.);
        const auto next=document::sheet_form_request(after,{}, {},{0,1,0},{1,0,0},1.);
        check(old.surface_snapshot&&next.surface_snapshot,"FORM XY preparation lost the source shell cache");
        near(next.source_normal.z,1.);near(next.source_normal.x,0.);near(next.source_normal.y,0.);
        near(old.surface_snapshot->surface_area,next.surface_snapshot->surface_area,1e-5);
        const auto shape=[](const kernel::BodyResult& body) {
            TopoDS_Shape result;BRep_Builder builder;std::istringstream input(body.kernel_shape);
            BRepTools::Read(result,input,builder);check(!result.IsNull(),"FORM comparison lost its native shell");return result;
        };
        gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),std::numbers::pi/2.);
        const auto rotated=BRepBuilderAPI_Transform(shape(*old.surface_snapshot),rotation,true).Shape();
        const auto rebuilt=shape(*next.surface_snapshot);
        const auto on_shell=[](kernel::Vec3 point,const TopoDS_Shape& shell) {
            const auto vertex=BRepBuilderAPI_MakeVertex(gp_Pnt(point.x,point.y,point.z)).Shape();
            BRepExtrema_DistShapeShape distance(vertex,shell);
            check(distance.IsDone()&&distance.Value()<1e-5,"FORM XY preparation changed the authored native shell");
        };
        for(const auto point:old.surface_snapshot->mesh.vertices)on_shell({point.x,-point.z,point.y},rebuilt);
        for(const auto point:next.surface_snapshot->mesh.vertices)on_shell(point,rotated);
        part.save(path,boundaries);
        const auto reopened=document::read_sheet_form_definition(path);
        const auto cut=std::ranges::find(reopened.part.sketches,reopened.cut_sketch,&sketcher::Sketch::id);
        check(cut!=reopened.part.sketches.end()&&cut->plane==sketcher::SketchPlane::XY&&cut->corner_radii.size()==2,
            "FORM XY native reload lost the cutting plane or R15 corners");
        std::cout<<"FORM XY: three Origin-bound Sketches, retained face-bound profile, unchanged IDs, independently compared rotated shell, save/reopen PASS\n";
        return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--prepare-definition") {
        const auto path=std::filesystem::path("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
        auto part=document::PartDocument::load(path);kernel::OcctKernel kernel;
        const auto boundaries=kernel.evaluate_history(part.kernel_operations());
        check(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"FORM source preparation failed");
        part.save(path,boundaries);
        const auto source=document::read_sheet_form_definition(path);
        const auto request=document::sheet_form_request(source,{}, {},{0,0,1},{1,0,0},1);
        check(static_cast<bool>(request.surface_snapshot),"Prepared FORM did not retain its native surface bindings");
        std::cout<<"Native FORM definition prepared"<<std::endl;return 0;
    }
    const auto begin=std::chrono::steady_clock::now();
    const auto elapsed=[](const auto start){return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    const auto source=document::read_sheet_form_definition("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
    auto cut_after_shape=source.part;auto role_graph=cut_after_shape.body_history;
    role_graph.move_body(source.bodies[1],0);
    cut_after_shape.set_body_history(std::move(role_graph));
    const auto reordered_source=document::sheet_form_definition(cut_after_shape,*source.calculated);
    const auto authored=source.part.serialized();
    const auto cut=std::ranges::find(source.part.sketches,source.cut_sketch,&sketcher::Sketch::id);
    check(cut!=source.part.sketches.end()&&cut->corner_radii.size()==2,
        "Ventilation Window cutting Sketch must contain two rounded corners");
    for(const auto& corner:cut->corner_radii) {
        near(corner.radius,15.);
        const auto vertex=std::ranges::find(cut->points,corner.vertex_id,&sketcher::SketchPoint::id);
        check(vertex!=cut->points.end(),"Window corner lost its source vertex");
        near(vertex->y,-20.);near(std::abs(vertex->x),50.);
    }
    auto part=document::PartDocument::create_default();
    static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Sheet"));
    auto stock=document::PartDocument::create_sketch_container();stock.feature_kind=document::FeatureKind::Flat;
    stock.flat.direction=document::ExtrusionDirection::Reverse;stock.flat.thickness_override=true;stock.flat.thickness=1;
    auto outline=sketcher::Sketch::create_default();outline.owner_container_id=stock.id;
    static_cast<void>(outline.add_rectangle(-100,-100,100,100));stock.flat.sketch_id=outline.id;
    part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);part.history.push_back(stock);part.sketches.push_back(outline);
    const auto original=part.kernel_operations();kernel::OcctKernel kernel;
    const auto baseline=kernel.evaluate_history(original);near(baseline.back().volume,40000.);
    const auto face=std::ranges::find_if(baseline.back().mesh.triangle_references,[](const auto& face){return
        (face.sheet_role==kernel::SheetFaceRole::SideA||face.sheet_role==kernel::SheetFaceRole::SideB)&&face.surface&&
        face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(face.surface->origin.z)<1e-9;});
    check(face!=baseline.back().mesh.triangle_references.end(),"Flat has no native outer face");
    std::cout<<"Support: "<<face->semantic_key<<" origin="<<face->surface->origin.x<<","<<face->surface->origin.y<<","<<face->surface->origin.z
        <<" axis="<<face->surface->axis.x<<","<<face->surface->axis.y<<","<<face->surface->axis.z<<" reversed="<<face->surface->reversed<<std::endl;
    const auto normal=kernel::sheet_material::mul(face->surface->axis,face->surface->reversed?-1.:1.);
    const auto position=kernel::sheet_material::mul(normal,kernel::sheet_material::dot(face->surface->origin,normal));
    const auto reference_geometry=part.construction_reference_geometry_for(stock.id,baseline.back().mesh.original_references);
    const auto* stock_body=part.body_owner_for_object(stock.id);
    check(stock_body!=nullptr,"FORM stock has no owning Body");
    const auto origin=stock_body->origin().id;
    std::set<std::string> tested_sides;
    for(const auto& side:baseline.back().mesh.triangle_references) {
        if(!side.surface||side.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||
           (side.sheet_role!=kernel::SheetFaceRole::SideA&&side.sheet_role!=kernel::SheetFaceRole::SideB)||
           !tested_sides.insert(side.semantic_key).second)continue;
        const auto outward=kernel::sheet_material::mul(side.surface->axis,side.surface->reversed?-1.:1.);
        const auto seed_point=kernel::sheet_material::add(kernel::Vec3{4,7,0},
            kernel::sheet_material::mul(outward,kernel::sheet_material::dot(side.surface->origin,outward)));
        const auto zero=document::sheet_form_attachment(side,origin,reference_geometry,{},seed_point);
        const auto zero_tangent=document::construction_direction_from_local_axis("x",
            {zero.rotation_x,zero.rotation_y,zero.rotation_z});
        const auto zero_z=document::construction_direction_from_local_axis("z",
            {zero.rotation_x,zero.rotation_y,zero.rotation_z});
        auto positioning_geometry=reference_geometry;
        kernel::ViewerPoint point;point.position={12,17,9};point.reference={"native-position-point","position"};
        positioning_geometry.points.push_back(point);
        kernel::ViewerEdge line;line.reference={"native-position-line","segment"};
        const auto anchor=kernel::sheet_material::add(side.surface->origin,
            kernel::sheet_material::add(kernel::sheet_material::mul(zero_tangent,21),kernel::sheet_material::mul(zero_z,13)));
        line.points={anchor,kernel::sheet_material::add(anchor,kernel::sheet_material::mul(zero_z,20))};
        positioning_geometry.edges.push_back(line);
        auto positioning_face=std::make_shared<kernel::SurfaceGeometry>();
        positioning_face->kind=kernel::SurfaceGeometry::Kind::Plane;
        positioning_face->origin=anchor;positioning_face->axis=zero_tangent;
        positioning_geometry.triangle_references.push_back({"native-position-face","face",{},positioning_face});
        check(document::sheet_form_position_reference_available({{},point.reference.owner_id,point.reference.semantic_key},positioning_geometry,outward)&&
            document::sheet_form_position_reference_available({{},line.reference.owner_id,line.reference.semantic_key},positioning_geometry,outward)&&
            document::sheet_form_position_reference_available({{},"native-position-face","face"},positioning_geometry,outward),
            "Form positioning reference kinds unavailable");
        auto curved=line;curved.reference.owner_id="curved-position-line";
        curved.points.insert(curved.points.begin()+1,kernel::sheet_material::add(anchor,zero_tangent));
        positioning_geometry.edges.push_back(curved);
        check(!document::sheet_form_position_reference_available({{},curved.reference.owner_id,curved.reference.semantic_key},positioning_geometry,outward),
            "Form offered a curved positioning segment");
        for(double offset:{0.,3.,-4.,-0.}) {
            auto posed=zero;
            posed.references[1]={"",point.reference.owner_id,point.reference.semantic_key,offset,true};
            posed.references[2]={"",point.reference.owner_id,point.reference.semantic_key,-2*offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM two point coordinates failed");
            const kernel::Vec3 result{posed.x,posed.y,posed.z};
            near(kernel::sheet_material::dot(result,zero_tangent),kernel::sheet_material::dot(point.position,zero_tangent)+offset);
            near(kernel::sheet_material::dot(result,zero_z),kernel::sheet_material::dot(point.position,zero_z)-2*offset);
            near(kernel::sheet_material::dot(kernel::sheet_material::sub(result,side.surface->origin),outward),0);
            auto serialized=document::create_sheet_form();serialized.placement=posed;
            serialized.sheet_form=document::copy_sheet_form_definition(source);serialized.sheet_form.support=side;
            auto point_part=part;point_part.insert_history_entry(document::PartHistoryKind::Feature,serialized.id);
            point_part.history.push_back(serialized);
            const auto reopened=document::PartDocument::from_serialized(point_part.serialized());
            check(std::signbit(reopened.history.back().placement.references[1].offset)==std::signbit(offset)&&
                std::signbit(reopened.history.back().placement.references[2].offset)==std::signbit(-2*offset),
                "Form persistence lost the authored signed zero offset");
            posed.references[1]={"","native-position-face","face",offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"Form planar face and point positioning failed");
            near(kernel::sheet_material::dot(kernel::sheet_material::sub({posed.x,posed.y,posed.z},anchor),zero_tangent),offset);
            posed.references[1]={"",line.reference.owner_id,line.reference.semantic_key,offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM line and point coordinates failed");
            const auto distance_normal=kernel::sheet_material::cross(outward,zero_z);
            near(kernel::sheet_material::dot(kernel::sheet_material::sub({posed.x,posed.y,posed.z},anchor),distance_normal),offset);
            const auto before=posed;posed.references[2]=posed.references[1];
            check(!document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM accepted dependent positioning lines");
            near(posed.x,before.x);near(posed.y,before.y);near(posed.z,before.z);
        }
        for(double angle:{0.,30.,-70.}) {
            document::Placement seed;seed.absolute_rotation_y=angle;
            auto attached=document::sheet_form_attachment(side,origin,reference_geometry,seed,seed_point);
            check(document::resolve_sheet_form_placement(attached,reference_geometry),"FORM native plane positioning failed");
            near(attached.x,seed_point.x);near(attached.y,seed_point.y);near(attached.z,seed_point.z);
            check(attached.references.size()==4,"FORM did not retain three position references and FRONT");
            check(document::point_constraint_remaining_dof(attached.references,reference_geometry,seed_point)==0,
                "FORM insertion point is not fixed by native references");
            const auto direction=document::construction_direction_from_local_axis("y",
                {attached.rotation_x,attached.rotation_y,attached.rotation_z});
            near(kernel::sheet_material::dot(direction,outward),1.);
            const auto tangent=document::construction_direction_from_local_axis("x",
                {attached.rotation_x,attached.rotation_y,attached.rotation_z});
            const auto radians=angle*std::numbers::pi/180.;
            const auto expected=kernel::sheet_material::add(kernel::sheet_material::mul(zero_tangent,std::cos(radians)),
                kernel::sheet_material::mul(kernel::sheet_material::cross(outward,zero_tangent),std::sin(radians)));
            near(kernel::sheet_material::dot(tangent,expected),1.);
            auto feature=document::create_sheet_form();feature.placement=attached;
            auto persisted=part;persisted.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
            feature.sheet_form=document::copy_sheet_form_definition(source);feature.sheet_form.support=side;
            persisted.history.push_back(feature);
            const auto reopened=document::PartDocument::from_serialized(persisted.serialized());
            check(reopened.history.back().placement==attached,"FORM attachment lost its native side or position references");
        }
    }
    check(tested_sides.size()==2,"FORM attachment test did not cover both sheet sides");
    {
        TopoDS_Shape solid;BRep_Builder builder;std::istringstream data(baseline.back().kernel_shape);BRepTools::Read(solid,data,builder);
        for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next()) {
            const auto actual=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(actual);
            if(surface.GetType()!=GeomAbs_Plane)continue;
            const auto plane=surface.Plane();if(plane.Distance(gp_Pnt(position.x,position.y,position.z))>1e-7)continue;
            auto outward=plane.Axis().Direction();if(!plane.Position().Direct())outward.Reverse();if(actual.Orientation()==TopAbs_REVERSED)outward.Reverse();
            BRepClass_FaceClassifier classifier(actual,gp_Pnt(position.x,position.y,position.z),1e-7);
            std::cout<<"Native support n="<<outward.X()<<","<<outward.Y()<<","<<outward.Z()<<" state="<<classifier.State()<<std::endl;
        }
    }
    auto start=std::chrono::steady_clock::now();
    auto request=document::sheet_form_request(source,*face,position,normal,face->surface->radial,1);
    check(static_cast<bool>(request.surface_snapshot),"FORM did not consume its calculated surface");
    auto changed_source=source;
    auto* source_shell=changed_source.part.find_container(
        changed_source.part.body_history.find(changed_source.bodies[1])->entries.back().id);
    check(source_shell&&source_shell->feature_kind==document::FeatureKind::Shell,"FORM fixture has no source shell");
    source_shell->shell.thickness=.25;
    check(!document::sheet_form_request(changed_source,*face,position,normal,face->surface->radial,1).surface_snapshot,
        "Changing FORM geometry reused a stale calculated shell");
    std::cout<<"FORM definition preparation="<<elapsed(start)<<" s"<<std::endl;
    kernel::HistoryOperation form{"inserted-form",request};form.body=original.front().body;
    {
        auto changed_symbol=source;
        auto symbol=std::ranges::find(changed_symbol.part.sketches,changed_symbol.symbol_sketch,&sketcher::Sketch::id);
        check(symbol!=changed_symbol.part.sketches.end(),"FORM fixture lost its manufacturing symbol");
        static_cast<void>(symbol->add_segment(1,2,3,4));
        auto changed=form;changed.primitive=document::sheet_form_request(changed_symbol,*face,position,normal,face->surface->radial,1);
        check(std::get<kernel::SheetFormRequest>(changed.primitive).symbol_edges.size()==request.symbol_edges.size()+1,
            "FORM symbol change reused stale display geometry");
        check(kernel::history_fingerprint({form},1)!=kernel::history_fingerprint({changed},1),
            "FORM symbol change did not invalidate the cached calculation");
    }
    kernel::SheetMaterialDefinition material;material.kind=kernel::SheetMaterialDefinition::Kind::Form;
    material.owner_id=form.owner_id;material.parent_owner_id=face->sheet_owner;material.thickness=1;
    material.origin=request.position;material.along=request.x_direction;material.radial=request.normal;
    material.tangent=kernel::sheet_material::cross(material.radial,material.along);form.sheet_material=material;
    auto history=original;history.push_back(form);
    if(argc==3&&std::string_view(argv[1])=="--angle") {
        const auto degrees=std::stod(argv[2]);const auto radians=degrees*std::numbers::pi/180.;
        request.x_direction=kernel::sheet_material::add(kernel::sheet_material::mul(request.x_direction,std::cos(radians)),
            kernel::sheet_material::mul(kernel::sheet_material::cross(normal,request.x_direction),std::sin(radians)));
        history.back().primitive=request;start=std::chrono::steady_clock::now();
        const auto rotated=kernel.evaluate_history_incremental(history,baseline);
        std::cout<<"FORM angle="<<degrees<<" insertion="<<elapsed(start)<<" s"<<std::endl;
        valid(rotated.back());return 0;
    }
    start=std::chrono::steady_clock::now();const auto result=kernel.evaluate_history(history);
    std::cout<<"FORM cold insertion="<<elapsed(start)<<" s"<<std::endl;valid(result.back());
    check(result.back().volume!=baseline.back().volume,"FORM left the spatial sheet unchanged");
    if(argc==2&&(std::string_view(argv[1])=="--copy-matrix"||std::string_view(argv[1])=="--copy-after-unbend")) {
        const bool after_unbend=std::string_view(argv[1])=="--copy-after-unbend";
        auto large=part;for(auto& p:large.sketches.front().points){p.x*=3;p.y*=3;}
        const auto stock_ops=large.kernel_operations();const auto stock_result=kernel.evaluate_history(stock_ops);
        auto source_form=form;auto source_request=request;source_request.position.y=-80;
        source_form.primitive=source_request;source_form.sheet_material->origin=source_request.position;
        for(int scenario=after_unbend?6:0;scenario<(after_unbend?7:6);++scenario) {
            auto chain=stock_ops;chain.push_back(source_form);
            const auto single=kernel.evaluate_history(chain);valid(single.back());
            const auto single_volume=single.back().volume-stock_result.back().volume;
            if(after_unbend) {
                kernel::HistoryOperation flatten{"flatten-before-copy",kernel::SheetStateRequest{true,true,{}}};
                flatten.body=form.body;chain.push_back(flatten);
            }
            kernel::HistoryOperation copied;copied.owner_id="form-copy-"+std::to_string(scenario);copied.body=form.body;
            kernel::BodyHistoryScope copy;copy.source_feature_id=form.owner_id;
            copy.combination=scenario<2?kernel::BodyCombination::Mirror:kernel::BodyCombination::Pattern;
            copy.mirror_plane={{0,0,0},{0,scenario==1?-1.:1.,0}};
            auto& p=copy.pattern;p.axis={0,0,1};p.origin={};
            if(scenario==2) {p.circular=true;p.count=2;p.angle_degrees=180;p.full_circle=false;}
            else if(scenario>=3) {p.circular=false;p.linear[0].local_axis=1;p.linear[0].direction={0,1,0};p.linear[0].count=2;p.linear[0].spacing=160;
                if(scenario==4){p.linear[1].local_axis=0;p.linear[1].direction={1,0,0};p.linear[1].count=2;p.linear[1].spacing=150;}}
            copied.feature_copy=copy;chain.push_back(copied);
            if(scenario==5) {auto nested=copied;nested.owner_id="nested-form-mirror";nested.feature_copy->source_feature_id=copied.owner_id;
                nested.feature_copy->combination=kernel::BodyCombination::Mirror;nested.feature_copy->mirror_plane={{75,0,0},{1,0,0}};chain.push_back(nested);}
            const auto spatial=kernel.evaluate_history(chain);valid(spatial.back());
            const auto state=kernel::sheet_material::regions_before(chain,chain.size());
            const auto count=std::ranges::count_if(state.regions,[](const auto& r){return r.kind==kernel::SheetMaterialDefinition::Kind::Form;});
            check(count==(scenario==4?4:scenario==5?3:2),"FORM copies lost material instances");
            // Boolean trimming changes the rational integration domains. Bound
            // the independent additive-volume comparison by the actual BRep
            // surface area and shape tolerance, not an arbitrary absolute mass.
            // valid() separately checks the persisted volume against GK.
            TopoDS_Shape copy_shape;BRep_Builder builder;std::istringstream copy_data(spatial.back().kernel_shape);
            BRepTools::Read(copy_shape,copy_data,builder);GProp_GProps area;
            BRepGProp::SurfaceProperties(copy_shape,area);double tolerance=1e-7;
            for(TopExp_Explorer edge(copy_shape,TopAbs_EDGE);edge.More();edge.Next())
                tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(edge.Current())));
            near(spatial.back().volume-stock_result.back().volume,after_unbend?0:single_volume*count,area.Mass()*tolerance);
            kernel::HistoryOperation unfold{"flat-copies",kernel::SheetStateRequest{true,true,{}}};unfold.body=form.body;
            if(!after_unbend)chain.push_back(unfold);
            const auto developed=kernel.evaluate_history_incremental(chain,spatial);valid(developed.back());near(developed.back().volume,stock_result.back().volume);
            const auto symbols=kernel::sheet_material::form_symbol_edges(chain,kernel::sheet_material::regions_before(chain,chain.size()));
            check(symbols.size()==request.symbol_edges.size()*count,"FORM copied flat symbols lost strokes");
            check(std::ranges::count_if(developed.back().mesh.edges,[](const auto& e){return e.reference.semantic_key.starts_with("form:symbol:");})==symbols.size(),
                "FORM flat viewer dropped manufacturing symbol strokes");
            check(std::ranges::none_of(developed.back().mesh.original_references.edges,[](const auto& e){return e.reference.semantic_key.starts_with("form:symbol:");}),
                "FORM symbol became a placement-reference owner");
            const auto reopened=document::load_body_result(document::serialize_body_result(developed.back()));
            kernel::ViewerReferenceGeometry before_edges,after_edges;
            before_edges.edges=developed.back().mesh.edges;after_edges.edges=reopened.mesh.edges;
            check(document::serialize_viewer_reference_geometry(before_edges)==document::serialize_viewer_reference_geometry(after_edges),
                "Native reopen changed FORM symbol identities or geometry");
            auto back=unfold;back.owner_id="restore-copies";back.primitive=kernel::SheetStateRequest{false,true,{}};chain.push_back(back);
            const auto restored_copies=kernel.evaluate_history_incremental(chain,developed);valid(restored_copies.back());
            near(restored_copies.back().volume,after_unbend?stock_result.back().volume+single_volume*count:spatial.back().volume,
                after_unbend?area.Mass()*tolerance:1e-3);
            check(std::ranges::none_of(restored_copies.back().mesh.edges,[](const auto& e){return e.reference.semantic_key.starts_with("form:symbol:");}),
                "Restored FORM retained flat-only symbol strokes");
            std::cout<<"FORM copy scenario "<<scenario<<": "<<count<<" spatial/flat/restored instances passed\n";
        }
        return 0;
    }
    {
        auto reordered_history=original;auto reordered_form=form;
        reordered_form.primitive=document::sheet_form_request(reordered_source,*face,position,normal,face->surface->radial,1.);
        reordered_history.push_back(reordered_form);
        const auto reordered_result=kernel.evaluate_history_incremental(reordered_history,baseline);
        valid(reordered_result.back());near(reordered_result.back().volume,result.back().volume);
        auto before=document::serialize_viewer_reference_geometry(result.back().mesh.original_references);
        auto after=document::serialize_viewer_reference_geometry(reordered_result.back().mesh.original_references);
        // Restoring the input BRep may reorder its display triangulation.
        // Compare actual sample positions independently of vertex indexing,
        // and keep every identity, analytical surface and exact edge check.
        const auto positions=[](const auto& geometry) {
            std::set<std::array<double,3>> values;
            for(const auto& p:geometry.vertices)values.insert({p.x,p.y,p.z});
            return values;
        };
        check(positions(reordered_result.back().mesh.original_references)==positions(result.back().mesh.original_references),
            "FORM_CUT after FORM changed original reference sample geometry");
        for(const auto* key:{"vertices_binary","triangles_binary"}) {before.erase(key);after.erase(key);}
        check(after==before,
            "FORM_CUT after FORM changed native result reference geometry");
        std::cout<<"FORM_CUT after FORM produced equivalent valid spatial geometry\n";
    }
    check(std::ranges::any_of(result.back().mesh.triangle_references,[&](const auto& face){return face.owner_id==form.owner_id&&face.sheet_role==kernel::SheetFaceRole::SideA;}),"FORM lost its outside skin identity");
    std::cout<<"Spatial FORM volume="<<result.back().volume<<std::endl;
    kernel::HistoryOperation flat{"unbend-form",kernel::SheetStateRequest{true,false,{form.owner_id}}};flat.body=form.body;
    history.push_back(flat);start=std::chrono::steady_clock::now();const auto unfolded=kernel.evaluate_history_incremental(history,result);
    std::cout<<"FORM flat calculation="<<elapsed(start)<<" s"<<std::endl;valid(unfolded.back());
    near(unfolded.back().volume,baseline.back().volume);
    check(std::ranges::none_of(unfolded.back().mesh.triangle_references,[&](const auto& face){return face.owner_id==form.owner_id;}),"Empty FORM_FLAT left a spatial forming face");
    kernel::HistoryOperation restore{"restore-form",kernel::SheetStateRequest{false,false,{form.owner_id}}};restore.body=form.body;
    history.push_back(restore);start=std::chrono::steady_clock::now();const auto restored=kernel.evaluate_history_incremental(history,unfolded);
    std::cout<<"FORM shape restoration="<<elapsed(start)<<" s"<<std::endl;valid(restored.back());
    near(restored.back().volume,result.back().volume);
    auto cut_history=original;cut_history.push_back(form);
    auto cut_sketch=sketcher::Sketch::create_default();
    auto opening=document::PartDocument::create_extrusion_container(cut_sketch.id);opening.extrusion.sheet_cut=true;
    cut_sketch.owner_container_id=opening.id;
    static_cast<void>(cut_sketch.add_rectangle(-5,-5,5,5));opening.extrusion.sketch_id=cut_sketch.id;
    opening.extrusion.height=10;opening.extrusion.length_forward=10;
    opening.extrusion.extent_mode=document::ProfileExtentMode::OneSide;opening.combine_mode=document::CombineMode::Subtract;
    auto cut_part=part;cut_part.insert_history_entry(document::PartHistoryKind::Feature,opening.id);
    cut_part.history.push_back(opening);cut_part.sketches.push_back(cut_sketch);
    auto cut_operation=cut_part.kernel_operations().back();cut_history.push_back(cut_operation);
    const auto cut_result=kernel.evaluate_history_incremental(cut_history,result);valid(cut_result.back());
    check(std::ranges::none_of(cut_result.back().sheet_cuts,[&](const auto& region){
        return region.source.owner_id==form.owner_id;}),"Sheet Cut processed a symbolic FORM skin");
    check(source.part.serialized()==authored,"FORM insertion changed the library source");
    auto feature=document::create_sheet_form();feature.sheet_form=document::copy_sheet_form_definition(source);
    feature.sheet_form.support=*face;feature.placement.rotation_x=90;feature.placement.absolute_rotation_x=90;
    check(normal==kernel::Vec3{0,0,1},"Native persistence fixture must retain its outward normal");
    check(document::stored_sheet_form_definition(feature.sheet_form).part.document_id!=source.part.document_id,
        "FORM persisted the library document namespace");
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
    const auto persisted=part.serialized();auto reopened=document::PartDocument::from_serialized(persisted);
    check(reopened.history.back()==feature,"FORM JSON round trip lost its independent definition or placement");
    const auto file=std::filesystem::absolute("build/form-diagnostic/InsertedVentilationWindow.prtz");part.save(file);
    reopened=document::PartDocument::load(file);
    check(reopened.history.back()==feature,"FORM native save/reopen lost its independent definition or placement");
    auto copied=document::stored_sheet_form_definition(feature.sheet_form);
    auto renamed=copied.part;for(const auto& id:copied.bodies) {
        auto body=*renamed.body_history.find(id);body.name="Renamed "+id;renamed.body_history.update_body(body);
    }
    auto parameters=feature.sheet_form;
    parameters.definition=std::make_shared<const std::string>(renamed.serialized(*copied.calculated).dump());
    check(document::stored_sheet_form_definition(parameters).bodies==copied.bodies,"FORM roles depended on Body names after insertion");
    start=std::chrono::steady_clock::now();const auto native=kernel.evaluate_history(reopened.kernel_operations());
    std::cout<<"FORM reopened native calculation="<<elapsed(start)<<" s"<<std::endl;valid(native.back());
    near(native.back().volume,result.back().volume);
    std::cout<<"FORM entire regression="<<elapsed(begin)<<" s"<<std::endl;
    std::cout<<"Native spatial/flat FORM geometry passed"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
