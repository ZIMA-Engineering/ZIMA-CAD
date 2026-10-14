#include <zima/document/cache_storage.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
using nlohmann::json;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
// Previous traversal is a byte-level oracle for the allocation optimization.
// It is intentionally confined to this test, never a second product path.
class CopyingWriter {
    std::unordered_map<std::string,std::size_t> indices;
public:
    json values=json::array();
    json encode(const json& source) {
        json encoded=source;
        if(encoded.is_structured())for(auto& child:encoded)child=encode(child);
        if(!encoded.is_structured()&&!encoded.is_string())return encoded;
        auto key=encoded.dump();if(key.size()<256)return encoded;
        if(const auto found=indices.find(key);found!=indices.end())return {{"$zima_cache",found->second}};
        const auto index=values.size();indices.emplace(std::move(key),index);
        values.push_back(std::move(encoded));return {{"$zima_cache",index}};
    }
};
json copying_pack(const json& source) {
    CopyingWriter writer;auto root=writer.encode(source);
    return {{"format","zima-shared-cache-v1"},{"root",std::move(root)},{"values",std::move(writer.values)}};
}
int main() {
    try {
        zima::kernel::BodyResult body;
        body.kernel_shape=std::string(8192,'B');
        body.source_fingerprint="stable-input";
        body.volume=17.25;
        body.mesh.vertices={{0,0,0},{1,0,0},{0,1,0}};
        body.mesh.triangles={0,1,2};
        auto different=body;different.volume=18.5;
        if(!different.kernel_shape.shares_with(body.kernel_shape))
            throw std::runtime_error("Calculated snapshots copied opaque archive bytes");
        auto changed_archive=different.kernel_shape.get();changed_archive.back()='C';
        different.kernel_shape=std::move(changed_archive);
        if(body.kernel_shape.get().back()!='B'||different.kernel_shape.shares_with(body.kernel_shape))
            throw std::runtime_error("Replacing an opaque archive changed its original snapshot");
        auto aggregate=body;
        aggregate.body_boundaries["body"]={body,different};
        aggregate.body_inputs["body"]=body;
        aggregate.body_outputs["body"]=different;
        const auto packet=zima::document::serialize_body_result(aggregate);
        json original={{"components",json::array()}};
        for(int i=0;i<12;++i)original["components"].push_back({
            {"instance_path","occurrence-"+std::to_string(i)},
            {"placement",i*10.0},{"calculated_source",packet}});
        const auto packed=zima::document::pack_cache_storage(original);
        auto owned=original;
        const auto consumed=zima::document::pack_cache_storage(std::move(owned));
        require(owned.is_null(),"Consuming cache retained its original tree");
        require(consumed.dump()==packed.dump(),"Consuming cache changed stored bytes or block order");
        require(packed.dump()==copying_pack(original).dump(),"Cache allocation optimization changed stored bytes or block order");
        // Round-trip the actual on-disk representation, not only in-memory JSON.
        const auto restored=zima::document::unpack_cache_storage(json::parse(packed.dump()));
        require(restored==original,"Shared cache changed geometry, history or occurrence identity");
        require(packed.dump().size()<original.dump().size()/4,"Identical cache blocks were not shared");
        const auto loaded=zima::document::load_body_result(restored["components"][0]["calculated_source"]);
        require(loaded.body_boundaries.at("body").size()==2 &&
            loaded.body_inputs.at("body")->kernel_shape==body.kernel_shape &&
            loaded.body_outputs.at("body")->kernel_shape==different.kernel_shape,
            "Distinct history states were merged or lost");
        require(zima::document::pack_cache_storage(original)==packed,"Cache encoding is not deterministic");
        const auto reject=[](json storage) {
            try {static_cast<void>(zima::document::unpack_cache_storage(storage));return false;}
            catch(const std::exception&){return true;}
        };
        auto broken=packed;broken["root"]={{"$zima_cache",packed["values"].size()}};
        require(reject(broken),"Dangling cache reference accepted");
        broken=packed;broken["values"][0]={{"$zima_cache",std::size_t(0)}};
        broken["root"]={{"$zima_cache",std::size_t(0)}};
        require(reject(broken),"Cyclic cache reference accepted");
        broken=packed;broken["root"]={{"$zima_cache",-1}};
        require(reject(broken),"Negative cache reference accepted");
        const json empty={{"a",json::array()},{"b",nullptr},{"c",json::object()}};
        require(zima::document::unpack_cache_storage(zima::document::pack_cache_storage(empty))==empty,
            "Empty cache data changed");
        json nested={{"unicode","\xc3\xa1\xc3\x9f\xd0\xb6"},{"zero",-0.0},{"positive_zero",0.0},
            {"boundaries",json::array({std::string(253,'a'),std::string(254,'a'),std::string(255,'a')})}};
        for(int depth=0;depth<24;++depth)nested={{"child",std::move(nested)},{"empty",json::array()}};
        const auto nested_packed=zima::document::pack_cache_storage(nested);
        auto owned_nested=nested;
        require(zima::document::pack_cache_storage(std::move(owned_nested)).dump()==nested_packed.dump(),"Consuming nested cache changed thresholds or signed zero");
        require(nested_packed.dump()==copying_pack(nested).dump(),"Nested/threshold cache bytes changed");
        require(zima::document::unpack_cache_storage(nested_packed).dump()==nested.dump(),"Nested cache changed signed zero or Unicode");
        auto large_strings=json::array();
        for(int i=0;i<600;++i)large_strings.push_back(std::string(4096,'x')+std::to_string(i)+"\n\"\\\xc3\xa1");
        const auto distinct=large_strings;
        for(const auto& value:distinct)large_strings.push_back(value);
        const auto strings_packed=zima::document::pack_cache_storage(large_strings);
        auto owned_strings=large_strings;
        require(zima::document::pack_cache_storage(std::move(owned_strings)).dump()==strings_packed.dump(),"Consuming string cache lost pool lifetime or block identity");
        require(strings_packed.dump()==copying_pack(large_strings).dump(),"Large-string indexing changed cache bytes after pool growth");
        require(zima::document::unpack_cache_storage(strings_packed).dump()==large_strings.dump(),"Large-string pool views lost data");
        bool invalid_rejected=false;
        try {static_cast<void>(zima::document::pack_cache_storage(json(std::string(5000,'x')+char(0xff))));}
        catch(const json::type_error&){invalid_rejected=true;}
        require(invalid_rejected,"Large-string pooling skipped UTF-8 validation");
        const auto rejects_pack=[](const json& value) {
            bool copied_rejected=false,consumed_rejected=false;
            try {static_cast<void>(zima::document::pack_cache_storage(value));}
            catch(const std::exception&){copied_rejected=true;}
            auto owned_value=value;
            try {static_cast<void>(zima::document::pack_cache_storage(std::move(owned_value)));}
            catch(const std::exception&){consumed_rejected=true;}
            return copied_rejected&&consumed_rejected;
        };
        require(rejects_pack(json{{"$zima_cache",0}}),"Cache writer accepted a reserved reference key");
        require(rejects_pack(json(std::string(5000,'x')+char(0xff))),"Cache writers disagree on invalid UTF-8");
        json deep=nullptr;
        for(int depth=0;depth<258;++depth)deep=json::array({std::move(deep)});
        require(rejects_pack(deep),"Cache writers disagree on the nesting limit");
        std::cout<<"Shared cache contracts passed; bytes "<<original.dump().size()
                 <<" -> "<<packed.dump().size()<<'\n';
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
