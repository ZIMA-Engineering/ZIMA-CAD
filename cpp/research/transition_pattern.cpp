#include "transition_pattern.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <stdexcept>
namespace zima::research::transition {
namespace {
bool same(Vec3 a,Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z)<1e-7;}
bool same_edge(Vec3 a,Vec3 b,Vec3 c,Vec3 d){return (same(a,c)&&same(b,d))||(same(a,d)&&same(b,c));}
template<class Faces> Pattern collect(const Faces& faces,const std::vector<Fold>& folds) {
    struct Edge {Vec3 a,b,flat_a,flat_b;int count{1};};std::vector<Edge> edges;
    for(const auto& face:faces)for(std::size_t i=0;i<face.folded.size();++i) {
        const auto j=(i+1)%face.folded.size();const auto a=face.folded[i],b=face.folded[j];
        auto found=std::find_if(edges.begin(),edges.end(),[&](const Edge& edge){return same_edge(a,b,edge.a,edge.b);});
        if(found==edges.end())edges.push_back({a,b,face.unfolded[i],face.unfolded[j]});
        else {
            if(!same_edge(found->flat_a,found->flat_b,face.unfolded[i],face.unfolded[j]))throw std::runtime_error("Discontinuous unfolded edge");
            ++found->count;
        }
    }
    Pattern result;
    for(const auto& edge:edges) {
        if(edge.count==1){result.push_back({edge.flat_a,edge.flat_b,PatternRole::Outline});continue;}
        if(edge.count!=2)throw std::runtime_error("Non-manifold transition edge");
        const auto fold=std::find_if(folds.begin(),folds.end(),[&](const Fold& f){return same_edge(edge.a,edge.b,f.first,f.second);});
        if(fold==folds.end())throw std::runtime_error("Missing fold relation");
        // Presentation-only numerical angular tolerance. Never alters model sides.
        if(std::abs(fold->signed_angle_radians)>1e-8)
            result.push_back({edge.flat_a,edge.flat_b,PatternRole::BendAxis,fold->signed_angle_radians});
    }return result;
}
void pair(std::ostream& out,int code,const auto& value){out<<code<<'\n'<<value<<'\n';}
}
Pattern pattern(const Result& r){return r.valid()?collect(r.facets,r.folds):Pattern{};}
Pattern pattern(const HalfResult& r){return r.valid()?collect(r.faces,r.folds):Pattern{};}
void export_pattern(const Pattern& lines,const std::filesystem::path& path) {
    const bool svg=path.extension()==".svg";if(!svg&&path.extension()!=".dxf")throw std::runtime_error("Unsupported study export format");
    if(lines.empty())throw std::runtime_error("Empty study pattern");
    double xmin=1e100,ymin=1e100,xmax=-1e100,ymax=-1e100;
    for(const auto& line:lines)for(auto p:{line.first,line.second}) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.z)>1e-7)throw std::runtime_error("Invalid study pattern");
        xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);
    }
    std::ofstream out(path);if(!out)throw std::runtime_error("Cannot open study export");out.imbue(std::locale::classic());out<<std::setprecision(17);
    if(svg) {
        out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<xmax-xmin+10<<"mm\" height=\""<<ymax-ymin+10<<"mm\" viewBox=\""<<xmin-5<<' '<<-ymax-5<<' '<<xmax-xmin+10<<' '<<ymax-ymin+10<<"\">\n<title>Surface study: external dimensions, no bend allowance</title>\n";
        for(const auto& line:lines)out<<"<path data-role=\""<<(line.role==PatternRole::BendAxis?"bend-axis":"outline")<<"\" d=\"M "<<line.first.x<<' '<<-line.first.y<<" L "<<line.second.x<<' '<<-line.second.y<<"\" fill=\"none\" stroke=\"black\" stroke-width=\""<<(line.role==PatternRole::BendAxis?.18:.5)<<"\""<<(line.role==PatternRole::BendAxis?" stroke-dasharray=\"9 2 1 2\"":"")<<"/>\n";
        out<<"</svg>\n";
    }else {
        pair(out,999,"Surface study only: external dimensions, no bend allowance");
        pair(out,0,"SECTION");pair(out,2,"HEADER");pair(out,9,"$ACADVER");pair(out,1,"AC1015");pair(out,9,"$INSUNITS");pair(out,70,4);pair(out,0,"ENDSEC");
        pair(out,0,"SECTION");pair(out,2,"TABLES");pair(out,0,"TABLE");pair(out,2,"LTYPE");pair(out,70,2);
        for(bool axis:{false,true}) {
            pair(out,0,"LTYPE");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbLinetypeTableRecord");pair(out,2,axis?"CENTER":"CONTINUOUS");pair(out,70,0);pair(out,3,axis?"Bend axis":"Outline");pair(out,72,65);pair(out,73,axis?4:0);pair(out,40,axis?14:0);
            if(axis)for(double length:{9.,-2.,1.,-2.}){pair(out,49,length);pair(out,74,0);}
        }
        pair(out,0,"ENDTAB");pair(out,0,"TABLE");pair(out,2,"LAYER");pair(out,70,2);
        for(bool axis:{false,true}){pair(out,0,"LAYER");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbLayerTableRecord");pair(out,2,axis?"BEND_AXES":"OUTLINE");pair(out,70,0);pair(out,62,axis?2:7);pair(out,6,axis?"CENTER":"CONTINUOUS");pair(out,370,axis?18:50);}
        pair(out,0,"ENDTAB");pair(out,0,"ENDSEC");pair(out,0,"SECTION");pair(out,2,"ENTITIES");
        for(const auto& line:lines){const bool axis=line.role==PatternRole::BendAxis;pair(out,0,"LINE");pair(out,100,"AcDbEntity");pair(out,8,axis?"BEND_AXES":"OUTLINE");pair(out,6,axis?"CENTER":"CONTINUOUS");pair(out,370,axis?18:50);pair(out,100,"AcDbLine");pair(out,10,line.first.x);pair(out,20,line.first.y);pair(out,30,0);pair(out,11,line.second.x);pair(out,21,line.second.y);pair(out,31,0);}
        pair(out,0,"ENDSEC");pair(out,0,"EOF");
    }
    out.flush();if(!out)throw std::runtime_error("Cannot finish study export");
}
}
