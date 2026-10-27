// Included by the shared native cache codec. Compression changes storage only;
// the existing JSON DAG reader remains the sole expansion implementation.
#include <openssl/evp.h>
#include <zstd.h>
#include <zlib.h>
#include <array>
#include <limits>

namespace {
using CacheJson=nlohmann::json;
constexpr std::size_t compression_chunk=48*1024;
[[noreturn]] void invalid_compressed_cache() {
    throw std::runtime_error("Invalid compressed document data.");
}

std::string cache_base64_encode(std::string_view bytes) {
    std::string result;
    result.reserve((bytes.size()+2)/3*4);
    for(std::size_t at=0;at<bytes.size();) {
        const auto count=std::min(compression_chunk,bytes.size()-at);
        std::array<unsigned char,compression_chunk/3*4+1> buffer;
        const int length=EVP_EncodeBlock(buffer.data(),
            reinterpret_cast<const unsigned char*>(bytes.data()+at),static_cast<int>(count));
        result.append(reinterpret_cast<const char*>(buffer.data()),length);
        at+=count;
    }
    return result;
}

std::string cache_base64_decode(std::string_view text) {
    if(text.empty()||text.size()%4)invalid_compressed_cache();
    std::string result;
    result.reserve(text.size()/4*3);
    for(std::size_t at=0;at<text.size();) {
        const auto count=std::min(compression_chunk,text.size()-at);
        const auto part=text.substr(at,count);
        for(std::size_t i=0;i<count;++i) {
            const unsigned char c=part[i];
            const bool alphabet=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                (c>='0'&&c<='9')||c=='+'||c=='/';
            if(!alphabet && !(c=='='&&at+count==text.size()&&i>=count-2))
                invalid_compressed_cache();
        }
        const std::size_t padding=(part.back()=='=')+(part.size()>1&&part[part.size()-2]=='=');
        if(padding==1&&part[part.size()-2]=='=')invalid_compressed_cache();
        std::array<unsigned char,compression_chunk> buffer;
        const int length=EVP_DecodeBlock(buffer.data(),
            reinterpret_cast<const unsigned char*>(part.data()),static_cast<int>(count));
        if(length<0||static_cast<std::size_t>(length)<padding)invalid_compressed_cache();
        const auto decoded=std::string_view(reinterpret_cast<const char*>(buffer.data()),length-padding);
        // Reject noncanonical padding bits as well as misplaced padding.
        if(padding&&cache_base64_encode(decoded)!=part)invalid_compressed_cache();
        result.append(decoded);
        at+=count;
    }
    return result;
}

struct CacheZlibStream {
    z_stream stream{};
    bool encoding;
    explicit CacheZlibStream(bool encode):encoding(encode) {
        const int status=encode?deflateInit(&stream,Z_BEST_SPEED):inflateInit(&stream);
        if(status==Z_MEM_ERROR)throw std::bad_alloc();
        if(status!=Z_OK)invalid_compressed_cache();
    }
    ~CacheZlibStream() { if(encoding)deflateEnd(&stream);else inflateEnd(&stream); }
};

std::string cache_zlib(std::string_view input,bool encode,std::size_t expected=0) {
    CacheZlibStream owner(encode);
    auto& stream=owner.stream;
    std::string result;
    if(!encode)result.reserve(expected);
    std::array<char,compression_chunk> output;
    std::size_t at=0;
    for(;;) {
        if(stream.avail_in==0&&at<input.size()) {
            const auto count=std::min(compression_chunk,input.size()-at);
            stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()+at));
            stream.avail_in=static_cast<uInt>(count);
            at+=count;
        }
        stream.next_out=reinterpret_cast<Bytef*>(output.data());
        stream.avail_out=static_cast<uInt>(output.size());
        const int status=encode?deflate(&stream,at==input.size()?Z_FINISH:Z_NO_FLUSH):
            inflate(&stream,Z_NO_FLUSH);
        if(status==Z_MEM_ERROR)throw std::bad_alloc();
        const auto produced=output.size()-stream.avail_out;
        if(!encode&&(result.size()>expected||produced>expected-result.size()))
            invalid_compressed_cache();
        result.append(output.data(),produced);
        if(status==Z_STREAM_END) {
            if(stream.avail_in||at!=input.size()||(!encode&&result.size()!=expected))
                invalid_compressed_cache();
            return result;
        }
        if(status!=Z_OK||(!produced&&!stream.avail_in&&at==input.size()))
            invalid_compressed_cache();
    }
}

