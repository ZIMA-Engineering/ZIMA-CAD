#pragma once
#include <algorithm>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace zima::document {
namespace exact_unit_detail {
// A length unit is coefficient * 10^power millimetres. Keeping 25.4
// decimal avoids importing its binary floating-point approximation.
struct Unit {unsigned coefficient;int power;bool angular;};
inline std::optional<Unit> unit(std::string_view name) {
    if(name=="mm")return Unit{1,0,false};
    if(name=="cm")return Unit{1,1,false};
    if(name=="m")return Unit{1,3,false};
    if(name=="in")return Unit{254,-1,false};
    if(name=="deg"||name=="rad")return Unit{1,0,true};
    return {};
}
constexpr int limit=2048;
struct Decimal {std::string digits;int power{};char sign{};};
inline std::optional<Decimal> parse(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    if(first==std::string_view::npos||last-first+1>limit)return {};
    text=text.substr(first,last-first+1);Decimal value;std::size_t index=0;
    if(text[index]=='+'||text[index]=='-')value.sign=text[index++];
    bool decimal=false;int fractional=0;
    for(;index<text.size()&&text[index]!='e'&&text[index]!='E';++index) {
        const char c=text[index];
        if(c=='.'||c==',') {if(decimal)return {};decimal=true;continue;}
        if(c<'0'||c>'9')return {};
        value.digits.push_back(c);if(decimal)++fractional;
    }
    if(value.digits.empty())return {};
    int exponent=0;
    if(index<text.size()) {
        ++index;bool negative=false;
        if(index<text.size()&&(text[index]=='+'||text[index]=='-'))negative=text[index++]=='-';
        if(index==text.size())return {};
        for(;index<text.size();++index) {
            if(text[index]<'0'||text[index]>'9')return {};
            exponent=exponent*10+(text[index]-'0');if(exponent>limit)return {};
        }
        if(negative)exponent=-exponent;
    }
    value.power=exponent-fractional;
    const auto nonzero=value.digits.find_first_not_of('0');
    value.digits=nonzero==std::string::npos?"0":value.digits.substr(nonzero);
    return value;
}
inline void multiply(std::string& digits,unsigned factor) {
    unsigned carry=0;
    for(auto i=digits.rbegin();i!=digits.rend();++i) {
        const auto value=unsigned(*i-'0')*factor+carry;*i=char('0'+value%10);carry=value/10;
    }
    while(carry){digits.insert(digits.begin(),char('0'+carry%10));carry/=10;}
}
inline unsigned divide(std::string& digits,unsigned divisor) {
    unsigned remainder=0;
    for(auto& c:digits) {const auto value=remainder*10+unsigned(c-'0');c=char('0'+value/divisor);remainder=value%divisor;}
    const auto nonzero=digits.find_first_not_of('0');
    digits=nonzero==std::string::npos?"0":digits.substr(nonzero);
    return remainder;
}
inline std::optional<std::string> render(Decimal value) {
    const int point=int(value.digits.size())+value.power;
    const int size=value.power>=0?point:std::max(point,1)+1-value.power;
    // Guard allocation before padding very small/large scientific values.
    if(size>limit||value.power < -limit)return {};
    std::string text;
    if(value.power>=0)text=value.digits+std::string(value.power,'0');
    else if(point>0)text=value.digits.substr(0,point)+"."+value.digits.substr(point);
    else text="0."+std::string(-point,'0')+value.digits;
    if(value.sign)text.insert(text.begin(),value.sign);
    if(text.size()>limit)return {};
    return text;
}
}

// Returns a finite decimal only when it communicates exactly the same value.
// No binary floating-point conversion, rounding, or loss of signed zero occurs.
// nullopt means invalid input, incompatible units, a nonterminating conversion
// (including nonzero deg/rad), or an annotation exceeding the text-size bound.
// Trailing decimal places are retained when possible; this is not a formatter
// for intentionally rounded, approximate engineering quantities.
inline std::optional<std::string> exact_decimal_unit_conversion(std::string_view text,
        std::string_view source_unit,std::string_view target_unit) {
    using namespace exact_unit_detail;
    const auto source=unit(source_unit),target=unit(target_unit);
    if(!source||!target||source->angular!=target->angular)return {};
    auto value=parse(text);if(!value)return {};
    if(source->angular&&source_unit!=target_unit&&value->digits!="0")return {};
    const auto common=std::gcd(source->coefficient,target->coefficient);
    multiply(value->digits,source->coefficient/common);
    unsigned denominator=target->coefficient/common;
    unsigned twos=0,fives=0;
    while(denominator%2==0){denominator/=2;++twos;}
    while(denominator%5==0){denominator/=5;++fives;}
    // Any other prime factor must divide the numerator exactly. Extra powers
    // of ten cannot cancel it; appending digits would only hide a repetition.
    if(denominator>1&&divide(value->digits,denominator)!=0)return {};
    // Cancel existing factors before adding decimal places. Otherwise exact
    // values such as 25.4 mm acquire an unnecessary zero when shown as inches.
    while(twos&&(value->digits.back()-'0')%2==0){divide(value->digits,2);--twos;}
    while(fives&&(value->digits.back()=='0'||value->digits.back()=='5')){divide(value->digits,5);--fives;}
    const auto places=std::max(twos,fives);
    for(auto i=twos;i<places;++i)multiply(value->digits,2);
    for(auto i=fives;i<places;++i)multiply(value->digits,5);
    value->power+=source->power-target->power-int(places);
    return render(std::move(*value));
}
} // namespace zima::document
