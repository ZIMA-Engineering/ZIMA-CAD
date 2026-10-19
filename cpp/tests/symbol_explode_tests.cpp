#include <zima/symbols/definition.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/document_session.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <numbers>
#include <iostream>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message){if(std::abs(a-b)>=1e-8*std::max({1.,std::abs(a),std::abs(b)}))throw std::runtime_error(std::string(message)+": "+std::to_string(a)+" != "+std::to_string(b));}
void verify(const symbols::Definition& definition,const std::string& variant,bool reflected,double rotation,double scale) {
    auto target=reflected?drawing::create_template_sketch(true,"Explode"):sketcher::Sketch::create_default();
    sketcher::SymbolInstance instance;instance.id="instance";instance.definition=definition.serialized();instance.variant=variant;
    instance.x=70;instance.y=-25;instance.angle_degrees=rotation;instance.scale=scale;target.add_symbol(instance);
    const double angle=rotation*std::numbers::pi/180.,c=std::cos(angle),s=std::sin(angle);
    const auto position=[&](double x,double y){x=(x-definition.insertion_point[0])*scale;y=(y-definition.insertion_point[1])*scale;
        return std::array{instance.x+(reflected?-1.:1.)*(c*x-s*y),instance.y+s*x+c*y};};
    symbols::explode_instance(target,instance.id);check(target.symbols.empty(),"Explode retained its source instance");target.validate();
    std::size_t lines{},circles{},arcs{},ellipses{},elliptical_arcs{},splines{},texts{},corners{};
    std::size_t points{};
    for(const auto& source:definition.evaluate(variant)) {
        points+=source.points.size();
        if(reflected)for(const auto& text:source.texts)if(text.anchor_point_id.empty())++points;
        const auto o=source.world_point(0,0),x=source.world_point(1,0),y=source.world_point(0,1);
        const bool mirrored=((x.x-o.x)*(y.y-o.y)-(x.y-o.y)*(y.x-o.x)<0)!=reflected;
        const auto local_position=[&](double x,double y){const auto p=source.world_point(x,y);return position(p.x,p.y);};
        const auto point=[&](const std::string& a,const std::string& b) {
            const auto* original=source.find_point(a);const auto* copied=target.find_point(b);
            check(original&&copied&&!copied->fixed,"Exploded geometry has a missing or secretly fixed point");
            const auto p=local_position(original->x,original->y);near(copied->x,p[0],"Explode X changed exact geometry");near(copied->y,p[1],"Explode Y changed exact geometry");
        };
        for(const auto& old:source.segments){const auto& v=target.segments.at(lines++);point(old.first_point_id,v.first_point_id);point(old.second_point_id,v.second_point_id);}
        for(const auto& old:source.circles){const auto& v=target.circles.at(circles++);point(old.center_point_id,v.center_point_id);near(v.radius,old.radius*scale,"Explode circle radius");}
        for(const auto& old:source.arcs){const auto& v=target.arcs.at(arcs++);point(old.center_point_id,v.center_point_id);point(old.start_point_id,mirrored?v.end_point_id:v.start_point_id);point(old.end_point_id,mirrored?v.start_point_id:v.end_point_id);near(v.radius,old.radius*scale,"Explode arc radius");
            near(v.end_angle-v.start_angle,old.end_angle-old.start_angle,"Explode selected the complementary arc");}
        for(const auto& old:source.ellipses){const auto& v=target.ellipses.at(ellipses++);point(old.center_point_id,v.center_point_id);point(old.major_point_id,v.major_point_id);point(old.minor_point_id,v.minor_point_id);near(v.major_radius,old.major_radius*scale,"Explode ellipse radius");check(v.reversed==(old.reversed!=mirrored),"Explode ellipse handedness");}
        for(const auto& old:source.elliptical_arcs){const auto& v=target.elliptical_arcs.at(elliptical_arcs++);point(old.center_point_id,v.center_point_id);point(old.start_point_id,v.start_point_id);point(old.end_point_id,v.end_point_id);near(v.start_parameter,old.start_parameter,"Explode ellipse start parameter");near(v.end_parameter,old.end_parameter,"Explode ellipse end parameter");check(v.reversed==(old.reversed!=mirrored),"Explode elliptical-arc handedness");}
        for(const auto& old:source.bsplines){const auto& v=target.bsplines.at(splines++);check(v.degree==old.degree&&v.knots==old.knots&&v.weights==old.weights&&v.closed==old.closed&&v.interpolating==old.interpolating,"Explode approximated an exact spline");for(std::size_t i=0;i<old.control_point_ids.size();++i)point(old.control_point_ids[i],v.control_point_ids.at(i));
            const auto before=sketcher::sketch_curve_geometry(source,old.id),after=sketcher::sketch_curve_geometry(target,v.id);
            for(int i=0;i<=32;++i){const auto p=kernel::bspline_value(before,i/32.),q=kernel::bspline_value(after,i/32.);const auto expected=local_position(p.x,p.y);near(q.x,expected[0],"Explode trimmed/offset curve X");near(q.y,expected[1],"Explode trimmed/offset curve Y");}
        }
        for(const auto& old:source.corner_radii){const auto& v=target.corner_radii.at(corners++);near(v.radius,old.radius*scale,"Explode corner radius");point(old.vertex_id,v.vertex_id);}
        for(const auto& old:source.texts){const auto& v=target.texts.at(texts++);check(v.value==old.value&&v.font==old.font&&v.contours.size()==old.contours.size(),"Explode changed text or replaced it by line fragments");
            near(v.height,old.height*scale,"Explode text size");
            for(std::size_t i=0;i<old.contours.size();++i)for(std::size_t j=0;j<old.contours[i].size();++j){const auto p=local_position(old.contours[i][j][0],old.contours[i][j][1]);near(v.contours[i][j][0],p[0],"Explode text X");near(v.contours[i][j][1],p[1],"Explode text Y");}
            auto edited=v;sketcher::rebuild_text_contours(edited,reflected);
            const auto bounds=[](const auto& contours){std::array<double,4> b{1e100,1e100,-1e100,-1e100};for(const auto& contour:contours)for(const auto& p:contour){b[0]=std::min(b[0],p[0]);b[1]=std::min(b[1],p[1]);b[2]=std::max(b[2],p[0]);b[3]=std::max(b[3],p[1]);}return b;};
            const auto before=bounds(v.contours),after=bounds(edited.contours);
            for(int i=0;i<4;++i)check(std::abs(before[i]-after[i])<.002,"Text Properties would change exploded text orientation");
        }
    }
    check(lines==target.segments.size()&&circles==target.circles.size()&&arcs==target.arcs.size()&&ellipses==target.ellipses.size()&&elliptical_arcs==target.elliptical_arcs.size()&&splines==target.bsplines.size()&&texts==target.texts.size()&&corners==target.corner_radii.size(),"Explode lost or duplicated native entities");
    check(target.constraints.empty()&&target.dimensions.empty(),"Explode added hidden source constraints");
    check(target.points.size()==points,"Explode dropped standalone native points");
    check(sketcher::Sketch::from_serialized(target.serialized()).serialized()==target.serialized(),"Exploded native data did not reopen exactly");
}
}
int main(){try {
    std::size_t cases{};
    {
        auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_point(-10,7));
        static_cast<void>(sketch.add_segment(0,0,5,5));static_cast<void>(sketch.add_circle(10,0,3));
        static_cast<void>(sketch.add_arc(0,20,5,20,0,25));
        static_cast<void>(sketch.add_ellipse(15,20,20,20,15,22));
        static_cast<void>(sketch.add_elliptical_arc(30,20,35,20,30,22,35,20,30,22));
        static_cast<void>(sketch.add_bspline({{0,40},{5,50},{10,40}},2));
        auto text=sketcher::Sketch::create_text();text.value="ZIMA Ω";text.anchor_x=10;text.anchor_y=30;text.height=2;
        sketcher::rebuild_text_contours(text,true);sketch.add_text(text);
        symbols::Definition definition;definition.id="mixed-native";definition.name="Exact native geometry";definition.default_variant="default";
        definition.variants["default"].sketches={sketch.id};definition.sketches={sketch};definition.insertion_point={2,3};
        for(bool reversed_frame:{false,true}) {
            auto& source=definition.sketches.front();source.resolved_origin={7,9,0};
            const double angle=27*std::numbers::pi/180.,sign=reversed_frame?-1.:1.;
            source.resolved_x_axis={std::cos(angle),std::sin(angle),0};source.resolved_y_axis={-sign*std::sin(angle),sign*std::cos(angle),0};source.resolved_normal={0,0,sign};
            for(bool reflected:{false,true})for(double rotation:{0.,37.,90.,180.})for(double scale:{.5,2.}) {
                verify(definition,"default",reflected,rotation,scale);++cases;
            }
        }
        // Detached definition Sketches use their authored XY plane, not stale
        // resolved-frame cache metadata. Use the same live frame as rendering.
        auto cached=definition;cached.sketches.front().resolved_x_axis={.8,0,.6};cached.sketches.front().resolved_y_axis={0,1,0};
        verify(cached,"default",false,37,2);++cases;
    }
    {
        auto sketch=sketcher::Sketch::create_default();const auto circle=sketch.add_circle(0,0,10);
        const auto offset=sketch.add_offset(circle,2,false);static_cast<void>(sketch.retain_curve_intervals(offset,{{.13,.81}}));
        static_cast<void>(sketch.retain_curve_intervals(circle,{{.15,.8}}));
        const auto first=sketch.add_segment(0,40,10,40),second=sketch.add_segment(10,40,10,50);static_cast<void>(sketch.add_corner_fillet(first,second,2));
        symbols::Definition definition;definition.id="derived-native";definition.name="Trimmed offset and corner";definition.default_variant="default";
        definition.variants["default"].sketches={sketch.id};definition.sketches={sketch};
        for(bool reflected:{false,true})for(double angle:{0.,37.,90.,180.})for(double scale:{.5,2.}){verify(definition,"default",reflected,angle,scale);++cases;}
    }
    for(const auto& entry:std::filesystem::recursive_directory_iterator("config/symbols"))if(entry.is_regular_file()&&entry.path().extension()==".symz") {
        const auto definition=symbols::Definition::load(entry.path());
        for(const auto& [variant,row]:definition.variants)for(bool reflected:{false,true})for(double rotation:{0.,37.,90.,180.})for(double scale:{.5,2.}){
            verify(definition,variant,reflected,rotation,scale);++cases;
        }
    }
    auto target=sketcher::Sketch::create_default();const auto definition=symbols::projection_method();
    sketcher::SymbolInstance instance;instance.id="one";instance.definition=definition.serialized();instance.variant=definition.default_variant;
    target.add_symbol(instance);auto part=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();target.owner_container_id=owner.id;part.sketches={target};part.history={owner};
    workspace::Workspace live;const auto id=part.document_id;live.add_part(part);const auto original=target.serialized();
    check(workspace::mutate_document_sketch(live,id,target.id,[&](auto& pending){symbols::explode_instance(pending,instance.id);}),"Explode did not commit");
    auto& session=live.open_part(id)->session;check(session.undo()&&session.document().sketches.front().serialized()==original,"Explode Undo lost the exact embedded symbol");
    check(session.redo()&&session.document().sketches.front().symbols.empty(),"Explode Redo failed");
    auto editable=session.document().sketches.front();const auto point=editable.segments.front().first_point_id;const auto* p=editable.find_point(point);const double x=p->x,y=p->y;
    check(editable.move_point(point,x+2,y+3),"Exploded native geometry cannot be edited");
    std::cout<<cases<<" factory variant/rotation/scale/coordinate cases preserve native geometry/text; Undo/Redo and editing passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
