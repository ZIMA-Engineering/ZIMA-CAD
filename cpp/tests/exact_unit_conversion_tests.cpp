#include <zima/document/exact_unit_conversion.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
using zima::document::exact_decimal_unit_conversion;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void exact(const char* text,const char* from,const char* to,const char* expected) {
    const auto result=exact_decimal_unit_conversion(text,from,to);
    if(!result||*result!=expected)throw std::runtime_error(std::string(text)+" "+from+" -> "+to+": "+result.value_or("rejected")+" != "+expected);
}
int main(){try {
    exact("0,0005","in","mm","0.01270");
    exact("0.01270","mm","in","0.0005");
    exact("0.0127","mm","in","0.0005");
    exact("25.4","mm","in","1");
    exact("2.54","mm","in","0.1");
    exact("0.254","mm","in","0.01");
    exact(".100","mm","cm","0.0100");
    exact(".100","m","cm","10.0");
    exact("1","m","mm","1000");
    exact("1000","mm","m","1.000");
    exact("-0.00050","in","mm","-0.012700");
    exact("+0.00050","in","mm","+0.012700");
    exact("-0.000","mm","cm","-0.0000");
    exact("-0","deg","rad","-0");
    exact("0.000","rad","deg","0.000");
    exact("000.1000","deg","deg","0.1000");
    exact("1.2700E-2","mm","in","0.00050");
    exact("1e3","cm","mm","10000");
    exact("  +.0127 ","mm","in","+0.0005");
    // Preserve exact decimals even below floating-point range.
    const auto tiny=exact_decimal_unit_conversion("1e-400","cm","mm");
    check(tiny&&*tiny=="0."+std::string(398,'0')+"1","Subnormal decimal was rounded to zero");
    const auto large=exact_decimal_unit_conversion("1e400","mm","m");
    check(large&&*large=="1"+std::string(397,'0'),"Large exact decimal overflowed through double");
    for(const auto* text:{"0.1","1","10","-0.001","127.0001"})
        check(!exact_decimal_unit_conversion(text,"mm","in"),"Repeating inch conversion silently rounded");
    for(const auto* text:{"1","90","-45.00","0.0000000000000000001"}) {
        check(!exact_decimal_unit_conversion(text,"deg","rad"),"Irrational radian conversion silently rounded");
        check(!exact_decimal_unit_conversion(text,"rad","deg"),"Irrational degree conversion silently rounded");
    }
    for(const auto* text:{""," ","+","-",".","1,2.3","1 000","nan","inf","1e","1e+","1e999999","1e-2048","H7","25.4mm","1/2"})
        check(!exact_decimal_unit_conversion(text,"mm","cm"),"Invalid/ambiguous decimal was accepted");
    check(!exact_decimal_unit_conversion("0","mm","rad"),"Zero bypassed dimension compatibility");
    check(!exact_decimal_unit_conversion("1","unknown","mm"),"Unknown source unit accepted");
    check(!exact_decimal_unit_conversion("1","mm","unknown"),"Unknown target unit accepted");
    // Independent integer arithmetic oracle: thousandths of an inch -> ten-
    // thousandths of a millimetre. Nonterminating values are never rounded.
    for(int value=1;value<=1000;++value) {
        const auto input=std::to_string(value/1000)+"."+std::string(3-std::to_string(value%1000).size(),'0')+std::to_string(value%1000);
        const int scaled=value*254;
        const auto expected=std::to_string(scaled/10000)+"."+std::string(4-std::to_string(scaled%10000).size(),'0')+std::to_string(scaled%10000);
        const auto converted=exact_decimal_unit_conversion(input,"in","mm");
        check(converted&&*converted==expected,"Integer-oracle inch conversion differs");
        const auto restored=exact_decimal_unit_conversion(*converted,"mm","in");
        check(restored&&*restored==input,"Exact inch round trip changed the authored decimal");
    }
    std::cout<<"Exact decimal units, signed zero, finite/repeating detection and integer oracle passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
