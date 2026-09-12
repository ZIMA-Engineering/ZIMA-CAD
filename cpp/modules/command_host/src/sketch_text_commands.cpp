#include "sketch_command_support.hpp"
#include <zima/sketcher/text_geometry.hpp>

namespace zima::command_host {
using namespace sketch_commands;
namespace {
const sketcher::SketchText& text(const Sketch& s,const std::string& id) {
    const auto found=std::ranges::find(s.texts,id,&sketcher::SketchText::id);
    if(found==s.texts.end())throw workspace::SketchOperationError("text_not_found","The Sketch text does not exist.");return *found;
}
Json data(const sketcher::SketchText& t) {
    std::size_t points=0;Json bounds=nullptr;double left=0,right=0,bottom=0,top=0;bool first=true;
    for(const auto& c:t.contours)for(const auto& p:c){++points;if(first){left=right=p[0];bottom=top=p[1];first=false;}else{left=std::min(left,p[0]);right=std::max(right,p[0]);bottom=std::min(bottom,p[1]);top=std::max(top,p[1]);}}
    if(!first)bounds={{"min",{left,bottom}},{"max",{right,top}}};
    return {{"text",t.id},{"value",t.value},{"position",{t.anchor_x,t.anchor_y}},{"height_mm",t.height},{"angle_degrees",t.angle_degrees},{"flipped",t.flipped},{"font",t.font},{"modeling_geometry",t.modeling_geometry},
        {"horizontal",t.horizontal==sketcher::TextHorizontalAlignment::Left?"left":t.horizontal==sketcher::TextHorizontalAlignment::Center?"center":"right"},
        {"vertical",t.vertical==sketcher::TextVerticalAlignment::Bottom?"bottom":t.vertical==sketcher::TextVerticalAlignment::Middle?"middle":"top"},
        {"color",t.color==sketcher::SketchTextColor::Green?"green":t.color==sketcher::SketchTextColor::White?"white":t.color==sketcher::SketchTextColor::Yellow?"yellow":"red"},
        {"contour_count",t.contours.size()},{"point_count",points},{"bounds_mm",bounds},{"anchor_point",t.anchor_point_id}};
}
void patch(sketcher::SketchText& t,const Json& a) {
    if(a.contains("value"))t.value=a["value"].get<std::string>();
    if(a.contains("position")){const auto p=point(a,"position");t.anchor_x=p[0];t.anchor_y=p[1];}
    if(a.contains("height_mm"))t.height=number(a["height_mm"]);
    if(a.contains("angle_degrees"))t.angle_degrees=number(a["angle_degrees"]);
    if(a.contains("flipped"))t.flipped=a["flipped"].get<bool>();
    if(a.contains("modeling_geometry"))t.modeling_geometry=a["modeling_geometry"].get<bool>();
    if(a.contains("horizontal")) {
        const auto v=a["horizontal"].get<std::string>();if(v!="left"&&v!="center"&&v!="right")invalid("Text horizontal alignment must be left, center or right.");
        t.horizontal=v=="left"?sketcher::TextHorizontalAlignment::Left:v=="center"?sketcher::TextHorizontalAlignment::Center:sketcher::TextHorizontalAlignment::Right;
    }
    if(a.contains("vertical")) {
        const auto v=a["vertical"].get<std::string>();if(v!="bottom"&&v!="middle"&&v!="top")invalid("Text vertical alignment must be bottom, middle or top.");
        t.vertical=v=="bottom"?sketcher::TextVerticalAlignment::Bottom:v=="middle"?sketcher::TextVerticalAlignment::Middle:sketcher::TextVerticalAlignment::Top;
    }
    if(a.contains("color")) {
        const auto v=a["color"].get<std::string>();if(v!="green"&&v!="white"&&v!="yellow"&&v!="red")invalid("Text color must be green, white, yellow or red.");
        t.color=v=="green"?sketcher::SketchTextColor::Green:v=="white"?sketcher::SketchTextColor::White:v=="yellow"?sketcher::SketchTextColor::Yellow:sketcher::SketchTextColor::Red;
    }
}
}
void Host::register_sketch_text_commands() {
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> args;
        if(!create)args.push_back({"text",true});
        args.push_back({"value",create});args.push_back({"position",create,Type::Array});
        const std::vector<commands::Argument> fields={{"height_mm",false,Type::Number},{"angle_degrees",false,Type::Number},{"flipped",false,Type::Boolean},{"modeling_geometry",false,Type::Boolean},{"horizontal",false},{"vertical",false},{"color",false}};
        args.insert(args.end(),fields.begin(),fields.end());
        add_sketch_command({create?"sketch.text.create":"sketch.text.set",tr("Create or edit Sketch text with native outlines of the bundled font."),args},[create](Sketch& s,const Json& a) {
            auto t=create?Sketch::create_text():text(s,a["text"].get<std::string>());const auto before=t;patch(t,a);
            if(create||t!=before)sketcher::rebuild_text_contours(t);
            if(create)s.add_text(t);else s.update_text(t);return data(text(s,t.id));
        });
    }
    add_sketch_query({"sketch.text.get",tr("Read text properties, contour counts and bounds without rebuilding its glyphs."),{{"text",true}}},[](const Sketch& s,const Json& a){return data(text(s,a["text"].get<std::string>()));});
}
}
