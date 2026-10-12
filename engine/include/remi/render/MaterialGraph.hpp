#pragma once
#include <remi/render/Material.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace remi {
// CPU material graph. GPU textures and scene entities retain their existing RAII ownership.
// The graph owns only node records; unreachable records, including cycles, are mark-swept.
class MaterialGraph {
public:
    struct NodeId {
        std::uint32_t index = UINT32_MAX;
        std::uint32_t generation = 0;
        friend bool operator==(NodeId, NodeId) = default;
        [[nodiscard]] explicit operator bool() const noexcept { return index != UINT32_MAX; }
    };
    enum class Operation { Constant, InputTint, InputMetallic, InputRoughness, Add, Multiply, OneMinus };
    enum class Output { Tint, Metallic, Roughness, Emissive, Count };

    [[nodiscard]] NodeId Constant(Vec3 value);
    [[nodiscard]] NodeId InputTint();
    [[nodiscard]] NodeId InputMetallic();
    [[nodiscard]] NodeId InputRoughness();
    [[nodiscard]] NodeId Add(NodeId a, NodeId b);
    [[nodiscard]] NodeId Multiply(NodeId a, NodeId b);
    [[nodiscard]] NodeId OneMinus(NodeId input);
    void Connect(NodeId target, unsigned input, NodeId source);
    void SetOutput(Output output, NodeId node);
    [[nodiscard]] MaterialProperties Evaluate(const MaterialProperties& base = {}) const;
    [[nodiscard]] std::size_t Collect();
    [[nodiscard]] std::size_t LiveNodes() const noexcept;
private:
    struct Node {
        Operation op = Operation::Constant;
        Vec3 value{};
        std::array<NodeId,2> inputs{};
        std::uint32_t generation = 1;
        bool live = false;
    };
    [[nodiscard]] NodeId Make(Operation op, Vec3 value, NodeId a = {}, NodeId b = {});
    [[nodiscard]] const Node& Get(NodeId id) const;
    [[nodiscard]] Node& Get(NodeId id);
    [[nodiscard]] Vec3 EvaluateNode(NodeId id, const MaterialProperties& base, std::vector<std::uint8_t>& states,
                                    std::vector<Vec3>& values) const;
    std::vector<Node> nodes_;
    std::array<NodeId,static_cast<std::size_t>(Output::Count)> outputs_{};
    std::size_t live_ = 0;
};
}
