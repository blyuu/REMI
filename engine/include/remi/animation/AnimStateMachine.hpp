// anim/AnimStateMachine.h — parameter-driven animation state machine.
// Like Unity's Animator Controller / Unreal's Anim Blueprint state machine: states
// map to clips, transitions fire on parameter conditions and auto-crossfade.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class Animator;

class AnimStateMachine
{
public:
    struct Params
    {
        std::unordered_map<std::string, float> values; // bools stored as 0/1
        float Get(const std::string& k) const { auto it = values.find(k); return it == values.end() ? 0.0f : it->second; }
    };

    int  AddState(const std::string& name, int clip);
    // from == -1 means "any state". First satisfied transition wins each Update.
    void AddTransition(int from, int to, std::function<bool(const Params&)> condition, float fade = 0.2f);

    void SetParam(const std::string& name, float value) { params.values[name] = value; }
    void SetBool(const std::string& name, bool value)   { params.values[name] = value ? 1.0f : 0.0f; }

    void Start(Animator& animator, int state);   // enter a state instantly
    void Update(Animator& animator, float dt);   // evaluate transitions + advance the animator

    int         Current() const { return m_current; }
    const char* CurrentName() const;

    Params params;

private:
    struct State { std::string name; int clip; };
    struct Transition { int from, to; std::function<bool(const Params&)> cond; float fade; };
    std::vector<State>      m_states;
    std::vector<Transition> m_transitions;
    int m_current = -1;
};
