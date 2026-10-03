#include <zima/document/relation_program.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
#include <stdexcept>

using namespace zima::document;
void check(bool value) { if(!value)throw std::runtime_error("Relation program assertion failed"); }
template<class F> void rejects(F f,int line=0) {
    try {f();}catch(const RelationError& e){check(!line||e.line==line);return;}
    throw std::runtime_error("Invalid relation accepted");
}

void unit_conversion() {
    const std::array<double,3> scales{1./25.4,std::numbers::pi/180.,1000.};
    const auto scale=[&](const std::array<int,3>& units){double value=1;for(int i=0;i<3;++i)value*=std::pow(scales[i],units[i]);return value;};
    RelationInputs inputs{{"d1",{{50.,{1,0,0}},true}}, {"d2",{{100.,{1,0,0}},true}},
        {"d3",{{1.,{1,0,0}},true}}, {"d4",{{3.,{}},true}}, {"d5",{{30.,{0,1,0}},true}},
        {"material",{{std::string("Steel # 25.4")},true}}, {"model.mass",{{12.,{0,0,1}},false,true}}};
    const std::string source=
        "# preserve comments: d1 = 25.4 mm\n"
        "if d1 > 100\n d3 = d1 * 2 + 10 # multiplier versus length\n"
        "elseif d1 > 20 and material == \"Steel # 25.4\"\n d3 = round(d2 / 3, 1)\n"
        "else\n d3 = ceil(d2 / 3)\nendif\n"
        "d4 = 3\nlength = floor(d2) + 0.125\n"
        "area = d1 * d2\nvolume = d1^3\nroot = sqrt(d1*d1)\n"
        "ratio = d1 / d2\ntrig = sind(d5) + cos(d5)\n"
        "inverse = asind(0.5) + atan2d(d1,d2)\n"
        "mass = model.mass\ndensity = model.mass / (d1^3)\n"
        "stock = \"⌀\" & d1 & \" x \" & d2 & \" # 25.4\"\n"
        "d5 = 30\n";
    const auto converted=RelationProgram(source).convert_units(inputs,scales);
    check(converted.source.find("# preserve comments: d1 = 25.4 mm")!=std::string::npos);
    check(converted.source.find("# multiplier versus length")!=std::string::npos);
    check(converted.source.find("\"Steel # 25.4\"")!=std::string::npos);
    check(converted.outputs.at("area").units==std::array<int,3>{2,0,0});
    check(converted.outputs.at("density").units==std::array<int,3>{-3,0,1});
    for(const double length:{5.,50.,500.}) {
        inputs.at("d1").value.data=length;auto new_inputs=inputs;
        for(auto& [name,input]:new_inputs)if(auto* number=std::get_if<double>(&input.value.data))*number*=scale(input.value.units);
        const auto before=RelationProgram(source).evaluate(inputs);
        const auto after=RelationProgram(converted.source).evaluate(new_inputs);
        check(before.size()==after.size());
        for(const auto& [name,value]:before) {
            const auto& next=after.at(name);check(value.units==next.units&&value.data.index()==next.data.index());
            if(const auto* number=std::get_if<double>(&value.data)) {
                const double expected=*number*scale(value.units),actual=std::get<double>(next.data);
                check(std::abs(actual-expected)<1e-11*std::max(1.,std::abs(expected)));
            } else check(value.data==next.data);
        }
    }
    auto new_inputs=inputs;for(auto& [name,input]:new_inputs)if(auto* number=std::get_if<double>(&input.value.data))*number*=scale(input.value.units);
    const auto fixed=RelationProgram("SHEETMETAL_THICKNESS = d1 / 10\nd3 = SHEETMETAL_THICKNESS * 2").convert_units(inputs,scales,{"SHEETMETAL_THICKNESS"});
    const auto fixed_values=RelationProgram(fixed.source).evaluate(new_inputs);
    check(std::abs(std::get<double>(fixed_values.at("SHEETMETAL_THICKNESS").data)-50)<1e-12);
    check(std::abs(std::get<double>(fixed_values.at("d3").data)-100*scales[0])<1e-12);
    const auto zero=RelationProgram("d3 = -0").convert_units(inputs,scales);
    check(std::signbit(std::get<double>(RelationProgram(zero.source).evaluate(new_inputs).at("d3").data)));
    const auto inactive=RelationProgram("if false\nd3 = 1 / 0\nelse\nd3 = 10\nendif").convert_units(inputs,scales);
    check(std::abs(std::get<double>(RelationProgram(inactive.source).evaluate(new_inputs).at("d3").data)-10*scales[0])<1e-14);
    rejects([&]{RelationProgram("if false\nx = d1 + d5\nendif").convert_units(inputs,scales);},2);
    rejects([&]{RelationProgram("if true\nx = d1\nelse\nx = d5\nendif").convert_units(inputs,scales);},4);
    rejects([&]{RelationProgram("x = d1^d4").convert_units(inputs,scales);},1);
    check(RelationProgram(source).convert_units(inputs,{1,1,1}).source==source);
    auto repeated=source;
    for(int i=0;i<6;++i) {
        const bool outward=i%2==0;const auto factors=outward?scales:std::array<double,3>{1/scales[0],1/scales[1],1/scales[2]};
        repeated=RelationProgram(repeated).convert_units(outward?inputs:new_inputs,factors).source;
        const auto actual=RelationProgram(repeated).evaluate(outward?new_inputs:inputs);
        const auto expected=RelationProgram(source).evaluate(inputs);
        check(std::abs(std::get<double>(actual.at("d3").data)-(outward?scales[0]:1)*std::get<double>(expected.at("d3").data))<1e-10);
    }
}
int main() {
    try {
        unit_conversion();
        RelationInputs inputs{{"d1",{{50.,{1,0,0}},true}}, {"d2",{{100.,{1,0,0}},true}},
            {"d3",{{1.,{1,0,0}},true}}, {"angle",{{30.,{0,1,0}},true}},
            {"material",{{std::string("Ocel")},true}}, {"model.mass",{{12.,{0,0,1}},false,true}}};
        auto values=RelationProgram("# text and dependencies\nstock = \"⌀\" & d1 & \"x\" & d2\nd3 = d1 + d2\nd1 = 25\n").evaluate(inputs);
        check(std::get<double>(values.at("d3").data)==125);
        check(std::get<std::string>(values.at("stock").data)=="⌀25x100");
        values=RelationProgram("if d1 > 100\n d3 = 1 / 0\nelseif d1 > 20 and material == \"Ocel\"\n d3 = d2 / 2\nelse\n d3 = 1\nendif").evaluate(inputs);
        check(std::get<double>(values.at("d3").data)==50);
        values=RelationProgram("if d1 < 20\n d3 = 99\nendif\nx = d3 + 1").evaluate(inputs);
        check(!values.contains("d3")&&std::get<double>(values.at("x").data)==2);
        values=RelationProgram("d3 = d1 * sind(angle)\nx = -2^2\ny = 2^3^2\nz = round(12.345,2)").evaluate(inputs);
        check(std::abs(std::get<double>(values.at("d3").data)-25)<1e-10);
        check(std::get<double>(values.at("x").data)==-4&&std::get<double>(values.at("y").data)==512);
        check(std::get<double>(values.at("z").data)==12.35);
        values=RelationProgram("d3 = 10 * sind(angle)").evaluate(inputs);
        check(std::abs(std::get<double>(values.at("d3").data)-5)<1e-10);
        check(std::signbit(std::get<double>(RelationProgram("d3 = -0").evaluate(inputs).at("d3").data)));
        const std::string text="český text # = & \"d1\" \\ cesta\nдругой ряд";
        check(std::get<std::string>(RelationProgram("text = "+quote_relation_text(text)).evaluate(inputs).at("text").data)==text);
        rejects([&]{RelationProgram("x = y\ny = x").evaluate(inputs);});
        rejects([&]{RelationProgram("x = 1\nx = 2");},2);
        rejects([&]{RelationProgram("if true\nx = 1");},2);
        rejects([&]{RelationProgram("d3 = d1 + angle").evaluate(inputs);},1);
        rejects([&]{RelationProgram("d3 = d1 + sind(angle)").evaluate(inputs);},1);
        rejects([&]{RelationProgram("d3 = model.mass").evaluate(inputs);},1);
        rejects([&]{RelationProgram("x = model.mass\nd3 = x").evaluate(inputs);},2);
        rejects([&]{RelationProgram("if false\nx = missing\nendif").evaluate(inputs);},2);
        rejects([&]{RelationProgram("x = sqrt(-1)").evaluate(inputs);},1);
        rejects([&]{RelationProgram("x = 1 / 0").evaluate(inputs);},1);
        rejects([&]{RelationProgram("d999 = 10").evaluate(inputs);},1);
        rejects([&]{RelationProgram("x = \"unterminated");},1);
        rejects([&]{RelationProgram("if missing\nendif").validate(inputs);},1);
        rejects([&]{RelationProgram("x = d1^16\ny = x^16").evaluate(inputs);},2);
        std::string growth="x0 = \"1234567890\"\n";
        for(int i=1;i<22;++i)growth+="x"+std::to_string(i)+" = x"+std::to_string(i-1)+" & x"+std::to_string(i-1)+"\n";
        rejects([&]{RelationProgram(growth).evaluate(inputs);});
        std::cout<<"Relation text, conditions, dependencies, quantities, domain errors and signed zero passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
