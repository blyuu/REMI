#include <remi/animation/Animator.hpp>

#include <functional>
#include <cmath>

using namespace DirectX;
using namespace gltf;

void Animator::SetModel(const Model* model)
{
    m_model = model;
    m_prevClip = -1; m_prevTime = 0; m_blend = 1;
    m_time = 0.0f;
    m_clip = 0;
    m_playing = true;
}

float Animator::Duration() const
{
    if (!HasClip()) return 0.0f;
    return m_model->animations[m_clip].duration;
}

int Animator::ClipCount() const
{
    return m_model ? static_cast<int>(m_model->animations.size()) : 0;
}

const char* Animator::ClipName(int i) const
{
    if (!m_model || i < 0 || i >= static_cast<int>(m_model->animations.size())) return "";
    const std::string& n = m_model->animations[i].name;
    return n.empty() ? "(unnamed)" : n.c_str();
}

void Animator::SetClip(int i)
{
    if (i < 0 || i >= ClipCount()) return;
    m_clip = i;
    m_time = 0.0f;
    m_prevClip = -1;
    m_blend = 1.0f; // instant, no blend
}

void Animator::CrossfadeTo(int clip, float fadeSeconds)
{
    if (clip < 0 || clip >= ClipCount() || clip == m_clip) return;

    // Carry the normalized phase over so walk<->run cycles stay in sync.
    const float prevDur = (m_clip >= 0 && m_clip < ClipCount()) ? m_model->animations[m_clip].duration : 0.0f;
    const float phase   = (prevDur > 0.0f) ? std::fmod(m_time, prevDur) / prevDur : 0.0f;

    m_prevClip = m_clip;
    m_prevTime = m_time;
    m_clip     = clip;
    const float newDur = m_model->animations[clip].duration;
    m_time     = phase * newDur;
    m_blend    = 0.0f;
    m_fadeDur  = (fadeSeconds > 0.0001f) ? fadeSeconds : 0.0001f;
}

void Animator::Update(float dt)
{
    if (!HasClip() || !m_playing) return;
    const float dur = Duration();
    if (dur <= 0.0f) return;

    auto advance = [&](float& t, float clipDur)
    {
        t += dt * m_speed;
        if (clipDur > 0.0f)
        {
            if (m_loop) { t = std::fmod(t, clipDur); if (t < 0.0f) t += clipDur; }
            else if (t > clipDur) t = clipDur;
        }
    };

    advance(m_time, dur);
    if (m_blend < 1.0f && m_prevClip >= 0)
    {
        advance(m_prevTime, m_model->animations[m_prevClip].duration);
        m_blend += dt / m_fadeDur;
        if (m_blend >= 1.0f) { m_blend = 1.0f; m_prevClip = -1; }
    }
}

namespace {

// Sample a sampler at time t into a float[comps]. Clamps outside the range.
void SampleAt(const AnimSampler& s, float t, float* out)
{
    const int c = s.comps;
    const size_t n = s.times.size();
    if (n == 0) { for (int i = 0; i < c; ++i) out[i] = 0.0f; return; }

    if (t <= s.times.front()) { for (int i = 0; i < c; ++i) out[i] = s.values[i]; return; }
    if (t >= s.times.back())  { const size_t b = (n - 1) * c; for (int i = 0; i < c; ++i) out[i] = s.values[b + i]; return; }

    // Find the keyframe interval [k, k+1].
    size_t k = 0;
    while (k + 1 < n && s.times[k + 1] <= t) ++k;
    const float t0 = s.times[k], t1 = s.times[k + 1];
    const float u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    const float* a = &s.values[k * c];
    const float* b = &s.values[(k + 1) * c];

    if (s.step)
    {
        for (int i = 0; i < c; ++i) out[i] = a[i];
    }
    else if (s.cubic)
    {
        const float u2 = u * u, u3 = u2 * u;
        for (int i = 0; i < c; ++i)
            out[i] = (2*u3 - 3*u2 + 1) * a[i]
                + (u3 - 2*u2 + u) * (t1 - t0) * s.outTangents[k*c + i]
                + (-2*u3 + 3*u2) * b[i]
                + (u3 - u2) * (t1 - t0) * s.inTangents[(k+1)*c + i];
        if (c == 4)
        {
            XMFLOAT4 q(out[0], out[1], out[2], out[3]);
            XMStoreFloat4(&q, XMQuaternionNormalize(XMLoadFloat4(&q)));
            out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
        }
    }
    else if (c == 4)
    {
        // Rotation: slerp.
        XMVECTOR qa = XMVectorSet(a[0], a[1], a[2], a[3]);
        XMVECTOR qb = XMVectorSet(b[0], b[1], b[2], b[3]);
        XMVECTOR q  = XMQuaternionSlerp(qa, qb, u);
        XMFLOAT4 r; XMStoreFloat4(&r, q);
        out[0] = r.x; out[1] = r.y; out[2] = r.z; out[3] = r.w;
    }
    else
    {
        for (int i = 0; i < c; ++i) out[i] = a[i] + (b[i] - a[i]) * u;
    }
}

} // namespace

