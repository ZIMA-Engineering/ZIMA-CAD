#include "profile_solid_fixture.hpp"
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <QTemporaryDir>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> double timed(F&& action,int count=1) {
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i)action();
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/count;
}
}

int main() {
    using namespace zima;
    try {
        QTemporaryDir directory;
        require(directory.isValid(),"Cannot create benchmark directory");
        const std::filesystem::path root=directory.path().toStdWString();
        auto part=document::PartDocument::create_default();
        part.history.push_back(test::rectangular_feature(part,{10,10,10}));
        kernel::OcctKernel kernel;
        auto bodies=kernel.evaluate_history(part.kernel_operations());
        const auto part_file=root/"source.prtz",sub_file=root/"sub.asmz";
        part.save(part_file,bodies);
        auto sub=assembly::AssemblyDocument::create_default();
        for(int i=0;i<8;++i) {
            auto leaf=assembly::AssemblyDocument::create_part_occurrence("leaf",part.document_id,part_file,bodies.back());
            leaf.placement.x=i*15.;sub.components.push_back(std::move(leaf));
        }
        sub.save(sub_file);
        std::cout<<std::fixed<<std::setprecision(3);
        for(bool opened:{false,true})for(int count:{32,128}) {
            auto top=assembly::AssemblyDocument::create_default();
            auto prototype=assembly::AssemblyDocument::create_assembly_occurrence("group",sub.document_id,sub_file,sub);
            for(int i=0;i<count;++i) {
                auto item=prototype;item.occurrence_id="group-"+std::to_string(i);
                item.placement.y=i*15.;top.components.push_back(std::move(item));
            }
            workspace::Workspace live;
            live.add_assembly(top,root/"top.asmz");
            if(opened) { live.add_assembly(sub,sub_file);live.add_part(part,bodies,part_file); }
            const double cold=timed([&]{live.refresh_source_geometry();});
            const auto initial=live.open_assembly(top.document_id)->session.document().components.front().calculated_source;
            const auto snapshot=live.open_assembly(top.document_id)->session.document().components.front().nested_snapshot;
            const double warm=timed([&]{live.refresh_source_geometry();},5);
            const auto& current=live.open_assembly(top.document_id)->session;
            require(current.revision()==0&&!current.is_dirty()&&!current.can_undo(),"Display refresh changed edit history");
            for(int i=0;i<count;++i) {
                const auto& c=current.document().components[i];
                require(c.calculated_source.shares_with(initial),"Unchanged shared source was rebuilt");
                require(c.placement.y==i*15.&&c.occurrence_id=="group-"+std::to_string(i),"Occurrence identity or placement changed");
                require(c.nested_snapshot==snapshot,"Nested metadata changed during unchanged refresh");
            }
            const auto scene=current.document().build_scene();
            std::cout<<"source_refresh open="<<opened<<" groups="<<count<<" leaves="<<count*8
                <<" cold_ms="<<cold<<" warm_mean_ms="<<warm<<" repetitions=5 triangles="<<scene.triangles.size()/3<<'\n'<<std::flush;
        }
        std::cout<<"Source refresh benchmark checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
