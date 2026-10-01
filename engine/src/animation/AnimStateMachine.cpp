#include <remi/animation/AnimStateMachine.hpp>
#include <remi/animation/Animator.hpp>

int AnimStateMachine::AddState(const std::string& name, int clip)
{
    m_states.push_back({ name, clip });
    return static_cast<int>(m_states.size() - 1);
}

void AnimStateMachine::AddTransition(int from, int to, std::function<bool(const Params&)> condition, float fade)
{
    m_transitions.push_back({ from, to, std::move(condition), fade });
}

void AnimStateMachine::Start(Animator& animator, int state)
{
    if (state < 0 || state >= static_cast<int>(m_states.size())) return;
    m_current = state;
    animator.SetClip(m_states[state].clip);
}

void AnimStateMachine::Update(Animator& animator, float dt)
{
    // Fire the first transition out of the current (or any) state whose condition holds.
    for (const Transition& t : m_transitions)
    {
        if (t.to == m_current) continue;
        if (t.from != -1 && t.from != m_current) continue;
        if (t.cond && t.cond(params))
        {
            m_current = t.to;
            animator.CrossfadeTo(m_states[t.to].clip, t.fade);
            break;
        }
    }
    animator.Update(dt);
}

const char* AnimStateMachine::CurrentName() const
{
    return (m_current >= 0 && m_current < static_cast<int>(m_states.size())) ? m_states[m_current].name.c_str() : "";
}
