#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
namespace zima::workspace::metadata_detail {
template<class Fn> auto read(const Workspace& live,const std::string& id,Fn fn) {
    if(const auto* part=live.open_part(id))return fn(part->session.document());
    if(const auto* assembly=live.open_assembly(id))return fn(assembly->session.document());
    throw std::invalid_argument("Document metadata requires an open Part or Assembly.");
}
template<class Fn,class Same> bool write(Workspace& live,const std::string& id,Fn fn,Same same) {
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();fn(next);
        document::refresh_physical_relations(next,document::physical_values(next,part->session.calculated_boundaries()));
        if(same(next,part->session.document()))return false;
        part->session.commit(std::move(next),part->session.calculated_boundaries());return true;
    }
    if(auto* assembly=live.open_assembly(id)) {
        auto next=assembly->session.document();fn(next);
        document::refresh_physical_relations(next,assembly::physical_values(next));
        if(same(next,assembly->session.document()))return false;
        assembly->session.commit(std::move(next));return true;
    }
    throw std::invalid_argument("Document metadata requires an open Part or Assembly.");
}
}
