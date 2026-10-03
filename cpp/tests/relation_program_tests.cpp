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

void angle_units() {
    const auto near=[](double a,double b){check(std::abs(a-b)<1e-11*std::max(1.,std::abs(b)));};
    for(const double degrees_per_unit:{1.,180./std::numbers::pi})for(const double length_scale:{1.,10.,1000.,25.4}) {
        RelationInputs input{{"d1",{{30./degrees_per_unit,{0,1,0}},true}},
            {"d2",{{25.4/length_scale,{1,0,0}},true}}, {"d3",{{0.,{0,1,0}},true}}, {"d4",{{0.,{1,0,0}},true}}};
        const auto run=[&](const std::string& source){return RelationProgram(source).evaluate(input,6,false,degrees_per_unit);};
        const auto n=[](const auto& values,const char* key){return std::get<double>(values.at(key).data);};
        auto values=run("d3 = asin(0.5)\nd4 = d2 * sin(d1)\nx = sind(d1)\ny = sin(pi/6)\nz = sind(30)\n");
        near(n(values,"d3")*degrees_per_unit,30);near(n(values,"d4")*length_scale,12.7);
        for(const auto* name:{"x","y","z"})near(n(values,name),.5);
        values=run("x = cos(d1)\ny = cosd(d1)\nz = tan(d1)\nw = tand(d1)");
        near(n(values,"x"),std::sqrt(3.)/2);near(n(values,"y"),std::sqrt(3.)/2);
        near(n(values,"z"),1/std::sqrt(3.));near(n(values,"w"),1/std::sqrt(3.));
        for(const auto& function:{"asin(0.5)","asind(0.5)","acos(0.5)","acosd(0.5)","atan(1)","atand(1)","atan2(d2,d2)","atan2d(d2,d2)"}) {
            values=run(std::string("d3 = ")+function);
            const double expected=std::string(function).starts_with("asin")?30:std::string(function).starts_with("acos")?60:45;
            near(n(values,"d3")*degrees_per_unit,expected);check(values.at("d3").units==std::array<int,3>{0,1,0});
        }
        values=run("x = sin(acos(0.5))\ny = sind(asind(0.5))\nd3 = atan2(-0,-1)");
        near(n(values,"x"),std::sqrt(3.)/2);near(n(values,"y"),.5);near(n(values,"d3")*degrees_per_unit,-180);
        values=run("d3 = atan2(-0,1)");check(n(values,"d3")==0&&std::signbit(n(values,"d3")));
        rejects([&]{run("x = sin(d2)");});rejects([&]{run("d3 = acos(2)");});
        input.at("d1").value.data=90./degrees_per_unit;
        rejects([&]{run("x = tan(d1)");});rejects([&]{run("x = tand(d1)");});
    }
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
        "if sin(d5) > 0.1\n guarded = asind(0.5)\nelse\n guarded = acos(2)\nendif\n"
        "area = d1 * d2\nvolume = d1^3\nroot = sqrt(d1*d1)\n"
        "angular_power = d1 ^ round((asin(0.5) + 30) / asin(0.5))\n"
        "ratio = d1 / d2\ntrig = sind(d5) + cos(d5)\n"
        "inverse = asind(0.5) + atan2d(d1,d2)\n"
        "nested = sin(asin(0.5)) + cosd(acosd(0.5))\n"
        "rounded_angle = round(asin(0.5) + 0.1234, 2)\n"
        "direct_inverse = atan2(d1,d2) # closing parentheses\n"
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
        const auto after=RelationProgram(converted.source).evaluate(new_inputs,3,false,180./std::numbers::pi);
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
    const auto fixed_values=RelationProgram(fixed.source).evaluate(new_inputs,3,false,180./std::numbers::pi);
    check(std::abs(std::get<double>(fixed_values.at("SHEETMETAL_THICKNESS").data)-50)<1e-12);
    check(std::abs(std::get<double>(fixed_values.at("d3").data)-100*scales[0])<1e-12);
    const auto zero=RelationProgram("d3 = -0").convert_units(inputs,scales);
    check(std::signbit(std::get<double>(RelationProgram(zero.source).evaluate(new_inputs,3,false,180./std::numbers::pi).at("d3").data)));
    const auto inactive=RelationProgram("if false\nd3 = 1 / 0\nelse\nd3 = 10\nendif").convert_units(inputs,scales);
    check(std::abs(std::get<double>(RelationProgram(inactive.source).evaluate(new_inputs,3,false,180./std::numbers::pi).at("d3").data)-10*scales[0])<1e-14);
    rejects([&]{RelationProgram("if false\nx = d1 + d5\nendif").convert_units(inputs,scales);},2);
    rejects([&]{RelationProgram("if true\nx = d1\nelse\nx = d5\nendif").convert_units(inputs,scales);},4);
    rejects([&]{RelationProgram("x = d1^d4").convert_units(inputs,scales);},1);
    check(RelationProgram(source).convert_units(inputs,{1,1,1}).source==source);
    auto repeated=source;
    for(int i=0;i<6;++i) {
        const bool outward=i%2==0;const auto factors=outward?scales:std::array<double,3>{1/scales[0],1/scales[1],1/scales[2]};
        repeated=RelationProgram(repeated).convert_units(outward?inputs:new_inputs,factors,{},outward?1.:180./std::numbers::pi).source;
        const auto actual=RelationProgram(repeated).evaluate(outward?new_inputs:inputs,3,false,outward?180./std::numbers::pi:1.);
        const auto expected=RelationProgram(source).evaluate(inputs);
        for(const auto& [name,value]:expected) {
            const auto& next=actual.at(name);check(value.units==next.units&&value.data.index()==next.data.index());
            if(const auto* number=std::get_if<double>(&value.data)) {
                const double converted=*number*(outward?scale(value.units):1.);
                check(std::abs(std::get<double>(next.data)-converted)<1e-10*std::max(1.,std::abs(converted)));
            } else check(value.data==next.data);
        }
    }
}
int main() {
    try {
        angle_units();
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
