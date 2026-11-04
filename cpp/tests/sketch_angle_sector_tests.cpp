#include "sketch_mechanism_study.hpp"
using namespace zima;
using namespace zima::test::mechanism;
namespace fs=std::filesystem;
int main() {
    unsigned passed{},failed{},edits{},drags{};
    for(double rotation:{0.,90.,180.,270.})for(bool order:{false,true})
    for(bool reversed:{false,true})for(bool external:{false,true})
    for(double label:{20.,110.,200.,290.})for(int state=0;state<3;++state)
    for(bool negative:{false,true})try {
        auto s=Sketch::create_default();
        const auto xy=[&](double radius,double angle){const double a=(angle+rotation)*radians;return XY{radius*std::cos(a),radius*std::sin(a)};};
        const auto base=xy(30,0),tip=xy(25,40);
        const auto pivot=s.add_point(0,0);s.find_point(pivot)->fixed=true;
        std::string reference;
        if(external) {
            auto r=Sketch::create_external_reference(ExternalReferenceKind::Edge);
            r.source_document_id="angle-datum";r.source_owner_id="fixed-line";r.source_semantic_key="datum";
            r.infinite=true;r.cached_points=reversed?std::vector<XY>{base,{0,0}}:std::vector<XY>{{0,0},base};
            reference=r.id;s.external_references.push_back(r);
        }else {
            reference=reversed?s.add_segment(base[0],base[1],0,0):s.add_segment(0,0,base[0],base[1]);
            for(const auto& line:s.segments)for(const auto& id:{line.first_point_id,line.second_point_id})s.find_point(id)->fixed=true;
        }
        const auto arm=s.add_segment(0,0,tip[0],tip[1]);
        const auto endpoint=s.segments.back().second_point_id;
        auto length=s.create_segment_dimension(arm);length.locked=true;s.apply_dimension(length);
        auto angle=s.create_line_pair_dimension(external||!order?reference:arm,external||!order?arm:reference,DimensionKind::AngleBetween);
        if(external)angle.angle_presentation_reversed=order;
        angle.placement=xy(18,label);
        auto preview=s;preview.dimensions.push_back(angle);
        const auto mesh=preview.viewer_mesh();
        const auto offered=std::ranges::find_if(mesh.dimensions,[&](const auto& d){return d.reference.semantic_key=="dimension:"+angle.id;});
        require(offered!=mesh.dimensions.end()&&offered->value>0,"Automatic angle borrowed a negative presentation sweep");
        const double base_value=angle.value;
        angle.value=offered->value;
        angle.angle_sector=std::abs(angle.value-base_value)<1e-7?0:1;
        if(negative)angle.value=-angle.value;
        angle.driving=state!=0;angle.locked=state==2;
        const auto supports=s.external_references;
        s.apply_dimension(angle);
        const auto equation=[&] {
            const auto* p=s.find_point(endpoint);
            require(std::abs(std::hypot(p->x,p->y)-25)<1e-7,"Angular edit violated the locked arm length");
            require(s.external_references==supports,"Angular edit changed its external support");
            const auto packet=s.viewer_mesh();
            const auto d=std::ranges::find_if(packet.dimensions,[&](const auto& v){return v.reference.semantic_key=="dimension:"+angle.id;});
            require(d!=packet.dimensions.end()&&(negative?d->value<0:d->value>0),"Angular value changed its authored sign");
            if(state) {
                const auto stored=std::ranges::find(s.dimensions,angle.id,&SketchDimension::id);
                const double dx=base[0]*(reversed?-1:1),dy=base[1]*(reversed?-1:1);
                const double dot=std::clamp((dx*p->x+dy*p->y)/(30*25),-1.,1.);
                const double actual=std::acos(dot)/radians;
                const double value=std::acos(std::cos(stored->value*radians))/radians;
                require(std::min(std::abs(actual-value),angle.angle_sector==1?std::abs(actual-(180-value)):1000.)<1e-7,"Angular geometric equation failed");
            }
        };
        equation();
        auto authored=part(s);s=authored.sketches.front();
        document::DocumentSession history(std::move(authored));
        const auto publish=[&] {auto next=history.document();next.sketches.front()=s;history.commit_metadata(std::move(next));};
        for(double delta:{6.,-4.}) {
            const auto before=s.serialized();
            static_cast<void>(s.set_dimension_value(angle.id,angle.value+(negative?-delta:delta)));
            if(!state)require(s.serialized()==before,"Reference angle drove geometry");
            else {require(s.serialized()!=before,"Driving angle did not edit");++edits;publish();}
            equation();
        }
        for(double a:{55.,30.,45.}) {
            const auto before=s.serialized();const auto p=xy(25,a);
            const bool moved=s.move_point(endpoint,p[0],p[1]);
            if(state==2)require(!moved&&s.serialized()==before,"Locked angle escaped during dragging");
            else {require(moved&&std::hypot(s.find_point(endpoint)->x-p[0],s.find_point(endpoint)->y-p[1])<1e-7,"Editable angular endpoint did not follow its reachable cursor");++drags;publish();}
            equation();
        }
        const auto final=history.document().sketches.front().serialized();
        require(history.undo(),"Angular history has no Undo");require(history.redo(),"Angular history has no Redo");
        require(history.document().sketches.front().serialized()==final,"Angular Undo/Redo changed identity");
        const auto dir=fs::temp_directory_path()/("zima-angle-"+s.id);
        require(fs::create_directory(dir),"Cannot create angular native fixture directory");
        const auto file=dir/"angle.prtz";history.document().save(file);
        require(document::PartDocument::load(file).sketches.front().serialized()==final,"Angular native reopen changed intent or geometry");
        require(fs::remove(file)&&fs::remove(dir),"Cannot retire angular fixture");
        ++passed;
    }catch(const std::exception& e) {
        ++failed;std::cerr<<"Angle rotation="<<rotation<<" order="<<order<<" reversed="<<reversed<<" external="<<external<<" label="<<label<<" state="<<state<<" negative="<<negative<<": "<<e.what()<<'\n';
    }
    std::cout<<"Angle sectors: "<<passed<<" passed, "<<failed<<" failed; "<<edits<<" numeric edits, "<<drags<<" moving drags\n";
    return failed?1:0;
}