void Animator::SamplePose(int clip, float time,
                          std::vector<XMFLOAT3>& T, std::vector<XMFLOAT4>& R, std::vector<XMFLOAT3>& S) const
{
    const auto& nodes = m_model->nodes;
    const size_t nodeCount = nodes.size();
    // Start from each node's default local TRS (bind pose).
    for (size_t i = 0; i < nodeCount; ++i) { T[i] = nodes[i].translation; R[i] = nodes[i].rotation; S[i] = nodes[i].scale; }

    if (clip < 0 || clip >= static_cast<int>(m_model->animations.size())) return;
    const Animation& anim = m_model->animations[clip];
    for (const AnimChannel& ch : anim.channels)
    {
        if (ch.node < 0 || ch.node >= static_cast<int>(nodeCount) || ch.path < 0) continue;
        if (ch.sampler < 0 || ch.sampler >= static_cast<int>(anim.samplers.size())) continue;
        float v[4] = { 0, 0, 0, 1 };
        SampleAt(anim.samplers[ch.sampler], time, v);
        if      (ch.path == 0) T[ch.node] = { v[0], v[1], v[2] };
        else if (ch.path == 1) R[ch.node] = { v[0], v[1], v[2], v[3] };
        else if (ch.path == 2) S[ch.node] = { v[0], v[1], v[2] };
    }
}

void Animator::ComputeBoneMatrices(std::vector<XMFLOAT4X4>& outBones) const
{
    outBones.clear();
    if (!m_model || !m_model->HasSkeleton()) return;

    const auto& nodes = m_model->nodes;
    const auto& skin  = m_model->skin;
    const size_t nodeCount = nodes.size();

    // 1) Sample the current clip's pose (blended with the previous clip if fading).
    std::vector<XMFLOAT3> T(nodeCount), S(nodeCount);
    std::vector<XMFLOAT4> R(nodeCount);
    if (HasClip()) SamplePose(m_clip, m_time, T, R, S);
    else for (size_t i = 0; i < nodeCount; ++i) { T[i] = nodes[i].translation; R[i] = nodes[i].rotation; S[i] = nodes[i].scale; }

    if (m_blend < 1.0f && m_prevClip >= 0)
    {
        std::vector<XMFLOAT3> Tp(nodeCount), Sp(nodeCount);
        std::vector<XMFLOAT4> Rp(nodeCount);
        SamplePose(m_prevClip, m_prevTime, Tp, Rp, Sp);
        const float w = m_blend; // weight of the current clip
        for (size_t i = 0; i < nodeCount; ++i)
        {
            XMStoreFloat3(&T[i], XMVectorLerp(XMLoadFloat3(&Tp[i]), XMLoadFloat3(&T[i]), w));
            XMStoreFloat3(&S[i], XMVectorLerp(XMLoadFloat3(&Sp[i]), XMLoadFloat3(&S[i]), w));
            XMStoreFloat4(&R[i], XMQuaternionSlerp(XMLoadFloat4(&Rp[i]), XMLoadFloat4(&R[i]), w));
        }
    }

    // 3) Local matrices, then global = local * parentGlobal (row-vector).
    std::vector<XMMATRIX> global(nodeCount);
    std::vector<bool> done(nodeCount, false);
    // Iterative resolve honoring parent-before-child via a simple recursion.
    std::function<XMMATRIX(int)> resolve = [&](int i) -> XMMATRIX
    {
        if (done[i]) return global[i];
        XMMATRIX local = XMMatrixScalingFromVector(XMLoadFloat3(&S[i]))
                       * XMMatrixRotationQuaternion(XMLoadFloat4(&R[i]))
                       * XMMatrixTranslationFromVector(XMLoadFloat3(&T[i]));
        global[i] = (nodes[i].parent >= 0) ? local * resolve(nodes[i].parent) : local;
        done[i] = true;
        return global[i];
    };
    for (size_t i = 0; i < nodeCount; ++i) resolve(static_cast<int>(i));

    // 4) boneMatrix[j] = inverseBind[j] * jointGlobal[j] (row-vector).
    const size_t jointCount = skin.joints.size();
    if (outBones.size() < jointCount) outBones.resize(jointCount);
    for (size_t j = 0; j < jointCount; ++j)
    {
        XMMATRIX ib = XMLoadFloat4x4(&skin.inverseBind[j]);
        XMMATRIX g  = global[skin.joints[j]];
        XMStoreFloat4x4(&outBones[j], ib * g);
    }
}
