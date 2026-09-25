#include <zima/symbols/definition.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace zima::symbols {
namespace {
using Json=nlohmann::json;
void require(bool valid) {if(!valid)throw std::invalid_argument("Invalid symbol definition");}
void check_value(const TextField& field,const std::string& value) {
    require(field.allow_custom||std::ranges::find(field.choices,value)!=field.choices.end());
}
}
void Definition::validate() const {
    require(!id.empty()&&!name.empty()&&std::isfinite(insertion_point[0])&&std::isfinite(insertion_point[1]));
    require(!sketches.empty()&&variants.contains(default_variant));
    require(variant_source.empty()||variant_source=="drawing.projection_method");
    std::set<std::string> ids;
    for(const auto& sketch:sketches) {
        require(sketch.symbols.empty()); // No recursive symbol graphs.
        sketch.validate();require(ids.insert(sketch.id).second);
        require(sketch.external_references.empty()&&sketch.plane_reference_owner_id.empty()&&sketch.owner_container_id.empty()&&!sketch.drawing_template);
        require(sketch.plane==sketcher::SketchPlane::XY&&sketch.plane_offset==0);
    }
    if(frame_layout) {
        const auto& f=*frame_layout;require(!f.cells.empty()&&std::isfinite(f.height)&&f.height>0&&std::isfinite(f.padding)&&f.padding>=0&&std::isfinite(f.minimum_width)&&f.minimum_width>0);
        std::set<std::string> cells;for(const auto& id:f.cells)require(ids.contains(id)&&cells.insert(id).second);
    }
    std::set<std::pair<std::string,std::string>> text_ids;
    for(const auto& [owner,entries]:pens) {
        require(ids.contains(owner));
        const auto& sketch=*std::ranges::find(sketches,owner,&sketcher::Sketch::id);
        std::set<std::string> curves;
        const auto add=[&](const auto& values){for(const auto& value:values)curves.insert(value.id);};
        add(sketch.segments);add(sketch.circles);add(sketch.arcs);add(sketch.ellipses);add(sketch.elliptical_arcs);add(sketch.bsplines);
        for(const auto& [curve,pen]:entries) {
            require(pen=="white"||pen=="yellow"||pen=="green");
            require(curves.contains(curve));
        }
    }
    for(const auto& [key,field]:fields) {
        require(!key.empty()&&ids.contains(field.sketch_id));
        const auto& sketch=*std::ranges::find(sketches,field.sketch_id,&sketcher::Sketch::id);
        require(std::ranges::find(sketch.texts,field.text_id,&sketcher::SketchText::id)!=sketch.texts.end());
        require(text_ids.emplace(field.sketch_id,field.text_id).second);
        require(field.allow_custom||!field.choices.empty());
    }
    for(const auto& [key,row]:variants) {
        require(!key.empty());std::set<std::string> used;
        for(const auto& sketch:row.sketches)require(ids.contains(sketch)&&used.insert(sketch).second);
        for(const auto& [field,value]:row.text_values){require(fields.contains(field));check_value(fields.at(field),value);}
        for(const auto& field:row.hidden_texts)require(fields.contains(field));
    }
    if(variant_source=="drawing.projection_method")require(variants.contains("first_angle")&&variants.contains("third_angle"));
}
std::vector<sketcher::Sketch> Definition::evaluate(const std::string& variant,const std::map<std::string,std::string>& overrides) const {
    validate();const auto& row=variants.at(variant.empty()?default_variant:variant);
    for(const auto& [field,value]:overrides){require(fields.contains(field));check_value(fields.at(field),value);}
    std::vector<sketcher::Sketch> result;
    for(const auto& id:row.sketches) {
        auto sketch=*std::ranges::find(sketches,id,&sketcher::Sketch::id);
        for(const auto& [key,field]:fields)if(field.sketch_id==id) {
            if(std::ranges::find(row.hidden_texts,key)!=row.hidden_texts.end()) {
                std::erase_if(sketch.texts,[&](const auto& text){return text.id==field.text_id;});continue;
            }
            auto& text=*std::ranges::find(sketch.texts,field.text_id,&sketcher::SketchText::id);
            if(row.text_values.contains(key))text.value=row.text_values.at(key);
            if(overrides.contains(key))text.value=overrides.at(key);
            if(text.value.find_first_not_of(" \t\r\n")==std::string::npos) {
                std::erase_if(sketch.texts,[&](const auto& item){return item.id==field.text_id;});continue;
            }
            sketcher::rebuild_text_contours(text,true);
        }
        result.push_back(std::move(sketch));
    }
    if(frame_layout) {
        const auto& layout=*frame_layout;double left=0;
        for(const auto& id:layout.cells) {
            const auto found=std::ranges::find(result,id,&sketcher::Sketch::id);if(found==result.end())continue;
            auto& sketch=*found;double xmin=1e100,xmax=-1e100;
            for(const auto& edge:sketch.viewer_mesh().edges) {
                const auto& key=edge.reference.semantic_key;if(key.starts_with("sketch_axis:")||key.starts_with("dimension:"))continue;
                for(const auto p:edge.points){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);}
            }
            if(xmin>xmax)continue;
            const double width=std::max(layout.minimum_width,xmax-xmin+2*layout.padding);
            const double shift=left+(width-(xmax-xmin))/2-xmin;
            for(auto& point:sketch.points)point.x+=shift;
            for(auto& text:sketch.texts){text.anchor_x+=shift;sketcher::rebuild_text_contours(text,true);}
            const auto segment=[&](const std::string& suffix,double x,double y,double u,double v){
                const auto key=id+":frame:"+suffix;sketch.points.push_back({key+":a",x,y,true});sketch.points.push_back({key+":b",u,v,true});sketch.segments.push_back({key,key+":a",key+":b"});
            };
            const double bottom=-layout.height/2,top=layout.height/2;
            if(left==0)segment("left",left,bottom,left,top);
            segment("bottom",left,bottom,left+width,bottom);segment("top",left,top,left+width,top);
            segment("right",left+width,bottom,left+width,top);left+=width;
        }
    }
    return result;
}
std::string Definition::serialized() const {
    validate();Json data={{"format","zima.symbol"},{"version",2},{"units","mm"},{"id",id},{"name",name},
        {"insertion_point",insertion_point},{"default_variant",default_variant},{"variant_source",variant_source},
        {"sketches",Json::array()},{"fields",Json::object()},{"variants",Json::object()}};
    data["pens"]=pens;
    if(frame_layout)data["frame_layout"]={{"cells",frame_layout->cells},{"height",frame_layout->height},{"padding",frame_layout->padding},{"minimum_width",frame_layout->minimum_width}};
    for(const auto& sketch:sketches)data["sketches"].push_back(Json::parse(sketch.serialized()));
    for(const auto& [key,field]:fields)data["fields"][key]={{"sketch",field.sketch_id},{"text",field.text_id},{"choices",field.choices},{"allow_custom",field.allow_custom}};
    for(const auto& [key,row]:variants)data["variants"][key]={{"sketches",row.sketches},{"text_values",row.text_values},{"hidden_texts",row.hidden_texts}};
    return data.dump(2)+"\n";
}
Definition Definition::from_serialized(const std::string& data) {
    const auto root=Json::parse(data);
    require(root.at("format")=="zima.symbol"&&root.at("version")==2&&root.at("units")=="mm");
    Definition d;d.id=root.at("id");d.name=root.at("name");d.insertion_point=root.at("insertion_point").get<std::array<double,2>>();
    d.default_variant=root.at("default_variant");d.variant_source=root.at("variant_source");
    d.pens=root.value("pens",decltype(d.pens){});
    if(root.contains("frame_layout")){const auto& f=root.at("frame_layout");d.frame_layout=FrameLayout{f.at("cells").get<std::vector<std::string>>(),f.at("height"),f.at("padding"),f.at("minimum_width")};}
    for(const auto& value:root.at("sketches")) {
        require(!value.contains("symbols")||value.at("symbols").empty());
        d.sketches.push_back(sketcher::Sketch::from_serialized(value.dump()));
    }
    for(const auto& [key,f]:root.at("fields").items())d.fields[key]={f.at("sketch"),f.at("text"),f.at("choices").get<std::vector<std::string>>(),f.at("allow_custom")};
    for(const auto& [key,r]:root.at("variants").items())d.variants[key]={r.at("sketches").get<std::vector<std::string>>(),r.at("text_values").get<std::map<std::string,std::string>>(),r.at("hidden_texts").get<std::vector<std::string>>()};
    d.validate();return d;
}
kernel::ViewerMesh instance_mesh(const sketcher::SymbolInstance& instance,const std::string& cad_variant,std::optional<double> paper_frame_angle) {
    const auto d=Definition::from_serialized(instance.definition);
    const auto variant=instance.use_cad_variant&&!cad_variant.empty()?cad_variant:instance.variant;
    kernel::ViewerMesh result;if(!instance.visible)return result;
    const double a=instance.angle_degrees*3.141592653589793/180.,c=std::cos(a),s=std::sin(a);
    for(auto& sketch:d.evaluate(variant,instance.text_values)) {
        if(paper_frame_angle)for(auto& text:sketch.texts)if(text.drawing_keep_readable) {
            // ISO 1302:1992 7.1: readable from bottom/right. A half-turn
            // preserves the authored center, symbol geometry and text slope.
            const double angle=std::remainder(*paper_frame_angle+instance.angle_degrees+text.angle_degrees,360.);
            if(angle>90.+1e-9||angle<=-90.+1e-9) {
                double left=1e100,right=-1e100,bottom=1e100,top=-1e100;
                for(const auto& contour:text.contours)for(const auto& p:contour){left=std::min(left,p[0]);right=std::max(right,p[0]);bottom=std::min(bottom,p[1]);top=std::max(top,p[1]);}
                if(left<=right)for(auto& contour:text.contours)for(auto& p:contour){p[0]=left+right-p[0];p[1]=bottom+top-p[1];}
            }
        }
        for(auto edge:sketch.viewer_mesh().edges) {
            const auto& key=edge.reference.semantic_key;
            if(key.starts_with("sketch_axis:")||key.starts_with("dimension:"))continue;
            for(auto& p:edge.points){const double x=(p.x-d.insertion_point[0])*instance.scale,y=(p.y-d.insertion_point[1])*instance.scale;p={instance.x+c*x-s*y,instance.y+s*x+c*y,0};}
            const auto separator=key.find(':');
            const auto curve=separator==std::string::npos?key:key.substr(separator+1);
            const auto pen=d.pens.contains(sketch.id)&&d.pens.at(sketch.id).contains(curve)?d.pens.at(sketch.id).at(curve):"white";
            const bool text=key.starts_with("text:");
            const std::string text_color=key.ends_with(":green")?"#4DD811":key.ends_with(":yellow")?"#F5CD50":key.ends_with(":red")?"#FF0000":"#FFFFFF";
            edge.reference={instance.id,"symbol:"+instance.id,{}};edge.overlay=true;edge.exact_spline.reset();
            edge.dash_dot=edge.construction;edge.infinite=false;
            edge.color=text?text_color:edge.construction||pen=="green"?"#4DD811":pen=="yellow"?"#F5CD50":"#FFFFFF";
            result.edges.push_back(std::move(edge));
        }
    }
    return result;
}
Definition projection_method() {
    Definition d;d.id="ze:projection-method";d.name="ZE-PROJECTION-METHOD";
    d.default_variant="first_angle";d.variant_source="drawing.projection_method";
    // ISO 5456-2 figures 4 and 7: the cone widens to the right in both variants.
    for(bool first:{true,false}) {
        const std::string key=first?"first_angle":"third_angle";
        auto& sketch=d.sketches.emplace_back(sketcher::Sketch::create_default());
        sketch.id=d.id+":"+key;sketch.name=key;
        const double left=first?-7.2:1.2, right=left+6., center=first?4.2:-4.2;
        const auto point=[&](std::string id,double x,double y) {
            id=key+":"+id;sketch.points.push_back({id,x,y,true,false});return id;
        };
        const auto a=point("cone-small-bottom",left,-1.5), b=point("cone-large-bottom",right,-3.);
        const auto c=point("cone-large-top",right,3.), e=point("cone-small-top",left,1.5);
        const auto segment=[&](std::string id,const std::string& p,const std::string& q,bool axis) {
            id=key+":"+id;sketch.segments.push_back({id,p,q,axis,false});
            d.pens[sketch.id][id]="green";
        };
        segment("cone-bottom",a,b,false);segment("cone-large",b,c,false);
        segment("cone-top",c,e,false);segment("cone-small",e,a,false);
        const auto center_id=point("circle-center",center,0);
        for(bool outer:{true,false}) {
            const std::string id=key+(outer?":outer-circle":":inner-circle");
            sketch.circles.push_back({id,center_id,outer?3.:1.5,false});
            d.pens[sketch.id][id]="green";
        }
        segment("cone-axis",point("cone-axis-left",left-.6,0),point("cone-axis-right",right+.6,0),true);
        segment("circle-axis-left",point("axis-left",center-3.6,0),center_id,true);
        segment("circle-axis-right",center_id,point("axis-right",center+3.6,0),true);
        segment("circle-axis-bottom",point("axis-bottom",center,-3.6),center_id,true);
        segment("circle-axis-top",center_id,point("axis-top",center,3.6),true);
        d.variants[key].sketches={sketch.id};
    }
    d.validate();return d;
}
}
