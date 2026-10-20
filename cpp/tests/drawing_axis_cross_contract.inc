#include <zima/kernel/stable_id.hpp>
namespace {
void verify_drawing_axis_cross_contract() {
    kernel::ViewerMesh mesh;
    mesh.edges={line("axis-test-top",{0,30,0},{70,30,0}),line("axis-test-right",{70,0,0},{70,30,0}),
        line("axis-test-slant",{0,-10,0},{20,0,0})};
    auto v=view(mesh);
    const auto make_axis=[](double x,double y,const std::string& occurrence){
        ModelAnnotation a;a.kind=ModelAnnotationKind::Axis;a.visible=true;
        a.source={"part","hole","axis:opening",occurrence};
        a.model_axis=std::array<kernel::Vec3,2>{{{x,y,-5},{x,y,5}}};
        a.model_envelope.include({x-5,y-5,-5});a.model_envelope.include({x+5,y+5,5});return a;
    };
    v.model_annotations={make_axis(10,10,"first"),make_axis(30,10,"second"),make_axis(50,10,"third")};
    const auto original_axes=v.model_annotations;
    MeasurementPickRequest line_request;line_request.lines_only=true;
    const auto pick=[&](Point2 cursor,const MeasurementPickRequest& request){
        auto candidates=checked_candidates(v,cursor,.2,request);require(!candidates.empty(),"Axis stroke not offered");return candidates.front().attachment;
    };
    const auto horizontal=pick({16,10},{}),vertical=pick({10,16},{});
    require(horizontal.kind==DimensionAttachmentKind::Line&&vertical.kind==DimensionAttachmentKind::Line,
        "Automatic axis-cross entry did not select the displayed line");
    require(measurement_axis_cross_stroke(horizontal.reference)==AxisCrossStroke::Horizontal&&
        measurement_axis_cross_stroke(vertical.reference)==AxisCrossStroke::Vertical,"Axis-cross directions lost independent identities");
    require(horizontal.reference!=vertical.reference,"Both axis-cross strokes share one identity");
    require(pick({4,10},line_request).reference==horizontal.reference&&pick({10,4},line_request).reference==vertical.reference,
        "Opposite halves of one axis stroke changed identity");
    for(const auto& a:{horizontal,vertical}) {
        const auto geometry=measurement_reference_geometry(v,a.reference);
        require(geometry.size()==2,"Axis inspection did not retain the two exact displayed halves");
        for(const auto& half:geometry)for(auto p:half)
            near(measurement_axis_cross_stroke(a.reference)==AxisCrossStroke::Horizontal?p.y:p.x,10);
    }
    auto explicit_center=MeasurementPickRequest{};explicit_center.mode=int(DimensionAttachmentKind::Center);
    auto point_request=MeasurementPickRequest{};point_request.mode=int(DimensionAttachmentKind::Point);
    const auto center=pick({16,10},explicit_center),point=pick({10,16},point_request),automatic_center=pick({10,10},{});
    require(center.kind==DimensionAttachmentKind::Center&&center.reference.semantic_key=="axis:opening"&&
        point.kind==center.kind&&point.reference==center.reference&&automatic_center.kind==center.kind&&
        automatic_center.reference==center.reference,"Centre/Point entry changed the physical axis reference");
    const auto second=pick({30,16},line_request),third=pick({50,16},line_request);
    require(second.reference.instance_path=="second"&&third.reference.instance_path=="third"&&
        second.reference!=vertical.reference,"Axis-cross reference crossed occurrence ownership");
    std::vector<DrawingDimension> retained;
    for(const auto& stroke:{horizontal,vertical})for(const bool reverse:{false,true}) {
        DimensionAttachment edge{DimensionAttachmentKind::Line,ref("axis-test-slant"),{},.5};
        auto angle=make_drawing_dimension(v.id,DrawingDimensionKind::Angular);
        angle.attachments=reverse?std::vector{edge,stroke}:std::vector{stroke,edge};
        const auto result=evaluate_drawing_dimension(v,angle);
        require(result.state==MeasurementState::Resolved,"Axis/edge angle did not resolve");
        near(result.presentations[0].value,measurement_axis_cross_stroke(stroke.reference)==AxisCrossStroke::Horizontal?
            std::atan(.5)*180/std::numbers::pi:90-std::atan(.5)*180/std::numbers::pi);
        refresh_drawing_dimension(v,angle);retained.push_back(angle);
        auto linear=make_drawing_dimension(v.id);
        edge.reference=ref(measurement_axis_cross_stroke(stroke.reference)==AxisCrossStroke::Horizontal?"axis-test-top":"axis-test-right");
        linear.attachments=reverse?std::vector{edge,stroke}:std::vector{stroke,edge};
        const auto distance=evaluate_drawing_dimension(v,linear);require(distance.state==MeasurementState::Resolved,"Axis/edge distance did not resolve");
        near(distance.presentations[0].value,measurement_axis_cross_stroke(stroke.reference)==AxisCrossStroke::Horizontal?20:60);
        refresh_drawing_dimension(v,linear);retained.push_back(linear);
    }
    for(const bool reverse:{false,true})for(const int first_side:{-1,1})for(const int second_side:{-1,1}) {
        auto angle=make_drawing_dimension(v.id,DrawingDimensionKind::Angular);
        angle.attachments=reverse?std::vector{vertical,horizontal}:std::vector{horizontal,vertical};
        angle.attachments[0].side=first_side;angle.attachments[1].side=second_side;
        const auto result=evaluate_drawing_dimension(v,angle);
        require(result.state==MeasurementState::Resolved,"Axis-cross angle did not resolve");near(result.presentations[0].value,90);
        place_drawing_dimension(v,angle,0,{20,20});refresh_drawing_dimension(v,angle);retained.push_back(angle);
    }
    for(const bool reverse:{false,true}) {
        auto linear=make_drawing_dimension(v.id);
        linear.attachments=reverse?std::vector{second,vertical}:std::vector{vertical,second};
        auto result=evaluate_drawing_dimension(v,linear);require(result.state==MeasurementState::Resolved,"Axis distance did not resolve");
        near(result.presentations[0].value,20);refresh_drawing_dimension(v,linear);retained.push_back(linear);
    }
    auto chain=make_drawing_dimension(v.id,DrawingDimensionKind::Chain);
    chain.attachments={vertical,second,third};resize_dimension_segments(chain);
    auto chain_result=evaluate_drawing_dimension(v,chain);require(chain_result.state==MeasurementState::Resolved,"Axis chain did not resolve");
    near(chain_result.presentations[0].value,20);near(chain_result.presentations[1].value,40);
    refresh_drawing_dimension(v,chain);retained.push_back(chain);
    // Exact source metadata changes, rather than an inferred or copied centre.
    for(double shift:{2.,-3.,4.}) {
        auto moved=v;
        for(auto& p:*moved.model_annotations[1].model_axis)p.x+=shift;
        auto result=evaluate_drawing_dimension(moved,chain);require(result.state==MeasurementState::Resolved,"Moved hole lost its axis reference");
        near(result.presentations[0].value,20+shift);near(result.presentations[1].value,40);
    }
    for(double scale:{.25,1.,4.}) {
        v.scale=scale;const auto geometry=axis_annotation_geometry(v,v.model_annotations.front());
        for(const auto& stroke:{horizontal,vertical}) {
            const auto index=measurement_axis_cross_stroke(stroke.reference)==AxisCrossStroke::Horizontal?1:3;
            const auto end=geometry.curves[index].back();
            Point2 at{10+.8*(end.x-10),10+.8*(end.y-10)};
            require(pick(at,line_request).reference==stroke.reference,"Scale changed the chosen axis-cross identity");
        }
        near(evaluate_drawing_dimension(v,chain).presentations[1].value,40);
    }
    v.scale=1;
    v.model_annotations.front().visible=false;
    require(checked_candidates(v,{16,10},.2,line_request).empty(),"Hidden axis cross remains pickable");
    near(evaluate_drawing_dimension(v,chain).presentations[0].value,20);
    v.model_annotations.front().visible=true;
    v.camera={{1,0,0},{0,0,1},{0,-1,0}};
    require(evaluate_drawing_dimension(v,chain).state==MeasurementState::Unresolved,
        "End-on stroke silently rebound to the side-on physical axis");
    require(measurement_reference_geometry(v,vertical.reference).empty(),"Missing cross stroke still highlights side-on geometry");
    v.camera={{1,0,0},{0,1,0},{0,0,1}};
    near(evaluate_drawing_dimension(v,chain).presentations[0].value,20);
    v.model_annotations.front().unresolved=true;
    require(evaluate_drawing_dimension(v,chain).state==MeasurementState::Unresolved,"Lost parent axis silently rebound");
    v.model_annotations.front().unresolved=false;
    require(v.model_annotations==original_axes,"Measurement changed the source model annotations");
    auto document=DrawingDocument::create_default();document.sheets.front().views={v};document.sheets.front().dimensions=retained;
    const auto path=std::filesystem::path("build")/("axis-cross-contract-"+kernel::make_stable_id()+".drwz");
    document.save(path);const auto reopened=DrawingDocument::load(path);
    const auto& saved=reopened.sheets.front();require(saved.dimensions==retained,"Native Drawing lost axis-cross references or sides");
    for(const auto& dimension:saved.dimensions)
        require(evaluate_drawing_dimension(saved.views.front(),dimension).state==MeasurementState::Resolved,"Reopened axis-cross dimension is unresolved");
    require(!measurement_axis_cross_stroke({"hole","drawing:axis-cross:h:5:wrong-size",{}}),"Malformed axis-cross reference accepted");
}
void verify_exact_circular_contacts() {
    for(const double sweep:{std::numbers::pi,-std::numbers::pi,1.6*std::numbers::pi,-1.6*std::numbers::pi,2*std::numbers::pi,-2*std::numbers::pi}) {
        kernel::ViewerMesh mesh;mesh.edges={circle("continuous-arc",50,40,20,sweep)};const auto v=view(mesh);
        for(const double fraction:{.21,.31,.73}) {
            const double angle=sweep*fraction;const Point2 target{50+20*std::cos(angle),40+20*std::sin(angle)};
            MeasurementPickRequest request;request.mode=int(DimensionAttachmentKind::CurvePoint);request.curve_contacts=true;
            const auto offered=checked_candidates(v,target,.01,request);require(!offered.empty(),"Exact circle contact not offered");
            const auto point=resolve_dimension_attachment(v,offered.front().attachment);require(point.has_value(),"Exact circle contact did not resolve");
            require(std::hypot(point->x-target.x,point->y-target.y)<1e-9,"Continuous circle contact used interpolated chord parameters");
            near(std::hypot(point->x-50,point->y-40),20);
        }
    }
}
} // namespace
