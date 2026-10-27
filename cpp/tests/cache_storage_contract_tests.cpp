#include <zima/document/cache_storage.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <zlib.h>
#include <openssl/evp.h>
using nlohmann::json;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
// Independent one-shot decoder checks the bytes of the actual storage wrapper.
json raw_storage(const json& packed) {
    if(!packed.contains("compression"))return packed;
    const auto encoded=packed.at("data").get<std::string>();
    std::string compressed(encoded.size()/4*3,'\0');
    auto length=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(compressed.data()),
        reinterpret_cast<const unsigned char*>(encoded.data()),static_cast<int>(encoded.size()));
    require(length>=0,"Independent base64 decoding failed");
    length-=encoded.back()=='=';length-=encoded[encoded.size()-2]=='=';
    compressed.resize(length);
    auto expected=packed.at("uncompressed_bytes").get<uLongf>();
    std::string raw(expected,'\0');
    require(uncompress(reinterpret_cast<Bytef*>(raw.data()),&expected,
        reinterpret_cast<const Bytef*>(compressed.data()),static_cast<uLong>(compressed.size()))==Z_OK,
        "Independent zlib decoding failed");
    require(expected==raw.size(),"Independent decoded length changed");
    return json::parse(raw);
}
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
// Independent expanded-tree oracle, including reference order and depth checks.
json recursive_decode(const json& source,const json& values,std::size_t limit,std::size_t depth=0) {
    if(depth>256)throw std::runtime_error("depth");
    if(source.is_object()&&source.contains("$zima_cache")) {
        const auto& id=source.at("$zima_cache");
        if(source.size()!=1||!id.is_number_unsigned()||id.get<std::size_t>()>=limit)
            throw std::runtime_error("reference");
        const auto index=id.get<std::size_t>();
        return recursive_decode(values.at(index),values,index,depth+1);
    }
    auto result=source;
    if(result.is_structured())for(auto& child:result)child=recursive_decode(child,values,limit,depth+1);
    return result;
}
void check_readers(const json& storage) {
    const auto raw=raw_storage(storage);
    bool expected_rejected=false;
    json expected;
    try {expected=recursive_decode(raw.at("root"),raw.at("values"),raw.at("values").size());}
    catch(const std::exception&){expected_rejected=true;}
    for(bool consume:{false,true}) {
        auto input=storage;
        bool rejected=false;
        json actual;
        try {actual=consume?zima::document::unpack_cache_storage(std::move(input)):
                           zima::document::unpack_cache_storage(input);}
        catch(const std::exception&){rejected=true;}
        require(rejected==expected_rejected,"Cache readers disagree on malformed or deep data");
        if(!rejected)require(actual.dump()==expected.dump(),"Cache reader changed exact expanded data");
        if(!consume)require(input.dump()==storage.dump(),"Read-only cache decoding changed its input");
    }
}
int main() {
    try {
        zima::kernel::BodyResult body;
        body.kernel_shape=std::string(8192,'B');
        body.source_fingerprint="stable-input";
        body.volume=17.25;
        body.mesh.vertices={{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
        body.mesh.triangles={0,1,2,1,3,2};
        zima::kernel::FaceReference face{"body","face:top",{}};
        face.surface_result=true;face.sheet_role=zima::kernel::SheetFaceRole::SideA;
        face.sheet_thickness=2.;face.sheet_owner="sheet";face.display_owner_id="display";
        body.mesh.triangle_references={face,face};
        zima::kernel::ViewerEdge edge;
        edge.points={{0,0,0},{.5,.25,0},{1,0,0}};
        edge.reference={"body","edge:top",{},"display"};edge.display_owner_id="display";
        edge.color="#123456";edge.surface_result=true;edge.parameter_seam=true;
        edge.edge_treatment_owner_ids={"fillet"};
        edge.edge_treatment_side_directions={
            {{0,1,0},{0,1,0},{0,1,0}},{{0,0,1},{0,0,1},{0,0,1}}};
        auto thickness_face=face;thickness_face.semantic_key="face:thickness";
        thickness_face.sheet_role=zima::kernel::SheetFaceRole::ThicknessFace;
        edge.edge_treatment_side_references={face,thickness_face};
        edge.edge_treatment_endpoint_references={
            {"body","vertex:start",{}},{"body","vertex:end",{}}};
        edge.measured_length=1.125;
        body.mesh.edges.push_back(edge);
        body.mesh.points.push_back({{.25,.5,.75},{"body","vertex:middle",{}}});
        body.mesh.points.back().surface_result=true;
        const auto body_packet=zima::document::serialize_body_result(body,false,false);
        require(body_packet.at("viewer_binary_encoding")=="f64le-u32le-base64-v2"&&
            body_packet.at("face_references").size()==1&&
            !body_packet.contains("triangle_references")&&!body_packet.at("edge_points_binary").empty(),
            "Viewer v2 did not compact repeated faces and edge coordinates");
        const auto body_roundtrip=zima::document::load_body_result(body_packet);
        require(zima::document::serialize_body_result(body_roundtrip,false,false)==body_packet,
            "Viewer v2 changed exact topology identities, sheet sides, edge metadata or points");
        auto different=body;different.volume=18.5;
        if(!different.kernel_shape.shares_with(body.kernel_shape))
            throw std::runtime_error("Calculated snapshots copied opaque archive bytes");
        auto changed_archive=different.kernel_shape.get();changed_archive.back()='C';
        different.kernel_shape=std::move(changed_archive);
        if(body.kernel_shape.get().back()!='B'||different.kernel_shape.shares_with(body.kernel_shape))
            throw std::runtime_error("Replacing an opaque archive changed its original snapshot");
        auto aggregate=body;
        aggregate.body_boundaries["body"]={body,different,body,different,body,different,body,different};
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
        require(raw_storage(packed).dump()==copying_pack(original).dump(),"Cache allocation optimization changed stored bytes or block order");
        // Round-trip the actual on-disk representation, not only in-memory JSON.
        const auto restored=zima::document::unpack_cache_storage(json::parse(packed.dump()));
        require(restored==original,"Shared cache changed geometry, history or occurrence identity");
        require(packed.dump().size()<original.dump().size()/4,"Identical cache blocks were not shared");
        const auto loaded=zima::document::load_body_result(restored["components"][0]["calculated_source"]);
        require(loaded.body_boundaries.at("body").size()==8 &&
            loaded.body_inputs.at("body")->kernel_shape==body.kernel_shape &&
            loaded.body_outputs.at("body")->kernel_shape==different.kernel_shape,
            "Distinct history states were merged or lost");
        require(zima::document::pack_cache_storage(original)==packed,"Cache encoding is not deterministic");
        const auto reject=[](json storage) {
            try {static_cast<void>(zima::document::unpack_cache_storage(storage));return false;}
            catch(const std::exception&){return true;}
        };
        const auto raw_packed=raw_storage(packed);
        auto broken=raw_packed;broken["root"]={{"$zima_cache",raw_packed["values"].size()}};
        require(reject(broken),"Dangling cache reference accepted");
        broken=raw_packed;broken["values"][0]={{"$zima_cache",std::size_t(0)}};
        broken["root"]={{"$zima_cache",std::size_t(0)}};
        require(reject(broken),"Cyclic cache reference accepted");
        broken=raw_packed;broken["root"]={{"$zima_cache",-1}};
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
        require(raw_storage(strings_packed).dump()==copying_pack(large_strings).dump(),"Large-string indexing changed cache bytes after pool growth");
        require(strings_packed.contains("compression"),"Large compressible cache was not compressed");
        std::size_t corruption_case=0;
        for(const auto& mutate:std::vector<std::function<void(json&)>>{
            [](json& p){p["compression"]="unknown";},
            [](json& p){p["uncompressed_bytes"]=std::size_t(1);},
            [](json& p){p["uncompressed_bytes"]=p.at("uncompressed_bytes").get<std::size_t>()+1;},
            [](json& p){p["uncompressed_bytes"]=-1;},
            [](json& p){p["data"]="!!!!";},
            [](json& p){auto s=p.at("data").get<std::string>();s.resize(s.size()-4);p["data"]=s;},
            [](json& p){auto s=p.at("data").get<std::string>();s[s.size()-4]=s[s.size()-4]=='A'?'B':'A';p["data"]=s;},
            [](json& p){p["data"]=p.at("data").get<std::string>()+"AAAA";},
            [](json& p){p["root"]=nullptr;}}) {
            auto corrupt=strings_packed;mutate(corrupt);
            if(!reject(corrupt))throw std::runtime_error("Compressed corruption case accepted: "+std::to_string(corruption_case));
            ++corruption_case;
        }
        const json signed_sides={{"left",-0.0},{"right",0.0},{"payload",std::string(100000,'S')},
            {"unicode","\xc3\xa1\xd0\xb6"},{"id",std::uint64_t(9007199254740993ULL)}};
        const auto sides_packed=zima::document::pack_cache_storage(signed_sides);
        require(sides_packed.contains("compression")&&
            zima::document::unpack_cache_storage(sides_packed).dump()==signed_sides.dump(),
            "Compression changed signed zero, Unicode or integer identity");
        std::string noise;std::uint32_t random_state=19;
        for(std::size_t i=0;i<120000;++i) {
            random_state=random_state*1664525U+1013904223U;
            noise.push_back(static_cast<char>(32+(random_state>>8)%95));
        }
        const auto noisy=zima::document::pack_cache_storage(json(noise));
        require(!noisy.contains("compression")&&
            zima::document::unpack_cache_storage(noisy)==json(noise),
            "Adaptive compression enlarged incompressible data or changed its bytes");
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
        for(const auto* fixture:{&packed,&nested_packed,&strings_packed})check_readers(*fixture);
        // Repeated parents and descendants, including a root/child shared leaf.
        json shared={{"format","zima-shared-cache-v1"},
            {"values",json::array({json::array({-0.0,0.0,"side:left","side:right"}),
                json::array({json{{"$zima_cache",std::size_t(0)}},json{{"$zima_cache",std::size_t(0)}}})})},
            {"root",json::array({json{{"$zima_cache",std::size_t(1)}},json{{"$zima_cache",std::size_t(1)}},
                json{{"$zima_cache",std::size_t(0)}}})}};
        check_readers(shared);
        for(int depth:{251,252,253,254,255,256,257}) {
            auto at_depth=shared;
            for(int i=0;i<depth;++i)at_depth["root"]=json::array({std::move(at_depth["root"])});
            check_readers(at_depth);
        }
        for(int depth:{255,256,257}) {
            json plain=-0.0;
            for(int i=0;i<depth;++i)plain=json::array({std::move(plain)});
            check_readers(json{{"format","zima-shared-cache-v1"},{"values",json::array()},{"root",std::move(plain)}});
        }
        auto independent=zima::document::unpack_cache_storage(shared);
        independent[0][0][0]=7;
        require(independent[1][0][0].dump()=="-0.0"&&independent[2][0].dump()=="-0.0",
            "Editing one expanded reference changed another consumer");
        for(const auto& reference:{json(-1),json(0.0),json(std::size_t(999))}) {
            auto invalid=shared;invalid["root"]={{"$zima_cache",reference}};check_readers(invalid);
        }
        auto cycle=shared;cycle["values"][0]={{"$zima_cache",std::size_t(0)}};check_readers(cycle);
        auto extra=shared;extra["root"]={{"$zima_cache",std::size_t(0)},{"extra",1}};check_readers(extra);
        // A previously decoded block reached deeper must still enforce depth.
        auto deeper=shared;
        json deep_reference={{"$zima_cache",std::size_t(0)}};
        for(int i=0;i<256;++i)deep_reference=json::array({std::move(deep_reference)});
        deeper["root"]=json::array({json{{"$zima_cache",std::size_t(0)}},std::move(deep_reference)});
        check_readers(deeper);
        std::cout<<"Shared cache contracts passed; bytes "<<original.dump().size()
                 <<" -> "<<packed.dump().size()<<'\n';
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
