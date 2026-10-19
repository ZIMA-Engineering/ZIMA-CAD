#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

int main(int argc,char** argv) {try {
    using namespace zima;
    if(argc==4&&std::string(argv[1])=="--sketch-template") {
        const auto input=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        const auto sketch=drawing::load_template_sketch(input,[](auto&){});
        drawing::save_template_sketch(sketch,output);
        const auto restored=drawing::load_template_sketch(output,[](auto&){});
        if(restored.serialized()!=sketch.serialized())throw std::runtime_error("Template refresh changed native geometry or references");
        std::cout<<sketch.symbols.size()<<" native template anchors saved and reopened: "<<output.string()<<'\n';return 0;
    }
    if(argc!=3)throw std::invalid_argument("Expected prepared input and native output paths");
    auto part=document::PartDocument::load(std::filesystem::u8path(argv[1]));
    std::size_t anchors{};
    for(const auto& sketch:part.sketches)for(const auto& symbol:sketch.symbols) {
        const auto* point=sketch.find_point(symbol.anchor_point_id);
        if(!point||point->x!=symbol.x||point->y!=symbol.y)
            throw std::runtime_error("Symbol anchor does not match its authored placement");
        ++anchors;
    }
    const auto authored=part.serialized();
    kernel::OcctKernel engine;const auto bodies=engine.evaluate_history(part.kernel_operations());
    const auto output=std::filesystem::u8path(argv[2]);part.save(output,bodies);
    std::vector<kernel::BodyResult> restored;const auto reopened=document::PartDocument::load(output,&restored);
    if(reopened.serialized()!=authored||restored.size()!=bodies.size())
        throw std::runtime_error("Native refresh changed authored data or calculated boundaries");
    for(std::size_t i=0;i<bodies.size();++i)
        if(document::serialize_body_result(bodies[i])!=document::serialize_body_result(restored[i]))
            throw std::runtime_error("Native refresh lost geometry or references");
    std::cout<<anchors<<" anchors calculated, saved and reopened: "<<output.string()<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
