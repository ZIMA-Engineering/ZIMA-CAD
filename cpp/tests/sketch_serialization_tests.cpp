#include "dxf_export_test_support.hpp"
#include <zima/symbols/definition.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <bit>
using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>double timed(F action){const auto start=std::chrono::steady_clock::now();for(int i=0;i<20;++i)action();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
}
int main(){try {
    auto sketch=test::dxf_curve_fixture();sketch.name="Žluťoučký / Ø / Пример";
    auto point=sketcher::Sketch::create_point(-0.0,500);sketch.points.push_back(point);
    auto definition=symbols::projection_method();sketcher::SymbolInstance symbol;
    symbol.id="symbol-test";symbol.definition=definition.serialized();symbol.variant=definition.default_variant;sketch.symbols.push_back(symbol);
    for(int i=0;i<128;++i)static_cast<void>(sketch.add_segment(200+i*3,100,200+i*3,120));
    const auto text=sketch.serialized();const auto packet=nlohmann::json::parse(text);
    require(sketch.serialized_json().dump(2)==text,"Direct JSON changed the existing native Sketch representation");
    const auto direct=sketcher::Sketch::from_serialized_json(packet),parsed=sketcher::Sketch::from_serialized(text);
    require(direct.serialized()==parsed.serialized()&&direct.serialized()==text,"Direct Sketch packet lost native fields");
    require(std::bit_cast<std::uint64_t>(direct.find_point(point.id)->x)==std::bit_cast<std::uint64_t>(-0.0),"Direct Sketch packet normalized authored signed zero");
    auto dangling=packet;dangling["segments"][0]["first"]="missing-reference";
    // Both reader entry points retain format, mandatory-field and geometry checks.
    for(auto bad:{nlohmann::json::object(),nlohmann::json{{"format","invalid"},{"version",35}},dangling}) {
        for(bool direct:{false,true}) {
            bool rejected=false;try{if(direct)static_cast<void>(sketcher::Sketch::from_serialized_json(bad));else static_cast<void>(sketcher::Sketch::from_serialized(bad.dump()));}catch(const std::exception&){rejected=true;}
            require(rejected,"Malformed Sketch packet bypassed validation");
        }
    }
    std::size_t observed=0;
    const auto old_write=timed([&]{observed+=nlohmann::json::parse(sketch.serialized()).size();});
    const auto new_write=timed([&]{observed+=sketch.serialized_json().size();});
    const auto old_read=timed([&]{observed+=sketcher::Sketch::from_serialized(packet.dump()).points.size();});
    const auto new_read=timed([&]{observed+=sketcher::Sketch::from_serialized_json(packet).points.size();});
    require(observed>0,"Serialization benchmark did no work");
    std::cout<<"Sketch native embedding, 20 iterations: text_write_ms="<<old_write<<" direct_write_ms="<<new_write<<" text_read_ms="<<old_read<<" direct_read_ms="<<new_read<<" bytes="<<text.size()<<"\n";
    std::cout<<"Exact native Sketch packets, identities, Unicode, rational curves, Symbol and signed zero passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
