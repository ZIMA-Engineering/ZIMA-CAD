// Numerical guide reuse belongs to one explicit history evaluation. Every
// lookup serializes the current edge and trimmed face without triangulations;
// TShape identity alone would miss in-place tolerance/geometry updates by OCCT.
// References are deliberately absent: callers publish the current identities.
struct EdgeGuideReuse {
    struct Key {
        const std::string* edge{};
        const std::string* face{};
        std::vector<std::uint64_t> parameters;
        bool operator==(const Key&)const=default;
    };
    struct Hash {
        std::size_t operator()(const Key& key)const {
            auto hash=std::hash<const void*>{}(key.edge);
            const auto combine=[&](std::size_t value){hash^=value+0x9e3779b9+(hash<<6)+(hash>>2);};
            combine(std::hash<const void*>{}(key.face));
            for(auto value:key.parameters)combine(std::hash<std::uint64_t>{}(value));
            return hash;
        }
    };
    // Intern complete signatures once; all edges of a large spline face use
    // the same immutable bytes. unordered_set node addresses survive rehash.
    std::unordered_set<std::string> signatures;
    std::unordered_map<Key,std::vector<Vec3>,Hash> values;
    std::size_t bytes{},hits{},misses{};
    static constexpr std::size_t budget=64*1024*1024;
    ~EdgeGuideReuse() {
        if(std::getenv("ZIMA_CPP_CALC_PROFILE"))std::fprintf(stderr,
            "Kernel edge guide reuse: hits=%zu misses=%zu bytes=%zu\n",hits,misses,bytes);
    }
    template<class Calculate>
    std::vector<Vec3> directions(const std::string& edge,const std::string& face,
            const std::vector<double>& parameters,Calculate&& calculate) {
        const auto intern=[&](const std::string& signature)->const std::string* {
            if(const auto found=signatures.find(signature);found!=signatures.end())return &*found;
            const auto cost=signature.size()+sizeof(std::string)+sizeof(void*)*4;
            if(cost>budget-bytes)return nullptr;
            const auto [found,inserted]=signatures.insert(signature);
            if(inserted)bytes+=cost;
            return &*found;
        };
        const auto* edge_key=intern(edge);const auto* face_key=intern(face);
        if(!edge_key||!face_key){++misses;return calculate();}
        Key key{edge_key,face_key,{}};key.parameters.reserve(parameters.size());
        for(double parameter:parameters)key.parameters.push_back(std::bit_cast<std::uint64_t>(parameter));
        const auto existing=values.find(key);
        if(existing!=values.end()) {
            ++hits;
            if(std::getenv("ZIMA_CPP_VERIFY_EDGE_GUIDE_REUSE")||std::getenv("ZIMA_CPP_VERIFY_FACE_CLASSIFIER_REUSE")) {
                const auto fresh=calculate();const auto& reused=existing->second;
                bool same=fresh.size()==reused.size();
                for(std::size_t i=0;same&&i<fresh.size();++i)
                    same=std::bit_cast<std::uint64_t>(fresh[i].x)==std::bit_cast<std::uint64_t>(reused[i].x)&&
                        std::bit_cast<std::uint64_t>(fresh[i].y)==std::bit_cast<std::uint64_t>(reused[i].y)&&
                        std::bit_cast<std::uint64_t>(fresh[i].z)==std::bit_cast<std::uint64_t>(reused[i].z);
                if(!same)throw std::logic_error("Edge guide reuse differs from the baseline");
            }
            return existing->second;
        }
        ++misses;auto result=calculate();
        const auto cost=key.parameters.size()*sizeof(std::uint64_t)+result.size()*sizeof(Vec3)+sizeof(Key)+sizeof(result)+sizeof(void*)*4;
        if(cost<=budget-bytes){bytes+=cost;values.emplace(std::move(key),result);}
        return result;
    }
};

// Shape maps ignore orientation. Keep a separate signature for each oriented
// use; location is part of the map key. Lifetime is a single fixed packet.
struct EdgeGuidePacketSignatures {
    TopTools_IndexedMapOfShape shapes;
    std::vector<std::array<std::optional<std::string>,4>> signatures;
    const std::string& get(const TopoDS_Shape& shape) {
        const auto index=static_cast<std::size_t>(shapes.Add(shape)-1);
        if(index>=signatures.size())signatures.resize(index+1);
        auto& value=signatures[index][static_cast<std::size_t>(shape.Orientation())];
        if(!value)value=serialize_kernel_shape(shape);
        return *value;
    }
};
