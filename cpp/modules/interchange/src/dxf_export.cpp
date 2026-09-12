#include <zima/interchange/dxf.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <numbers>
#include <unordered_set>

namespace zima::interchange {
namespace {
void pair(std::ostream& output,int code,const auto& value){output<<code<<'\n'<<value<<'\n';}
void entity(std::ostream& output,const char* type,const char* subclass,bool construction) {
    pair(output,0,type);pair(output,100,"AcDbEntity");pair(output,8,construction?"CONSTRUCTION":"PROFILE");pair(output,100,subclass);
}
void point(std::ostream& output,int code,double x,double y,double z=0) {
    pair(output,code,x);pair(output,code+10,y);pair(output,code+20,z);
}
double angle(double value) {const double turn=2*std::numbers::pi;value=std::fmod(value,turn);return value<0?value+turn:value;}
void ellipse(std::ostream& output,const sketcher::SketchPoint& center,double rx,double ry,double rotation,
        bool reversed,bool construction,double start,double end,bool full) {
    const double sign=reversed?-1:1;double ax=rx*std::cos(rotation),ay=rx*std::sin(rotation),ratio=ry/rx;
    if(ry>rx) {
        // DXF requires the longer axis. Preserve the same oriented parameterization
        // by rotating the basis one quarter turn and shifting both parameters.
        ax=-sign*ry*std::sin(rotation);ay=sign*ry*std::cos(rotation);ratio=rx/ry;
        start-=std::numbers::pi/2;end-=std::numbers::pi/2;
    }
    entity(output,"ELLIPSE","AcDbEllipse",construction);
    point(output,10,center.x,center.y);point(output,11,ax,ay);point(output,210,0,0,sign);
    pair(output,40,ratio);pair(output,41,full?0:angle(start));pair(output,42,full?2*std::numbers::pi:angle(end));
}
}
void validate_dxf_export(const sketcher::Sketch& sketch) {
    sketch.validate();
    if(!sketch.texts.empty()||!sketch.corner_radii.empty())throw DxfExportError("DXF export does not yet support Sketch text or corner fillets.");
}
void export_dxf(const std::filesystem::path& path,const sketcher::Sketch& sketch) {
    validate_dxf_export(sketch);
    std::ofstream output(path);if(!output)throw std::runtime_error("Nelze vytvořit DXF soubor");
    output.imbue(std::locale::classic());output<<std::setprecision(17);
    pair(output,0,"SECTION");pair(output,2,"HEADER");
    pair(output,9,"$ACADVER");pair(output,1,"AC1015");pair(output,9,"$INSUNITS");pair(output,70,4);
    pair(output,0,"ENDSEC");pair(output,0,"SECTION");pair(output,2,"ENTITIES");
    std::unordered_set<std::string> used_points;
    const auto remember=[&](const auto&... ids){(used_points.insert(ids),...);};
    for(const auto& line:sketch.segments) {
        const auto* first=sketch.find_point(line.first_point_id);const auto* second=sketch.find_point(line.second_point_id);
        remember(first->id,second->id);
        entity(output,line.centerline?"XLINE":"LINE",line.centerline?"AcDbXline":"AcDbLine",line.construction);
        point(output,10,first->x,first->y);
        if(line.centerline) {
            const auto dx=second->x-first->x,dy=second->y-first->y,length=std::hypot(dx,dy);
            point(output,11,dx/length,dy/length);
        } else point(output,11,second->x,second->y);
    }
    for(const auto& circle:sketch.circles) {
        const auto* center=sketch.find_point(circle.center_point_id);remember(center->id);
        entity(output,"CIRCLE","AcDbCircle",circle.construction);point(output,10,center->x,center->y);pair(output,40,circle.radius);
    }
    for(const auto& arc:sketch.arcs) {
        const auto* center=sketch.find_point(arc.center_point_id);remember(center->id,arc.start_point_id,arc.end_point_id);
        entity(output,"ARC","AcDbCircle",arc.construction);point(output,10,center->x,center->y);pair(output,40,arc.radius);
        pair(output,100,"AcDbArc");pair(output,50,angle(arc.start_angle)*180/std::numbers::pi);pair(output,51,angle(arc.end_angle)*180/std::numbers::pi);
    }
    for(const auto& value:sketch.ellipses) {
        remember(value.center_point_id,value.major_point_id,value.minor_point_id);
        ellipse(output,*sketch.find_point(value.center_point_id),value.major_radius,value.minor_radius,value.rotation,
            value.reversed,value.construction,0,2*std::numbers::pi,true);
    }
    for(const auto& value:sketch.elliptical_arcs) {
        remember(value.center_point_id,value.major_point_id,value.minor_point_id,value.start_point_id,value.end_point_id);
        ellipse(output,*sketch.find_point(value.center_point_id),value.major_radius,value.minor_radius,value.rotation,
            value.reversed,value.construction,value.start_parameter,value.end_parameter,false);
    }
    for(const auto& spline:sketch.bsplines) {
        for(const auto& id:spline.control_point_ids)used_points.insert(id);
        const auto curve=sketcher::sketch_curve_geometry(sketch,spline.id);
        const bool rational=std::ranges::any_of(curve.weights,[](double weight){return weight!=1;});
        entity(output,"SPLINE","AcDbSpline",spline.construction);point(output,210,0,0,1);
        pair(output,70,8|(spline.closed?1:0)|(rational?4:0)|(curve.degree==1?16:0));pair(output,71,curve.degree);
        pair(output,72,curve.knots.size());pair(output,73,curve.poles.size());pair(output,74,0);
        for(double knot:curve.knots)pair(output,40,knot);
        if(rational)for(double weight:curve.weights)pair(output,41,weight);
        for(const auto& pole:curve.poles)point(output,10,pole.x,pole.y,pole.z);
    }
    // Centers and control vertices are editing handles, not standalone entities.
    // Curve-support snapshots are also not additional visible geometry.
    for(const auto& value:sketch.points)if(!used_points.contains(value.id)) {
        entity(output,"POINT","AcDbPoint",value.construction);point(output,10,value.x,value.y);
    }
    pair(output,0,"ENDSEC");pair(output,0,"EOF");output.close();
    if(!output)throw std::runtime_error("Zápis DXF souboru selhal");
}
}
