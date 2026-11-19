// Material partitions are derived from authored attachment ancestry. They are
// calculation data, never another persisted identity or a spatial heuristic.
namespace sheet_branch_geometry {
using Branches=std::map<std::string,std::string>;

PrimitiveData compound(const std::vector<PrimitiveData>& inputs) {
    PrimitiveData result;TopoDS_Compound shape;BRep_Builder builder;builder.MakeCompound(shape);
    std::size_t count=0;
    for(const auto& input:inputs)if(!input.shape.IsNull()) {
        ++count;builder.Add(shape,input.shape);result.shape=input.shape;
        result.faces.insert(result.faces.end(),input.faces.begin(),input.faces.end());
        result.edges.insert(result.edges.end(),input.edges.begin(),input.edges.end());
        result.vertices.insert(result.vertices.end(),input.vertices.begin(),input.vertices.end());
    }
    if(count>1)result.shape=shape;
    return result;
}

std::map<std::string,PrimitiveData> partition(const PrimitiveData& input,const Branches& branches) {
    std::map<std::string,std::vector<PrimitiveData>> pieces;
    if(input.shape.IsNull())return {};
    const auto retain=[&](const TopoDS_Shape& shape) {
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> members;
        TopExp::MapShapes(shape,members);
        PrimitiveData piece;piece.shape=shape;
        const auto select=[&](auto& target,const auto& source) {
            for(const auto& value:source)if(members.Contains(value.shape))target.push_back(value);
        };
        select(piece.faces,input.faces);select(piece.edges,input.edges);select(piece.vertices,input.vertices);
        std::set<std::string> owners;
        for(const auto& face:piece.faces) {
            const auto found=branches.find(face.reference.sheet_owner);
            if(found!=branches.end()&&!found->second.empty())owners.insert(found->second);
        }
        // A prepared independent branch must not already contain a different
        // branch's material. Never silently pick one owner from a fused solid.
        if(owners.size()>1)throw std::runtime_error("Cannot retain the authored sheet material change.");
        pieces[owners.empty()?std::string{}:*owners.begin()].push_back(std::move(piece));
    };
    for(TopExp_Explorer it(input.shape,TopAbs_SOLID);it.More();it.Next())retain(it.Current());
    for(TopExp_Explorer it(input.shape,TopAbs_FACE,TopAbs_SOLID);it.More();it.Next())retain(it.Current());
    std::map<std::string,PrimitiveData> result;
    for(auto& [owner,values]:pieces)result.emplace(owner,compound(values));
    return result;
}

// Keep the caller's existing Boolean and topology policy inside each branch.
// Only the distribution and collection of independent jobs is shared here.
template<class Join>
PrimitiveData add(const PrimitiveData& input,const PrimitiveData& operand,
        const Branches& branches,Join&& join) {
    auto left=partition(input,branches);
    auto right=partition(operand,branches);
    for(const auto& [branch,piece]:right)left.try_emplace(branch);
    std::vector<std::pair<PrimitiveData,PrimitiveData>> groups;
    for(auto& [branch,piece]:left)groups.emplace_back(std::move(piece),std::move(right[branch]));
    std::vector<PrimitiveData> results(groups.size());
    const auto calculate=[&](std::size_t index) {
        auto& [a,b]=groups[index];
        results[index]=a.shape.IsNull()?std::move(b):b.shape.IsNull()?std::move(a):join(a,b);
    };
    if(groups.size()==1)calculate(0);
    else {
        std::atomic_size_t next{};
        std::vector<std::future<void>> jobs;
        const auto workers=std::min<std::size_t>(8,std::min<std::size_t>(groups.size(),
            std::max(2u,std::thread::hardware_concurrency())));
        for(std::size_t worker=0;worker<workers;++worker)
            jobs.push_back(std::async(std::launch::async,[&] {
                while(true) {
                    const auto index=next.fetch_add(1);if(index>=groups.size())break;
                    parallel_sheet_branch_jobs.fetch_add(1,std::memory_order_relaxed);
                    calculate(index);
                }
            }));
        for(auto& job:jobs)job.get();
    }
    return compound(results);
}
}
