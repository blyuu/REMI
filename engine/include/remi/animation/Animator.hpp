// anim/Animator.h — samples a glTF animation and produces a bone palette.
#pragma once
#include <remi/assets/GltfLoader.hpp>
#include <DirectXMath.h>
#include <vector>

class Animator
{
public:
    void SetModel(const gltf::Model* model); // must have a skeleton
    bool HasClip() const { return m_model && !m_model->animations.empty() && m_model->HasSkeleton(); }

    void  Update(float dt);
    void  SetTime(float t) { m_time = t; }
    float Time() const     { return m_time; }
    float Duration() const;
    void  SetSpeed(float s) { m_speed = s; }
    float Speed() const     { return m_speed; }
    void  SetPlaying(bool p) { m_playing = p; }
    bool  Playing() const    { return m_playing; }
    void  SetLoop(bool l)    { m_loop = l; }
    bool  Loop() const       { return m_loop; }

    // Clip selection.
    int         ClipCount() const;
    const char* ClipName(int i) const;
    int         CurrentClip() const { return m_clip; }
    void        SetClip(int i);                       // instant switch (no blend)
    void        CrossfadeTo(int clip, float fadeSeconds = 0.2f); // smooth transition
    bool        Blending() const { return m_blend < 1.0f && m_prevClip >= 0; }

    // Fills `outBones` (>= joint count) with the skinning matrices for the current time.
    void ComputeBoneMatrices(std::vector<DirectX::XMFLOAT4X4>& outBones) const;

private:
    // Fills per-node local TRS for `clip` at `time` (bind pose for nodes without channels).
    void SamplePose(int clip, float time,
                    std::vector<DirectX::XMFLOAT3>& T,
                    std::vector<DirectX::XMFLOAT4>& R,
                    std::vector<DirectX::XMFLOAT3>& S) const;

    const gltf::Model* m_model = nullptr;
    float m_time    = 0.0f;
    float m_speed   = 1.0f;
    bool  m_playing = true;
    bool  m_loop    = true;
    int   m_clip    = 0;

    // Crossfade state (blend from the previous clip to the current one).
    int   m_prevClip = -1;
    float m_prevTime = 0.0f;
    float m_blend    = 1.0f;   // weight of the current clip; 1 = fully current
    float m_fadeDur  = 0.2f;
};
