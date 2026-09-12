#include <zima/document/thread_catalog.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

using namespace zima::document;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected){require(std::abs(value-expected)<1e-10,"Incorrect catalog dimension");}
const ThreadCatalogSize& entry(const char* standard,const char* designation) {
    const auto& values=thread_catalog(standard);const auto found=std::ranges::find(values,designation,&ThreadCatalogSize::designation);
    require(found!=values.end(),"Missing thread designation");return *found;
}
}
int main(){try {
    for(const auto& [standard,count]:{std::pair{"metric",392U},{"whitworth",28U},{"pipe",24U}}) {
        const auto& values=thread_catalog(standard);require(values.size()==count,"Catalog row count changed");
        require(&values==&thread_catalog(standard),"Catalog rebuilt immutable records");std::set<std::string> names;
        for(const auto& value:values) {
            require(names.insert(value.designation).second,"Duplicate catalog designation");
            require(std::isfinite(value.pitch)&&value.pitch>0&&value.internal_root_diameter>0&&
                value.external_root_diameter>0&&value.nominal_diameter>value.internal_root_diameter&&
                value.nominal_diameter>value.external_root_diameter,"Invalid thread diameter or pitch scale");
        }
    }
    const auto& m10=entry("metric","M10");near(m10.nominal_diameter,10);near(m10.pitch,1.5);
    near(m10.internal_root_diameter,8.376);near(m10.external_root_diameter,8.160);require(m10.preferred,"Missing preferred metric size");
    const auto& fine=entry("metric","M10\xc3\x97" "1");near(fine.pitch,1);near(fine.internal_root_diameter,8.917);require(!fine.preferred,"Fine pitch marked preferred");
    const auto& half=entry("whitworth","W 1/2");near(half.nominal_diameter,25.4/2);near(half.pitch,2.117);near(half.internal_root_diameter,9.988);
    const auto& pipe=entry("pipe","G 1/2");near(pipe.nominal_diameter,20.955);near(pipe.pitch,(20.955-19.793)/.640327);near(pipe.internal_root_diameter,18.631);
    bool rejected=false;try{static_cast<void>(thread_catalog("unknown"));}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Unknown thread standard silently selected a different table");
    std::cout<<"Shared thread tables, exact dimensions, UTF-8 names and immutable records passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
