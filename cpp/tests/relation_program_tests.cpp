#include <zima/document/relation_program.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace zima::document;
void check(bool value) { if(!value)throw std::runtime_error("Relation program assertion failed"); }
template<class F> void rejects(F f,int line=0) {
    try {f();}catch(const RelationError& e){check(!line||e.line==line);return;}
    throw std::runtime_error("Invalid relation accepted");
}
int main() {
    try {
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
