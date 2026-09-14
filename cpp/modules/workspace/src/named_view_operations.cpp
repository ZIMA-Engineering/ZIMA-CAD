#include <zima/workspace/named_view_operations.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
bool publish(Workspace& live,const std::string& id,const std::vector<document::NamedView>& views) {
    if(live.active_document_id()!=id)throw std::invalid_argument("Named views can only be edited in the active document.");
    const auto serialized=document::serialize_named_views(views);
    if(auto* part=live.open_part(id)) {
        if(document::parse_named_views(part->session.document().named_views)==views)return false;
        auto next=part->session.document();next.named_views=serialized;
        part->session.commit(std::move(next),part->session.calculated_boundaries());return true;
    }
    if(auto* assembly=live.open_assembly(id)) {
        if(document::parse_named_views(assembly->session.document().named_views)==views)return false;
        auto next=assembly->session.document();next.named_views=serialized;
        assembly->session.commit(std::move(next));return true;
    }
    throw std::invalid_argument("Named views require an open Part or Assembly.");
}
}
std::vector<document::NamedView> named_views(const Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return document::parse_named_views(part->session.document().named_views);
    if(const auto* assembly=live.open_assembly(id))return document::parse_named_views(assembly->session.document().named_views);
    throw std::invalid_argument("Named views require an open Part or Assembly.");
}
document::NamedView named_view(const Workspace& live,const std::string& id,const std::string& name) {
    const auto views=named_views(live,id);
    const auto found=std::ranges::find(views,name,&document::NamedView::name);
    if(found==views.end())throw std::out_of_range("Named view not found.");
    return *found;
}
bool set_named_view(Workspace& live,const std::string& id,document::NamedView view) {
    document::normalize_named_view(view);
    auto views=named_views(live,id);const auto found=std::ranges::find(views,view.name,&document::NamedView::name);
    if(found==views.end())views.push_back(std::move(view));else *found=std::move(view);
    return publish(live,id,views);
}
bool delete_named_view(Workspace& live,const std::string& id,const std::string& name) {
    auto views=named_views(live,id);
    if(!std::erase_if(views,[&](const auto& view){return view.name==name;}))throw std::out_of_range("Named view not found.");
    return publish(live,id,views);
}
}
