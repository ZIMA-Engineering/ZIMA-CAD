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
        // Distinct open subassemblies are also workspace roots. Exercise both
        // tab orders, so a source may be visited before or after its parent.
        for(bool parent_first:{false,true})for(int count:{32,128}) {
            auto top=assembly::AssemblyDocument::create_default();
            std::vector<assembly::AssemblyDocument> children;
            for(int i=0;i<count;++i) {
                auto child=sub;child.document_id=assembly::AssemblyDocument::create_default().document_id;
                auto item=assembly::AssemblyDocument::create_assembly_occurrence("group",child.document_id,root/(child.document_id+".asmz"),child);
                item.placement.y=i*15.;top.components.push_back(std::move(item));
                children.push_back(std::move(child));
            }
            workspace::Workspace live;
            live.add_part(part,bodies,part_file);
            if(parent_first)live.add_assembly(top,root/"open-top.asmz");
            for(const auto& child:children)live.add_assembly(child,root/(child.document_id+".asmz"));
            if(!parent_first)live.add_assembly(top,root/"open-top.asmz");
            live.refresh_source_geometry();
            const auto initial=live.open_assembly(top.document_id)->session.document();
            const auto warm=timed([&]{live.refresh_source_geometry();},20);
            const auto& current=live.open_assembly(top.document_id)->session;
            require(current.document().occurrence_snapshot()==initial.occurrence_snapshot(),"Open-root refresh changed metadata");
            for(int i=0;i<count;++i) {
                require(current.document().components[i].calculated_source.shares_with(initial.components[i].calculated_source),"Open-root refresh rebuilt unchanged geometry");
                const auto& session=live.open_assembly(children[i].document_id)->session;
                require(session.revision()==0&&!session.is_dirty()&&!session.can_undo(),"Open child refresh changed history");
            }
            auto edited=part;edited.name="Unsaved renamed source";
            test::profile_dimension(edited,edited.history.front(),0)*=2;
            auto changed=kernel.evaluate_history(edited.kernel_operations());
            live.open_part(part.document_id)->session.commit(edited,changed);
            live.refresh_source_geometry();
            for(const auto& c:live.open_assembly(top.document_id)->session.document().components)
                require(std::abs(c.calculated_source->volume-8*changed.back().volume)<1e-6,"Open-root refresh missed source edit");
            require(live.open_part(part.document_id)->session.undo(),"Source Undo failed");
            live.refresh_source_geometry();
            for(const auto& c:live.open_assembly(top.document_id)->session.document().components)
                require(std::abs(c.calculated_source->volume-8*bodies.back().volume)<1e-6,"Open-root refresh missed source Undo");
            std::cout<<"open_roots parent_first="<<parent_first<<" groups="<<count<<" leaves="<<count*8
                <<" warm_mean_ms="<<warm<<" repetitions=20\n"<<std::flush;
        }
        std::cout<<"Source refresh benchmark checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
