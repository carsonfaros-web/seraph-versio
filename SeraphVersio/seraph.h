// seraph.h - Seraph engine, C++ port of the web prototype.
// Header-only, no libDaisy dependency, so it can also be built and tested on a computer.
//
// Six voices, each a cluster of four slightly detuned copies, drifting in pitch and level.
// Dark 24 dB low-pass, sub sine, eight-line feedback reverb. Slow random swells and
// slow harmony changes. Measured against the album "Seraphim" (see README).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace seraph
{
constexpr int kVoices = 6;
constexpr int kCopies = 4;
constexpr int kOsc    = kVoices * kCopies;
constexpr int kLines  = 8;
constexpr int kBuf    = 16384; // samples per reverb line (0.34 s at 48 kHz)
constexpr int kTick   = 64;    // control update interval in samples
constexpr float kFoldRef = 0.6f; // folder reference level (about the typical oscillator mix peak, set by measurement)

// sin(2*pi*p) for p in [0,1). Parabolic approximation with one refinement step.
inline float FastSin01(float p)
{
    float t = p < 0.5f ? p : p - 1.f;
    float u = 2.f * t;
    float y = 4.f * u * (1.f - fabsf(u));
    return 0.225f * (y * fabsf(y) - y) + y;
}

inline float FastTanh(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float Cl(float x, float lo, float hi)
{
    if(!(x == x)) // NaN
        return lo;
    return x < lo ? lo : (x > hi ? hi : x);
}

struct Params
{
    float root   = 34.f; // MIDI note of the root (34 = A#1)
    int   scale  = 1;    // 0 minor pentatonic, 1 dorian, 2 lydian, 3 open fifths
    int   dens   = 5;    // active voices, 1..6
    float clus   = 16.f; // cluster spread, cents
    float drift  = 0.5f;
    float timbre = 0.6f; // 0 sine .. 1 saw
    float breath = 0.6f;
    float harm   = 0.4f; // how often voices change note
    float dark   = 0.46f;
    float body   = 0.5f;
    float space  = 0.65f;
    float decay  = 0.6f;
    float size   = 0.5f;
    float damp   = 0.4f;
    float wow    = 0.4f;
    float tune   = 0.f; // cents
    float noise  = 0.15f;
    float width  = 1.f;
    float level  = 0.7f;
    float glide  = 4.f;  // seconds, note glide and root glide
    float inLevel = 0.f; // external audio into the filter and reverb
    float warp   = 0.f;  // 0 off. Cross-modulation between the oscillators: phase mod first, then ring mod
    float shift  = 0.f;  // -1..1, 0 off. Frequency shifter inside the reverb loop, +-30 Hz
    float fold   = 0.f;  // 0 off. Soft wave folder ahead of the filter
};

// Quadrature pair of allpass chains (Niemitalo design): output q leads output i by 90 degrees from about 60 Hz
// to nearly Nyquist. Used by the frequency shifter.
struct Hilbert
{
    void Reset()
    {
        for(int c = 0; c < 2; c++)
        {
            dl_[c] = 0.f;
            for(int s = 0; s < 4; s++)
                x1_[c][s] = x2_[c][s] = y1_[c][s] = y2_[c][s] = 0.f;
        }
    }
    void Process(float in, float& i, float& q)
    {
        static const float k[2][4] = {
            {0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f, 0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f},
            {0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f, 0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f}};
        float out[2];
        for(int c = 0; c < 2; c++)
        {
            float a = in;
            for(int s = 0; s < 4; s++)
            {
                const float y = k[c][s] * (a + y2_[c][s]) - x2_[c][s];
                x2_[c][s]     = x1_[c][s];
                x1_[c][s]     = a;
                y2_[c][s]     = y1_[c][s];
                y1_[c][s]     = y;
                a             = y;
            }
            out[c] = a;
        }
        i   = dl_[0]; // the first chain is delayed by one sample
        dl_[0] = out[0];
        q   = out[1];
    }
    float x1_[2][4], x2_[2][4], y1_[2][4], y2_[2][4], dl_[2];
};

class Engine
{
  public:
    Params P;

    void Init(float sr, float (*buf)[kBuf], uint32_t seed)
    {
        sr_  = sr;
        buf_ = buf;
        for(int i = 0; i < kLines; i++)
            memset(buf_[i], 0, sizeof(float) * kBuf); // SDRAM .bss is not zeroed at start-up
        memset(z_, 0, sizeof(z_));
        memset(ph_, 0, sizeof(ph_));
        memset(ds_, 0, sizeof(ds_));
        w_ = 0;
        br_[0] = br_[1] = nl_[0] = nl_[1] = 0.f;
        hx_[0] = hx_[1] = hy_[0] = hy_[1] = 0.f;
        bph_[0] = bph_[1] = 0.f;
        hil_.Reset();
        shPh_ = shInc_ = shMix_ = 0.f;
        mt_ = mdb_ = mtg_ = 0.f;
        wow_ = wtg_ = wtm_ = fw_ = ftg_ = ftm_ = 0.f;
        tph_ = 0.f;
        tc_  = 0;
        frozen_ = false;
        static const float kBase[kLines] = {31.1f, 37.7f, 43.3f, 50.9f, 59.1f, 67.3f, 76.9f, 89.3f};
        for(int i = 0; i < kLines; i++)
        {
            baseMs_[i] = kBase[i];
            lf_[i]     = 0.8f * i;
            lr_[i]     = (0.07f + 0.021f * i) / sr_;
            len_[i]    = baseMs_[i] * 0.001f * sr_;
        }
        const int lo[kVoices] = {0, 0, 7, 12, 12, 19};
        const int hi[kVoices] = {7, 14, 19, 24, 27, 28};
        const float vg[kVoices] = {0.85f, 1.f, 0.85f, 0.7f, 0.6f, 0.5f};
        for(int s = 0; s < kVoices; s++)
        {
            lo_[s] = lo[s];
            hi_[s] = hi[s];
            vg_[s] = vg[s];
        }
        rootS_ = P.root;
        Reseed(seed, true);
        Tick();
        for(int i = 0; i < kLines; i++)
            len_[i] = lenT_[i];
    }

    // New random harmony. immediate = true snaps to it, false glides there.
    void Reseed(uint32_t seed, bool immediate)
    {
        rs_ = seed * 2654435761u + 12345u;
        if(!rs_)
            rs_ = 1;
        for(int i = 0; i < kOsc; i++)
        {
            ph_[i]  = Rnd();
            ctg_[i] = Rnd() * 2.f - 1.f;
            ct_[i]  = Rnd() * 6.f;
            if(immediate)
                cw_[i] = ctg_[i];
        }
        for(int s = 0; s < kVoices; s++)
        {
            Pick(s);
            if(immediate)
            {
                semi_[s] = stgt_[s];
                gate_[s] = s < P.dens ? 1.f : 0.f;
            }
            stm_[s] = Rnd() * 20.f;
            atm_[s] = Rnd() * 5.f;
            atg_[s] = -Rnd() * 6.f;
            if(immediate)
                ampdb_[s] = atg_[s];
        }
    }

    // One random active voice moves to a new note now (use for a clock or gate).
    void Advance()
    {
        int n = P.dens < 1 ? 1 : (P.dens > kVoices ? kVoices : P.dens);
        int s = (int)(Rnd() * n);
        if(s >= n)
            s = n - 1;
        Pick(s);
        stm_[s] = (120.f - 105.f * P.harm) * (0.5f + Rnd());
    }

    // Forces every parameter into its valid range, so no input can make the engine unstable.
    void Sanitize()
    {
        P.root    = Cl(P.root, 0.f, 127.f);
        P.scale   = (int)Cl((float)P.scale, 0.f, 3.f);
        P.dens    = (int)Cl((float)P.dens, 1.f, (float)kVoices);
        P.clus    = Cl(P.clus, 0.f, 60.f);
        P.drift   = Cl(P.drift, 0.f, 1.f);
        P.timbre  = Cl(P.timbre, 0.f, 1.f);
        P.breath  = Cl(P.breath, 0.f, 1.f);
        P.harm    = Cl(P.harm, 0.f, 1.f);
        P.dark    = Cl(P.dark, 0.f, 1.f);
        P.body    = Cl(P.body, 0.f, 1.f);
        P.space   = Cl(P.space, 0.f, 1.f);
        P.decay   = Cl(P.decay, 0.f, 1.f);
        P.size    = Cl(P.size, 0.f, 1.f);
        P.damp    = Cl(P.damp, 0.f, 1.f);
        P.wow     = Cl(P.wow, 0.f, 1.f);
        P.tune    = Cl(P.tune, -100.f, 100.f);
        P.noise   = Cl(P.noise, 0.f, 1.f);
        P.width   = Cl(P.width, 0.f, 1.f);
        P.level   = Cl(P.level, 0.f, 1.f);
        P.glide   = Cl(P.glide, 0.2f, 60.f);
        P.inLevel = Cl(P.inLevel, 0.f, 2.f);
        P.warp    = Cl(P.warp, 0.f, 1.f);
        P.shift   = Cl(P.shift, -1.f, 1.f);
        P.fold    = Cl(P.fold, 0.f, 1.f);
    }

    void  SetFrozen(bool f) { frozen_ = f; }
    bool  Frozen() const { return frozen_; }
    float MasterDb() const { return mdb_; }
    float Wander() const { return fw_; }

    void Render(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        Sanitize();
        const float T    = P.timbre;
        const float wet  = P.space * 1.1f;
        const float dry  = 1.f - 0.5f * P.space;
        const float lv   = P.level;
        const float inG  = P.inLevel;
        const float md   = 0.0006f * sr_;
        // timbre stages, all neutral (and bit-exact to the old sound) when their control is at zero
        const float pmIdx = (P.warp * 2.f < 1.f ? P.warp * 2.f : 1.f) * 0.5f;
        const float rmMix = (P.warp * 2.f > 1.f ? P.warp * 2.f - 1.f : 0.f) * 0.6f;
        const float fold  = P.fold;
        const float fDrv  = (1.f + 3.f * fold) / kFoldRef;
        for(int k = 0; k < n; k++)
        {
            if(tc_-- <= 0)
            {
                Tick();
                tc_ = kTick - 1;
            }
            float l = 0.f, r = 0.f;
            float prev = 0.f; // the previous oscillator modulates the next one
            for(int i = 0; i < kOsc; i++)
            {
                float p = ph_[i] + inc_[i];
                if(p >= 1.f)
                    p -= 1.f;
                ph_[i] = p;
                const float gl = gL_[i], gr = gR_[i];
                if(gl + gr < 1e-6f)
                    continue;
                const float dt  = inc_[i];
                float       saw = 2.f * p - 1.f;
                if(p < dt)
                {
                    float t = p / dt;
                    saw -= t + t - t * t - 1.f;
                }
                else if(p > 1.f - dt)
                {
                    float t = (p - 1.f) / dt;
                    saw -= t * t + t + t + 1.f;
                }
                float       q  = p + pmIdx * prev; // phase modulation of the sine part
                q -= floorf(q);
                const float s0 = (1.f - T) * FastSin01(q) + T * saw;
                const float s  = s0 * (1.f - rmMix) + rmMix * 2.f * s0 * prev; // ring modulation with the oscillator before
                prev           = s0;
                l += s * gl;
                r += s * gr;
            }
            if(fold > 0.f)
            {
                // soft sine folder: quiet swells stay clean, loud ones fold over
                float ul = l * fDrv * 0.15915494f, ur = r * fDrv * 0.15915494f;
                ul -= floorf(ul);
                ur -= floorf(ur);
                l = (1.f - fold) * l + fold * 0.8f * kFoldRef * FastSin01(ul);
                r = (1.f - fold) * r + fold * 0.8f * kFoldRef * FastSin01(ur);
            }
            for(int c = 0; c < 2; c++)
            {
                float b = bph_[c] + binc_[c];
                if(b >= 1.f)
                    b -= 1.f;
                bph_[c] = b;
            }
            l += FastSin01(bph_[0]) * bodyG_;
            r += FastSin01(bph_[1]) * bodyG_;
            for(int c = 0; c < 2; c++)
            {
                // band-limited noise (about 80 to 530 Hz): difference of two one-pole filters, so no DC or sub-audio
                const float wn = Rnd() * 2.f - 1.f;
                br_[c] += 0.01f * (wn - br_[c]);
                nl_[c] += 0.07f * (wn - nl_[c]);
            }
            l += (nl_[0] - br_[0]) * noiseG_ * 40.f;
            r += (nl_[1] - br_[1]) * noiseG_ * 40.f;
            if(inG > 0.f)
            {
                l += inL[k] * inG;
                r += inR[k] * inG;
            }
            // two biquad low-pass stages per channel (transposed direct form II)
            float x = l, y;
            y = b_[0] * x + z_[0][0];
            z_[0][0] = b_[1] * x - a_[0] * y + z_[0][1];
            z_[0][1] = b_[2] * x - a_[1] * y;
            x = y;
            y = b_[0] * x + z_[0][2];
            z_[0][2] = b_[1] * x - a_[0] * y + z_[0][3];
            z_[0][3] = b_[2] * x - a_[1] * y;
            const float fl = y;
            x = r;
            y = b_[0] * x + z_[1][0];
            z_[1][0] = b_[1] * x - a_[0] * y + z_[1][1];
            z_[1][1] = b_[2] * x - a_[1] * y;
            x = y;
            y = b_[0] * x + z_[1][2];
            z_[1][2] = b_[1] * x - a_[0] * y + z_[1][3];
            z_[1][3] = b_[2] * x - a_[1] * y;
            const float fr = y;
            // eight-line feedback delay network
            float d[kLines], v[kLines];
            for(int i = 0; i < kLines; i++)
            {
                lfp_[i] += lr_[i];
                if(lfp_[i] >= 1.f)
                    lfp_[i] -= 1.f;
                float rp = (float)w_ - len_[i] - FastSin01(lfp_[i]) * md;
                while(rp < 0.f)
                    rp += (float)kBuf;
                int         i0 = (int)rp;
                const float fr2 = rp - (float)i0;
                if(i0 >= kBuf)
                    i0 -= kBuf;
                const int   i1 = i0 + 1 >= kBuf ? 0 : i0 + 1;
                const float dd = buf_[i][i0] * (1.f - fr2) + buf_[i][i1] * fr2;
                d[i]           = dd;
                ds_[i] += da_ * (dd - ds_[i]);
                v[i] = ds_[i] * g_[i];
            }
            for(int h = 1; h < kLines; h *= 2)
                for(int j = 0; j < kLines; j += 2 * h)
                    for(int q = j; q < j + h; q++)
                    {
                        const float aa = v[q], bb = v[q + h];
                        v[q]     = aa + bb;
                        v[q + h] = aa - bb;
                    }
            {
                // frequency shifter in the loop: crossfade the common (all-lines) part of the feedback with a
                // shifted copy of itself. The shifter keeps level, so the loop cannot get louder.
                float vm = 0.f;
                for(int i = 0; i < kLines; i++)
                    vm += v[i];
                vm *= 0.125f;
                float hi, hq;
                hil_.Process(vm, hi, hq);
                if(shMix_ > 0.f)
                {
                    shPh_ += shInc_;
                    shPh_ -= floorf(shPh_);
                    float cp = shPh_ + 0.25f;
                    cp -= floorf(cp);
                    const float sh = hi * FastSin01(cp) + hq * FastSin01(shPh_);
                    const float dv = shMix_ * (sh - vm);
                    for(int i = 0; i < kLines; i++)
                        v[i] += dv;
                }
            }
            for(int i = 0; i < kLines; i++)
                buf_[i][w_] = ((i & 1) ? fr : fl) * 0.5f + v[i] * 0.35355339f;
            w_ = w_ + 1 >= kBuf ? 0 : w_ + 1;
            const float wl = (d[0] + d[2] + d[4] + d[6]) * 0.5f;
            const float wr = (d[1] + d[3] + d[5] + d[7]) * 0.5f;
            // one-pole high-pass at about 15 Hz removes any DC before the output stage
            const float ml = dry * fl + wet * wl, mr = dry * fr + wet * wr;
            const float yl = ml - hx_[0] + 0.998f * hy_[0];
            const float yr = mr - hx_[1] + 0.998f * hy_[1];
            hx_[0] = ml; hy_[0] = yl; hx_[1] = mr; hy_[1] = yr;
            outL[k]        = FastTanh(lv * yl);
            outR[k]        = FastTanh(lv * yr);
        }
    }

  private:
    // ---- random ----
    float Rnd()
    {
        uint32_t s = rs_;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        if(!s)
            s = 1;
        rs_ = s;
        return (float)(s >> 8) * (1.f / 16777216.f);
    }

    void Pick(int s)
    {
        static const int sc[4][7]  = {{0, 3, 5, 7, 10, -1, -1},
                                      {0, 2, 3, 5, 7, 9, 10},
                                      {0, 2, 4, 6, 7, 9, 11},
                                      {0, 7, 14, -1, -1, -1, -1}};
        static const int scn[4] = {5, 7, 7, 3};
        int              si = P.scale < 0 ? 0 : (P.scale > 3 ? 3 : P.scale);
        int              c[40];
        int              nc = 0;
        for(int o = 0; o <= 36; o += 12)
            for(int j = 0; j < scn[si]; j++)
            {
                int v = sc[si][j] + o;
                if(v >= lo_[s] && v <= hi_[s] && nc < 40)
                    c[nc++] = v;
            }
        if(!nc)
            c[nc++] = 0;
        int v = c[(int)(Rnd() * nc) % nc];
        if(nc > 1 && (float)v == stgt_[s])
            v = c[(int)(Rnd() * nc) % nc];
        stgt_[s] = (float)v;
    }

    void Tick()
    {
        Sanitize();
        const float dt = (float)kTick / sr_;
        // slow global wanders
        wtm_ -= dt;
        if(wtm_ <= 0.f)
        {
            wtm_ = 6.f + Rnd() * 10.f;
            wtg_ = Rnd() * 2.f - 1.f;
        }
        wow_ += (wtg_ - wow_) * (1.f - expf(-dt / 5.f));
        ftm_ -= dt;
        if(ftm_ <= 0.f)
        {
            ftm_ = 8.f + Rnd() * 14.f;
            ftg_ = Rnd() * 2.f - 1.f;
        }
        fw_ += (ftg_ - fw_) * (1.f - expf(-dt / 6.f));
        tph_ += 2.2f * dt;
        if(tph_ > 6.28318530718f)
            tph_ -= 6.28318530718f;
        const float tuneC = P.tune + wow_ * P.wow * 12.f + sinf(tph_) * P.wow * 1.2f;
        const float tauG  = P.glide < 0.2f ? 0.2f : P.glide;
        rootS_ += (P.root - rootS_) * (1.f - expf(-dt / tauG));
        rootHz_ = 440.f * exp2f((rootS_ - 69.f) / 12.f + tuneC / 1200.f);
        // master swell
        mt_ -= dt;
        if(mt_ <= 0.f)
        {
            mt_  = 25.f + Rnd() * 35.f;
            mtg_ = -Rnd() * (6.f + 16.f * P.breath);
        }
        mdb_ += (mtg_ - mdb_) * (1.f - expf(-dt / 8.f));
        const float mg    = powf(10.f, mdb_ / 20.f);
        const float kg    = 1.f - expf(-dt / tauG);
        const float kgate = 1.f - expf(-dt / 3.f);
        const float spanT = 120.f - 105.f * P.harm;
        const float kA    = 1.f - expf(-dt / (1.f + 3.f * (1.f - P.breath)));
        const float kc    = 1.f - expf(-dt / 3.f);
        const float wd    = P.width;
        for(int s = 0; s < kVoices; s++)
        {
            if(!frozen_)
            {
                stm_[s] -= dt;
                if(stm_[s] <= 0.f)
                {
                    stm_[s] = spanT * (0.5f + Rnd());
                    Pick(s);
                }
            }
            semi_[s] += (stgt_[s] - semi_[s]) * kg;
            gate_[s] += ((s < P.dens ? 1.f : 0.f) - gate_[s]) * kgate;
            atm_[s] -= dt;
            if(atm_[s] <= 0.f)
            {
                atm_[s] = (10.f - 7.5f * P.breath) * (0.6f + 0.8f * Rnd());
                atg_[s] = -Rnd() * (4.f + 18.f * P.breath);
            }
            ampdb_[s] += (atg_[s] - ampdb_[s]) * kA;
            const float vl = gate_[s] * vg_[s] * powf(10.f, ampdb_[s] / 20.f) * mg * 1.0f;
            for(int c = 0; c < kCopies; c++)
            {
                const int i = s * kCopies + c;
                ct_[i] -= dt;
                if(ct_[i] <= 0.f)
                {
                    ct_[i]  = (4.f + 8.f * Rnd()) / (0.3f + P.drift);
                    ctg_[i] = Rnd() * 2.f - 1.f;
                }
                cw_[i] += (ctg_[i] - cw_[i]) * kc;
                const float cents = P.clus * ((c - 1.5f) / 1.5f) * 0.75f + cw_[i] * P.clus * (0.25f + 0.5f * P.drift);
                const float f     = rootHz_ * exp2f(semi_[s] / 12.f + cents / 1200.f);
                inc_[i]           = f / sr_;
                const float pan   = ((c % 2 == 0) ? -1.f : 1.f) * (c < 2 ? 1.f : 0.55f) * ((s % 2) ? -1.f : 1.f) * wd;
                const float th    = (pan + 1.f) * 0.78539816f;
                gL_[i]            = cosf(th) * vl;
                gR_[i]            = sinf(th) * vl;
            }
        }
        binc_[0] = rootHz_ * exp2f(4.f / 1200.f) / sr_;
        binc_[1] = rootHz_ * exp2f(-4.f / 1200.f) / sr_;
        bodyG_   = P.body * 0.2f;
        // low-pass biquad
        float fc = 120.f * exp2f(P.dark * 5.5f + fw_ * 0.7f * P.drift);
        if(fc > sr_ * 0.4f)
            fc = sr_ * 0.4f;
        if(fc < 40.f)
            fc = 40.f;
        const float w0 = 6.28318530718f * fc / sr_, cs = cosf(w0), al = sinf(w0) / (2.f * 0.6f), a0 = 1.f + al;
        b_[0] = ((1.f - cs) * 0.5f) / a0;
        b_[1] = (1.f - cs) / a0;
        b_[2] = b_[0];
        a_[0] = (-2.f * cs) / a0;
        a_[1] = (1.f - al) / a0;
        // reverb
        const float RT = 2.f * powf(20.f, P.decay);
        const float sc = 0.6f + P.size * 2.4f;
        da_            = 1.f - expf(-6.28318530718f * (300.f * exp2f(P.damp * 5.f)) / sr_);
        const float maxLen = (float)kBuf - 8.f - 0.0006f * sr_;
        for(int i = 0; i < kLines; i++)
        {
            float t = baseMs_[i] * 0.001f * sr_ * sc;
            if(t > maxLen)
                t = maxLen;
            lenT_[i] = t;
            len_[i] += (t - len_[i]) * 0.05f;
            g_[i] = powf(10.f, -3.f * len_[i] / (RT * sr_));
        }
        noiseG_ = P.noise * 0.03f;
        // frequency shifter: quadratic taper, +-30 Hz, mix fades in over the first part of the knob
        const float sx = P.shift;
        shInc_ = (sx < 0.f ? -1.f : 1.f) * sx * sx * 30.f / sr_;
        const float ax = fabsf(sx);
        shMix_ = (ax * 4.f < 1.f ? ax * 4.f : 1.f) * 0.7f;
    }

    // ---- state ----
    float     sr_ = 48000.f;
    float (*buf_)[kBuf] = nullptr;
    uint32_t  rs_ = 1;
    int       tc_ = 0;
    bool      frozen_ = false;
    int       lo_[kVoices], hi_[kVoices];
    float     vg_[kVoices];
    float     ph_[kOsc], inc_[kOsc], gL_[kOsc], gR_[kOsc], cw_[kOsc], ctg_[kOsc], ct_[kOsc];
    float     semi_[kVoices], stgt_[kVoices], stm_[kVoices];
    float     ampdb_[kVoices], atg_[kVoices], atm_[kVoices], gate_[kVoices];
    float     wow_, wtg_, wtm_, fw_, ftg_, ftm_, tph_;
    float     mt_, mdb_, mtg_;
    float     rootS_ = 34.f, rootHz_ = 58.f;
    float     bph_[2], binc_[2], bodyG_ = 0.f;
    float     b_[3] = {0, 0, 0}, a_[2] = {0, 0};
    float     z_[2][4];
    float     br_[2], nl_[2], noiseG_ = 0.f;
    float     hx_[2] = {0, 0}, hy_[2] = {0, 0};
    int       w_ = 0;
    float     baseMs_[kLines], len_[kLines], lenT_[kLines], g_[kLines], ds_[kLines];
    float     lf_[kLines], lfp_[kLines] = {0}, lr_[kLines];
    float     da_ = 0.2f;
    Hilbert   hil_;
    float     shPh_ = 0.f, shInc_ = 0.f, shMix_ = 0.f;
};

} // namespace seraph