struct CacheZstdContext {
    ZSTD_CCtx* context{ZSTD_createCCtx()};
    CacheZstdContext() { if(!context)throw std::bad_alloc(); }
    ~CacheZstdContext() { ZSTD_freeCCtx(context); }
};

std::string cache_zstd_compress(std::string_view input) {
    CacheZstdContext owner;
    if(ZSTD_isError(ZSTD_CCtx_setParameter(owner.context,
            ZSTD_c_compressionLevel,1)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(owner.context,
            ZSTD_c_checksumFlag,1)))invalid_compressed_cache();
    std::string result(ZSTD_compressBound(input.size()),'\0');
    const auto size=ZSTD_compress2(owner.context,result.data(),result.size(),
        input.data(),input.size());
    if(ZSTD_isError(size))invalid_compressed_cache();
    result.resize(size);
    return result;
}

std::string cache_zstd_decompress(std::string_view input,std::size_t expected) {
    if(input.empty() ||
       ZSTD_getFrameContentSize(input.data(),input.size())!=expected ||
       ZSTD_findFrameCompressedSize(input.data(),input.size())!=input.size())
        invalid_compressed_cache();
    std::string result(expected,'\0');
    const auto size=ZSTD_decompress(result.data(),result.size(),
        input.data(),input.size());
    if(ZSTD_isError(size)||size!=expected)invalid_compressed_cache();
    return result;
}

CacheJson compress_cache_storage(CacheJson&& storage) {
    // Tiny inputs retain the same current raw representation. Estimate first
    // to avoid serializing a small template twice merely to reject compression.
    if(storage.at("values").empty())return std::move(storage);
    const auto profile=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto raw=storage.dump();
    const auto dumped=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if(raw.size()<64*1024)return std::move(storage);
    const auto compressed=cache_zstd_compress(raw);
    const auto deflated=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if(compressed.size()>(raw.size()-raw.size()/10)/4*3)return std::move(storage);
    auto encoded=cache_base64_encode(compressed);
    if(profile) {
        const auto finished=std::chrono::steady_clock::now();
        const auto milliseconds=[](auto from,auto to) {
            return std::chrono::duration<double,std::milli>(to-from).count();
        };
        std::fprintf(stderr,
            "Native IO cache compression dump: %.3f ms; zstd: %.3f ms; base64: %.3f ms; raw=%zu compressed=%zu\n",
            milliseconds(started,dumped),milliseconds(dumped,deflated),
            milliseconds(deflated,finished),raw.size(),compressed.size());
    }
    return {{"format","zima-shared-cache-v2"},{"compression","zstd-base64"},
        {"uncompressed_bytes",raw.size()},{"data",std::move(encoded)}};
}

CacheJson decompress_cache_storage(CacheJson&& storage) {
    if(!storage.is_object()||!storage.contains("compression"))return std::move(storage);
    const auto format=storage.value("format",std::string{});
    const auto compression=storage.value("compression",std::string{});
    const bool zstd=format=="zima-shared-cache-v2"&&compression=="zstd-base64";
    const bool legacy_zlib=format=="zima-shared-cache-v1"&&compression=="zlib-base64";
    if(storage.size()!=4||(!zstd&&!legacy_zlib)||!storage.contains("data")||
       !storage.at("data").is_string()||!storage.contains("uncompressed_bytes")||
       !storage.at("uncompressed_bytes").is_number_unsigned())invalid_compressed_cache();
    const auto expected=storage.at("uncompressed_bytes").get<std::uint64_t>();
    if(expected>std::numeric_limits<std::size_t>::max())invalid_compressed_cache();
    const auto profile=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto compressed=cache_base64_decode(storage.at("data").get_ref<const std::string&>());
    const auto decoded_base64=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto raw=zstd
        ? cache_zstd_decompress(compressed,static_cast<std::size_t>(expected))
        : cache_zlib(compressed,false,static_cast<std::size_t>(expected));
    const auto inflated=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    auto decoded=CacheJson::parse(raw,nullptr,false);
    if(profile) {
        const auto parsed=std::chrono::steady_clock::now();
        std::fprintf(stderr,"Native IO cache decompression base64: %.3f ms; %s: %.3f ms; JSON: %.3f ms\n",
            std::chrono::duration<double,std::milli>(decoded_base64-started).count(),
            zstd?"zstd":"zlib",
            std::chrono::duration<double,std::milli>(inflated-decoded_base64).count(),
            std::chrono::duration<double,std::milli>(parsed-inflated).count());
    }
    if(decoded.is_discarded()||!decoded.is_object()||decoded.contains("compression"))
        invalid_compressed_cache();
    return decoded;
}
}
