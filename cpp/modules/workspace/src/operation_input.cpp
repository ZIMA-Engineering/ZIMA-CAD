#include <zima/workspace/operation_input.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
bool owns_kernel_operation(const document::HistoryContainer* feature) {
    return feature&&feature->feature_kind!=document::FeatureKind::Sketch&&
        feature->feature_kind!=document::FeatureKind::SheetProfile;
}
}
const kernel::BodyResult* calculated_operation_input(const document::DocumentSession& session,
    const std::string& id) {
    const auto& doc=session.document();const auto& calculated=session.calculated_boundaries();
    if(calculated.empty()||(!id.empty()&&!doc.find_container(id)))return nullptr;
    std::size_t count{};const std::vector<kernel::BodyResult>* boundaries=&calculated;
    if(!doc.body_history.bodies().empty()) {
        const auto* body=id.empty()?doc.body_history.find(doc.body_history.active_body_id()):doc.body_owner_for_object(id);
        if(!body)return nullptr;
        const auto end=id.empty()?body->cursor:doc.body_history.rollback_before(id).entry_count;
        for(std::size_t i=0;i<std::min(end,body->entries.size());++i) {
            const auto& entry=body->entries[i];if(entry.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=doc.find_container(entry.id);
            if(owns_kernel_operation(feature))++count;
        }
        const auto found=calculated.back().body_boundaries.find(body->scope.id);
        if(found==calculated.back().body_boundaries.end())return nullptr;
        boundaries=&found->second.get();
    }else if(id.empty())count=doc.body_operation_count_at_history_cursor();
    else if(doc.history_order.empty()) {
        for(const auto& feature:doc.history) {
            if(feature.id==id)break;
            if(owns_kernel_operation(&feature))++count;
        }
    }else {
        for(const auto& entry:doc.history_order) {
            if(entry.id==id)break;
            if(entry.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=doc.find_container(entry.id);
            if(owns_kernel_operation(feature))++count;
        }
    }
    return count&&count<=boundaries->size()?&boundaries->at(count-1):nullptr;
}

const kernel::BodyResult* calculated_operation_output(
        const document::DocumentSession& session,const std::string& id) {
    const auto& doc=session.document();const auto& calculated=session.calculated_boundaries();
    if(id.empty()||calculated.empty()||
            !owns_kernel_operation(doc.find_container(id)))return nullptr;
    std::size_t count{};bool found_id{};
    const std::vector<kernel::BodyResult>* boundaries=&calculated;
    if(!doc.body_history.bodies().empty()) {
        const auto* body=doc.body_owner_for_object(id);if(!body)return nullptr;
        for(const auto& entry:body->entries) {
            if(entry.kind==document::PartHistoryKind::Feature) {
                const auto* feature=doc.find_container(entry.id);
                if(owns_kernel_operation(feature))++count;
            }
            if(entry.id==id){found_id=true;break;}
        }
        const auto body_rows=calculated.back().body_boundaries.find(body->scope.id);
        if(body_rows==calculated.back().body_boundaries.end())return nullptr;
        boundaries=&body_rows->second.get();
    } else if(doc.history_order.empty()) {
        for(const auto& feature:doc.history) {
            if(owns_kernel_operation(&feature))++count;
            if(feature.id==id){found_id=true;break;}
        }
    } else {
        for(const auto& entry:doc.history_order) {
            if(entry.kind==document::PartHistoryKind::Feature) {
                const auto* feature=doc.find_container(entry.id);
                if(owns_kernel_operation(feature))++count;
            }
            if(entry.id==id){found_id=true;break;}
        }
    }
    return found_id&&count&&count<=boundaries->size()
        ?&boundaries->at(count-1):nullptr;
}
} // namespace zima::workspace
