#include <remi/render/MaterialGraph.hpp>
#include <cmath>
#include <stdexcept>
namespace {
void Require(bool condition) { if (!condition) throw std::runtime_error("Material graph test failed"); }
}

int main() {
    remi::MaterialGraph graph;
    const auto source = graph.InputRoughness();
    const auto half = graph.Constant({.5f,.5f,.5f});
    const auto result = graph.Multiply(source,half);
    graph.SetOutput(remi::MaterialGraph::Output::Roughness,result);
    remi::MaterialProperties material;
    material.roughness = .8f;
    Require(std::abs(graph.Evaluate(material).roughness - .4f) < .0001f);
    Require(graph.Collect() == 0);

    // Unreachable cycles are reclaimable even though their nodes reference each other.
    const auto a = graph.Add(half,half);
    const auto b = graph.Multiply(a,half);
    graph.Connect(a,0,b);
    Require(graph.Collect() == 2);
    Require(graph.LiveNodes() == 3);
    bool stale = false;
    try { graph.SetOutput(remi::MaterialGraph::Output::Tint,a); }
    catch (const std::invalid_argument&) { stale = true; }
    Require(stale);

    // Rooted cycles remain alive but evaluation rejects them explicitly.
    const auto c = graph.Add(half,half);
    graph.Connect(c,0,c);
    graph.SetOutput(remi::MaterialGraph::Output::Tint,c);
    Require(graph.Collect() == 0);
    bool cycle = false;
    try { (void)graph.Evaluate(material); }
    catch (const std::logic_error&) { cycle = true; }
    Require(cycle);
    graph.SetOutput(remi::MaterialGraph::Output::Tint,{});
    Require(graph.Collect() == 1);
    Require(std::abs(graph.Evaluate(material).roughness - .4f) < .0001f);
}
