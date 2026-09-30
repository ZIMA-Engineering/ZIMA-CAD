#include <zima/kernel/geometry_kernel.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima::kernel;
namespace {
template<class Profile> void populate(Profile& profile, int sides) {
    ExtrusionRequest::PolygonProfile polygon;
    for (int i=0; i<sides; ++i) {
        const double angle=2*std::numbers::pi*i/sides;
        polygon.vertices.push_back({20+10*std::cos(angle),10*std::sin(angle),0});
        profile.outer_edge_source_ids.push_back("sketch:0123456789abcdef:curve:"+std::to_string(i));
        profile.outer_vertex_source_ids.push_back("sketch:0123456789abcdef:point:"+std::to_string(i));
    }
    profile.outer_profile=polygon;
    profile.profile_region_id="region"; profile.outer_boundary_id="outer";
    profile.inner_profiles={ExtrusionRequest::CircleProfile{{20,0,0},2}};
    profile.inner_boundary_ids={"inner"};
    profile.inner_edge_source_ids={{"inner-circle"}};
    profile.inner_vertex_source_ids={{}};
    ExtrusionRequest::ProfileRegion region;
    region.region_id="second-region"; region.outer_boundary_id="second-outer";
    region.outer_profile=ExtrusionRequest::CircleProfile{{50,0,0},3};
    region.outer_edge_source_ids={"second-circle"};
    region.inner_profiles={ExtrusionRequest::CircleProfile{{50,0,0},1}};
    region.inner_boundary_ids={"second-inner"};
    region.inner_edge_source_ids={{"second-inner-circle"}};
    region.inner_vertex_source_ids={{}};
    profile.additional_profile_regions={region};
}
std::vector<HistoryOperation> fixture(int count, int sides) {
    std::vector<HistoryOperation> operations;
    for (int i=0;i<count;++i) {
        HistoryOperation operation; operation.owner_id="feature-"+std::to_string(i);
        if(i%2) { RevolutionRequest request; populate(request,sides); operation.primitive=request; }
        else { ExtrusionRequest request; populate(request,sides); operation.primitive=request; }
        operations.push_back(std::move(operation));
    }
    return operations;
}
void require(bool condition, const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
    try {
        std::ofstream snapshot;
        if(argc>1) { snapshot.open(argv[1]); require(bool(snapshot),"Cannot open fingerprint snapshot"); }
        for(int sides:{4,64})for(int count:{32,128}) {
            const auto operations=fixture(count,sides);
            std::vector<std::string> fingerprints(count+2);
            double single_ms=0,ms=0;
            for(int repeat=0;repeat<5;++repeat) {
                auto start=std::chrono::steady_clock::now();
                for(int prefix=0;prefix<=count+1;++prefix)fingerprints[prefix]=history_fingerprint(operations,prefix);
                single_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/5;
                start=std::chrono::steady_clock::now();
                const auto batch=history_fingerprints(operations);
                ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/5;
                for(int prefix=0;prefix<=count+1;++prefix)require(fingerprints[prefix]==batch[std::min(prefix,count)],"Batch changed a persisted prefix fingerprint");
            }
            std::cout<<"single_prefixes_ms="<<single_ms<<" ";
            require(fingerprints[count]==fingerprints[count+1],"Prefix clamp changed");
            if(snapshot.is_open()) {
                snapshot<<sides<<' '<<count<<'\n';
                for(const auto& fingerprint:fingerprints)snapshot<<fingerprint<<'\n';
            }
            std::cout<<"sides="<<sides<<" operations="<<count<<" all_prefixes_ms="
                     <<std::fixed<<std::setprecision(3)<<ms<<'\n';
        }
        // Exercise every affected list independently, including empty/singleton
        // groups. Persisted IDs and authored signed zero must remain observable.
        auto operations=fixture(2,4);
        const auto original=history_fingerprint(operations,2);
        // Captured from the original copying encoder before the optimization.
        const std::array<const char*,20> expected{
            "d9e483bb623368b4","554bdc78a5da933d","e83de9172ff69984","e0035bdb370e1bbe",
            "dc9c36d078e32de6","af0043656e3fedd0","4e226de94b1d3d2b","0feb8a96b85a7f56",
            "dbcfa2567a5ee860","cebca3d0dbf0544d","531ffbcfe34a62b0","802a82edeb0621fe",
            "3fe4ef7da5958a16","ba6f3d26d06680ac","98b5bc3699c01727","c76400768748dfb6",
            "0cf447405491fe65","6918cb26400ab095","c2768901c1b4bc1d","09f02aa40b340623"};
        std::size_t case_index=0;
        const auto changed=[&](auto edit) {
            auto copy=operations; edit(copy);
            const auto fingerprint=history_fingerprint(copy,2);
            require(history_fingerprints(copy).back()==fingerprint,"Batched invalidation differs from single prefix");
            require(fingerprint!=original,"Changed history input was not detected");
            require(fingerprint==expected.at(case_index++),"Fingerprint encoding changed");
            if(snapshot.is_open())snapshot<<fingerprint<<'\n';
        };
        for(int feature=0;feature<2;++feature)for(int list=0;list<8;++list)
            changed([&](auto& copy) {
                const auto edit=[&](auto& request) {
                    switch(list) {
                    case 0: request.outer_edge_source_ids.front()+="-changed"; break;
                    case 1: request.outer_vertex_source_ids.clear(); break;
                    case 2: request.inner_edge_source_ids.push_back({}); break;
                    case 3: request.inner_vertex_source_ids.front().push_back("new-point"); break;
                    case 4: request.additional_profile_regions.front().outer_edge_source_ids.clear(); break;
                    case 5: request.additional_profile_regions.front().outer_vertex_source_ids={"new-point"}; break;
                    case 6: request.additional_profile_regions.front().inner_edge_source_ids.front().clear(); break;
                    case 7: request.additional_profile_regions.front().inner_vertex_source_ids.clear(); break;
                    }
                };
                if(feature==0)edit(std::get<ExtrusionRequest>(copy[feature].primitive));
                else edit(std::get<RevolutionRequest>(copy[feature].primitive));
            });
        changed([](auto& copy){std::get<ExtrusionRequest>(copy[0].primitive).start_offset=-0.0;});
        changed([](auto& copy){copy[0].mesh_deflection=0.2;});
        changed([](auto& copy){copy[0].boolean_tolerance=1e-6;});
        changed([](auto& copy){std::get<ExtrusionRequest>(copy[0].primitive).direction.z=11;});
        if(snapshot.is_open()){snapshot.flush();require(bool(snapshot),"Cannot write fingerprint snapshot");}
        std::cout<<"Fingerprint invalidation checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
