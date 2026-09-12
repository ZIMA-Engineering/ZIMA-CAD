#include <zima/command_host/host.hpp>
#include <zima/document/thread_catalog.hpp>
#include <stdexcept>

namespace zima::command_host {
void Host::register_opening_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"thread.catalog",tr("Read the shared thread catalog; all dimensions are millimetres."),
        {{"standard",true},{"designation",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer}},false},[this](const Json& args) {
        try {
            const auto& catalog=document::thread_catalog(args.at("standard").get<std::string>());
            const auto offset=args.value("offset",0LL),limit=args.value("limit",100LL);
            if(offset<0||offset>100000000||limit<1||limit>1000)
                return Result::failure("invalid_arguments",tr("Catalog offset must be nonnegative and limit must be between 1 and 1000."));
            const auto designation=args.value("designation",std::string{});
            auto items=Json::array();std::size_t total=0;
            for(const auto& size:catalog) {
                if(!designation.empty()&&designation!=size.designation)continue;
                if(total++<static_cast<std::size_t>(offset)||items.size()>=static_cast<std::size_t>(limit))continue;
                items.push_back({{"designation",size.designation},{"nominal_diameter_mm",size.nominal_diameter},
                    {"pitch_mm",size.pitch},{"internal_root_diameter_mm",size.internal_root_diameter},
                    {"external_root_diameter_mm",size.external_root_diameter},{"preferred",size.preferred}});
            }
            const auto next=static_cast<std::size_t>(offset)+items.size();
            return Result::success({{"standard",args.at("standard")},{"items",std::move(items)},{"total",total},
                {"more",next<total},{"next_offset",next}});
        } catch(const std::invalid_argument& error){return Result::failure("invalid_arguments",tr(error.what()));}
    });
}
} // namespace zima::command_host
