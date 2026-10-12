#include <remi/render/MaterialGraph.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace remi {
MaterialGraph::NodeId MaterialGraph::Make(Operation op, Vec3 value, NodeId a, NodeId b) {
    if (a) (void)Get(a);
    if (b) (void)Get(b);
    for (std::uint32_t i = 0; i < nodes_.size(); ++i) {
        auto& n = nodes_[i];
        if (n.live) continue;
        n.op = op; n.value = value; n.inputs = {a,b}; n.live = true;
        ++live_;
        return {i,n.generation};
    }
    nodes_.push_back(Node{op,value,{a,b},1,true});
    ++live_;
    return {static_cast<std::uint32_t>(nodes_.size()-1),1};
}
MaterialGraph::NodeId MaterialGraph::Constant(Vec3 value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        throw std::invalid_argument("Non-finite material graph constant");
    return Make(Operation::Constant,value);
}
MaterialGraph::NodeId MaterialGraph::InputTint() { return Make(Operation::InputTint,{}); }
MaterialGraph::NodeId MaterialGraph::InputMetallic() { return Make(Operation::InputMetallic,{}); }
MaterialGraph::NodeId MaterialGraph::InputRoughness() { return Make(Operation::InputRoughness,{}); }
MaterialGraph::NodeId MaterialGraph::Add(NodeId a, NodeId b) { return Make(Operation::Add,{},a,b); }
MaterialGraph::NodeId MaterialGraph::Multiply(NodeId a, NodeId b) { return Make(Operation::Multiply,{},a,b); }
MaterialGraph::NodeId MaterialGraph::OneMinus(NodeId input) { return Make(Operation::OneMinus,{},input); }
const MaterialGraph::Node& MaterialGraph::Get(NodeId id) const {
    if (!id || id.index >= nodes_.size() || !nodes_[id.index].live || nodes_[id.index].generation != id.generation)
        throw std::invalid_argument("Stale material graph node handle");
    return nodes_[id.index];
}
MaterialGraph::Node& MaterialGraph::Get(NodeId id) {
    return const_cast<Node&>(static_cast<const MaterialGraph&>(*this).Get(id));
}
void MaterialGraph::Connect(NodeId target, unsigned input, NodeId source) {
    if (input > 1) throw std::invalid_argument("Material graph input out of range");
    (void)Get(source);
    auto& node = Get(target);
    if (node.op == Operation::Constant || node.op == Operation::InputTint ||
        node.op == Operation::InputMetallic || node.op == Operation::InputRoughness ||
        (node.op == Operation::OneMinus && input != 0))
        throw std::invalid_argument("Material graph operation has no such input");
    node.inputs[input] = source;
}
void MaterialGraph::SetOutput(Output output, NodeId node) {
    if (static_cast<std::size_t>(output) >= outputs_.size()) throw std::invalid_argument("Invalid material output");
    if (node) (void)Get(node);
    outputs_[static_cast<std::size_t>(output)] = node;
}
Vec3 MaterialGraph::EvaluateNode(NodeId id, const MaterialProperties& base, std::vector<std::uint8_t>& states,
                                 std::vector<Vec3>& values) const {
    const auto& node = Get(id);
    auto& state = states[id.index];
    if (state == 1) throw std::logic_error("Cycle in material graph");
    if (state == 2) return values[id.index];
    state = 1;
    Vec3 result = node.value;
    if (node.op == Operation::InputTint) result = base.tint;
    else if (node.op == Operation::InputMetallic) result = {base.metallic,base.metallic,base.metallic};
    else if (node.op == Operation::InputRoughness) result = {base.roughness,base.roughness,base.roughness};
    else if (node.op != Operation::Constant) {
        if (!node.inputs[0]) throw std::logic_error("Disconnected material graph input");
        const auto a = EvaluateNode(node.inputs[0],base,states,values);
        if (node.op == Operation::OneMinus) result = {1-a.x,1-a.y,1-a.z};
        else {
            if (!node.inputs[1]) throw std::logic_error("Disconnected material graph input");
            const auto b = EvaluateNode(node.inputs[1],base,states,values);
            if (node.op == Operation::Add) result = {a.x+b.x,a.y+b.y,a.z+b.z};
            else result = {a.x*b.x,a.y*b.y,a.z*b.z};
        }
    }
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        throw std::overflow_error("Material graph result is not finite");
    state = 2;
    values[id.index] = result;
    return result;
}
MaterialProperties MaterialGraph::Evaluate(const MaterialProperties& base) const {
    MaterialProperties result = base;
    std::vector<std::uint8_t> states(nodes_.size());
    std::vector<Vec3> values(nodes_.size());
    auto read = [&](Output output) -> Vec3 {
        return EvaluateNode(outputs_[static_cast<std::size_t>(output)],base,states,values);
    };
    if (outputs_[0]) result.tint = read(Output::Tint);
    if (outputs_[1]) result.metallic = std::clamp(read(Output::Metallic).x,0.f,1.f);
    if (outputs_[2]) result.roughness = std::clamp(read(Output::Roughness).x,.04f,1.f);
    if (outputs_[3]) result.emissive = std::clamp(read(Output::Emissive).x,0.f,2.f);
    return result;
}
std::size_t MaterialGraph::Collect() {
    std::vector<std::uint8_t> marked(nodes_.size());
    auto mark = [&](auto&& self, NodeId id) -> void {
        if (!id) return;
        const auto& n = Get(id);
        if (marked[id.index]) return;
        marked[id.index] = 1;
        for (const auto edge : n.inputs) self(self,edge);
    };
    for (const auto root : outputs_) mark(mark,root);
    std::size_t reclaimed = 0;
    for (std::size_t i=0;i<nodes_.size();++i) {
        auto& n = nodes_[i];
        if (!n.live || marked[i]) continue;
        n.live = false; n.inputs = {}; ++n.generation; ++reclaimed; --live_;
        if (n.generation == 0) ++n.generation;
    }
    return reclaimed;
}
std::size_t MaterialGraph::LiveNodes() const noexcept { return live_; }
}
