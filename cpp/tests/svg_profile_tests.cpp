#include "profile_solid_fixture.hpp"
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <filesystem>
#include <zima/sketcher/svg_geometry.hpp>
#include <zima/sketcher/vector_profile.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/profile_status.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <QByteArray>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace zima;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
sketcher::TemplateImage fixture(const std::string& shapes) {
    sketcher::TemplateImage image;image.id="svg-profile";image.name="embedded.svg";image.format="svg";
    image.pixel_width=image.width=20;image.pixel_height=image.height=20;image.template_coordinates=false;image.modeling_geometry=true;
    image.data_base64=QByteArray::fromStdString("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 20 20\">"+shapes+"</svg>").toBase64().toStdString();return image;
}
int main(){try{
    kernel::OcctKernel kernel;
    const std::vector<std::pair<std::string,double>> cases{
        {"<rect x='2' y='2' width='10' height='10'/>",100},
        {"<rect width='20' height='20' fill='rgba(0,0,0,0)'/><rect x='2' y='2' width='10' height='10'/>",100},
        {"<g color='transparent'><rect width='20' height='20' fill='currentColor'/></g><rect x='2' y='2' width='10' height='10'/>",100},
        {"<g opacity='0'><rect width='20' height='20'/></g><g fill-opacity='.3'><rect x='2' y='2' width='10' height='10'/></g>",100},
        {"<rect x='2' y='2' width='10' height='10'/><rect x='7' y='2' width='10' height='10'/>",150},
        {"<path fill-rule='evenodd' d='M2 2h10v10H2Z M5 5h4v4H5Z'/>",84},
        {"<path d='M2 2h10v10H2Z M5 5h4v4H5Z'/>",100},
        {"<path d='M2 2h10v10H2Z M5 5v4h4V5Z'/>",84},
        {"<circle cx='10' cy='10' r='4'/>",16*std::numbers::pi},
        {"<g transform='translate(3 4) rotate(20) scale(2 1)'><ellipse cx='3' cy='2' rx='3' ry='2'/></g>",12*std::numbers::pi},
        {"<path d='M7 10 A3 3 0 0 1 13 10 A3 3 0 0 1 7 10z'/>",9*std::numbers::pi},
        {"<rect x='2' y='2' width='10' height='6' rx='2'/>",44+4*std::numbers::pi}
    };
    for(const auto& [svg,area]:cases) {
        auto image=fixture(svg);auto sketch=sketcher::Sketch::create_default();sketch.reference_images.push_back(image);
        require(document::profile_status(sketch)==document::ProfileStatus::Closed,"SVG was not accepted as a closed profile");
        const auto authored=sketch.serialized();auto doc=document::PartDocument::create_default();doc.sketches.push_back(sketch);
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);doc.history.push_back(feature);
        for(double height:{2.,5.}) {
            doc.history[0].extrusion.height=height;const auto start=std::chrono::steady_clock::now();const auto operations=doc.kernel_operations();
            const auto result=kernel.evaluate_history(operations);
            require(result.size()==1&&result.back().calculation_errors.empty(),"SVG extrusion calculation failed");
            if(std::abs(result.back().volume-area*height)>1e-5)throw std::runtime_error("SVG volume mismatch: "+std::to_string(result.back().volume)+" expected "+std::to_string(area*height)+" source "+svg);
            require(doc.sketches[0].serialized()==authored,"SVG extrusion changed the authored image");
            std::cout<<"SVG h="<<height<<" area="<<area<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
        }
        const auto restored=sketcher::Sketch::from_serialized(authored);require(restored.reference_images==sketch.reference_images,"SVG source or solid participation changed on reopening");
        sketch.reference_images[0].modeling_geometry=false;require(document::profile_status(sketch)==document::ProfileStatus::Empty,"Reference-only SVG still participates in a solid");
    }
    // The retained image is also a real Boolean tool, with stable native data.
    for(const auto mode:{document::CombineMode::Add,document::CombineMode::Subtract}) {
        auto doc=document::PartDocument::create_default();doc.history.push_back(test::rectangular_feature(doc,{30,30,10}));
        auto image=fixture("<circle cx='10' cy='10' r='4'/>");image.x=image.y=-10;
        auto sketch=sketcher::Sketch::create_default();sketch.reference_images.push_back(image);doc.sketches.push_back(sketch);
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);feature.extrusion.height=8;feature.combine_mode=mode;
        doc.history.push_back(feature);auto bodies=kernel.evaluate_history(doc.kernel_operations());
        require(bodies.back().calculation_errors.empty(),"SVG Boolean calculation failed");
        const double area=16*std::numbers::pi,expected=9000+(mode==document::CombineMode::Add?3:-5)*area;
        require(std::abs(bodies.back().volume-expected)<1e-5,"Retained SVG Boolean changed the expected material volume");
        document::DocumentSession session(doc,bodies);auto changed=doc;changed.history.back().extrusion.height=9;
        session.commit(changed,kernel.evaluate_history(changed.kernel_operations()));session.undo();
        require(session.document().serialized()==doc.serialized(),"SVG solid Undo changed source data");session.redo();
        const auto file=std::filesystem::temp_directory_path()/("zima-svg-boolean-"+doc.document_id+".prtz");
        session.document().save(file,session.calculated_boundaries());std::vector<kernel::BodyResult> restored;
        const auto reopened=document::PartDocument::load(file,&restored);
        require(reopened.serialized()==session.document().serialized(),"SVG solid native save lost the retained image definition");
        require(!restored.empty()&&document::serialize_body_result(restored.back())==document::serialize_body_result(session.calculated_boundaries().back()),"SVG solid native save lost result reference identity");
    }
    // Crossing subpaths must either be interpreted with the authored fill rule
    // or be rejected before an incorrect solid can be committed.
    for(const auto& [rule,area]:std::vector<std::pair<std::string,double>>{{"nonzero",150.},{"evenodd",100.}}) {
        auto sketch=sketcher::Sketch::create_default();
        sketch.reference_images.push_back(fixture("<path fill-rule='"+rule+"' d='M2 2h10v10H2Z M7 2h10v10H7Z'/>"));
        auto doc=document::PartDocument::create_default();doc.sketches.push_back(sketch);
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);feature.extrusion.height=2;doc.history.push_back(feature);
        const auto authored=doc.serialized();std::vector<kernel::BodyResult> result;bool rejected=false;
        try {result=kernel.evaluate_history(doc.kernel_operations());}catch(const std::exception&){rejected=true;}
        if(!rejected&&!result.empty()&&result.back().calculation_errors.empty())
            require(std::abs(result.back().volume-2*area)<1e-5,"Crossing SVG subpaths silently changed the fill rule");
        require(doc.serialized()==authored,"Crossing SVG validation changed the authored image");
        std::cout<<"Crossing SVG "<<rule<<": "<<(rejected||result.empty()||!result.back().calculation_errors.empty()?"explicit rejection":"correct volume")<<'\n';
    }
    // Homogeneous degree elevation must preserve circular arcs exactly.
    auto image=fixture("<circle cx='10' cy='10' r='4'/>");const auto fills=sketcher::exact_svg_fills(image);
    for(const auto& curve:fills[0].boundaries)for(std::size_t i=0;i+3<curve.poles.size();i+=3)for(double t:{.1,.3,.5,.9}) {
        const double u=1-t;const std::array b{u*u*u,3*u*u*t,3*u*t*t,t*t*t};double x=0,y=0,w=0;
        for(int k=0;k<4;++k){const double v=b[k]*curve.weights[i+k];x+=v*curve.poles[i+k].x;y+=v*curve.poles[i+k].y;w+=v;}
        require(std::abs(std::hypot(x/w-10,y/w-10)-4)<1e-10,"SVG circular arc was approximated by polynomial cubics");
    }
    for(const auto* unsupported:{"<path stroke='black' d='M0 0L10 10'/>","<image href='external.png'/>","<g clip-path='url(#clip)'><rect width='10' height='10'/></g>"}) {
        bool rejected=false;try{static_cast<void>(sketcher::exact_svg_fills(fixture(unsupported)));}catch(const std::exception&){rejected=true;}require(rejected,"Unsupported SVG silently changed the painting");
    }
    std::cout<<"SVG exact regions, winding, overlaps, rational arcs, embedded persistence and reference-only mode passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
