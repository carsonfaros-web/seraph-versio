// seraph.h - Seraph engine, C++ port of the web prototype.
// Header-only, no libDaisy dependency, so it can also be built and tested on a computer.
//
// Six voices, each a cluster of four slightly detuned copies, drifting in pitch and level.
// Dark 24 dB low-pass, sub sine, eight-line feedback reverb. Measured against the album "Seraphim" (see README).
//
// Notes work like the Sdkc Instruments Helical: every voice plays a note, and when it ends the next pitch
// is worked out from the length of the last note, and the next length from the new pitch (higher notes last
// longer). The same seed always gives the same pattern. Rate sets how long notes are; Lock repeats them.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace seraph
{
constexpr int kVoices = 12;
constexpr int kCopies = 2;
constexpr int kOsc    = kVoices * kCopies;
constexpr int kLines  = 8;
constexpr int kBuf    = 16384; // samples per reverb line (0.34 s at 48 kHz)
constexpr int kTick   = 64;    // control update interval in samples

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
    int   scale  = 4;    // 0..7, dark to bright: open fifths, minor pent, hirajoshi, phrygian, dorian, major pent, lydian, whole tone
    int   dens   = 6;    // active voices, 1..12
    float clus   = 16.f; // cluster spread, cents
    float drift  = 0.5f;
    float timbre = 0.6f; // 0 sine .. 1 saw
    float breath = 0.1f; // depth of the slow random level wander
    float rate   = 2.4f; // seconds per note at the root; the higher the pitch, the longer the note (up to 2.8x)
    float spread = 1.f;  // pitch range above the root: 0 = root only, 1 = three octaves
    int   env    = 0;    // note shape: 0 soft swell, 1 even, 2 pluck
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
    float vpan   = 0.f;  // 0 = each voice's copies spread across the stereo field, 1 = each voice sits hard left or right
    float strike = 0.f;  // 0 = timbre fixed, 1 = each note starts at the Wave timbre and rings down to a pure sine
    float level  = 0.7f;
    float glide  = 0.35f; // seconds, note glide (root glide is capped at 2 s)
    float inLevel = 0.f; // external audio into the filter and reverb
    float gain   = 1.f;  // voice level trim, so the Wave knob does not change the loudness
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
        mt_ = mdb_ = mtg_ = 0.f;
        act_ = 0.f;
        rr_  = 0;
        sig_ = -1;
        wetS_ = P.space * 1.1f;
        dryS_ = 1.f - 0.5f * P.space;
        memset(envS_, 0, sizeof(envS_));
        memset(tv_, 0, sizeof(tv_));
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
        const int lo[kVoices] = {0, 0, 0, 0, 7, 7, 12, 12, 12, 14, 19, 19};
        const float vg[kVoices] = {0.85f, 1.f, 0.85f, 1.f, 0.85f, 0.8f, 0.7f, 0.7f, 0.6f, 0.6f, 0.5f, 0.5f};
        for(int s = 0; s < kVoices; s++)
        {
            lo_[s] = lo[s];
            vg_[s] = vg[s];
        }
        rootS_ = P.root;
        Reseed(seed, true);
        Tick();
        for(int i = 0; i < kLines; i++)
            len_[i] = lenT_[i];
    }

    // New seed: every voice starts a new note now. immediate = true snaps to it, false glides there.
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
            y_[s]   = Rnd();
            dm0_[s] = 1.f;
            StartNote(s, true);
            if(immediate)
            {
                semi_[s] = stgt_[s];
                gate_[s] = s < P.dens ? 1.f : 0.f;
                xph_[s]  = Rnd() * 0.9f; // spread the voices through their notes
            }
            atm_[s] = Rnd() * 5.f;
            atg_[s] = -Rnd() * 6.f;
            if(immediate)
                ampdb_[s] = atg_[s];
        }
    }

    // A clock or gate: the next voice in turn starts a new note now (unless Lock is on, then it just repeats).
    void Advance()
    {
        const int n = P.dens < 1 ? 1 : (P.dens > kVoices ? kVoices : P.dens);
        rr_         = (rr_ + 1) % n;
        StartNote(rr_, false);
    }

    // Forces every parameter into its valid range, so no input can make the engine unstable.
    void Sanitize()
    {
        P.root    = Cl(P.root, 0.f, 127.f);
        P.scale   = (int)Cl((float)P.scale, 0.f, 7.f);
        P.dens    = (int)Cl((float)P.dens, 1.f, (float)kVoices);
        P.clus    = Cl(P.clus, 0.f, 60.f);
        P.drift   = Cl(P.drift, 0.f, 1.f);
        P.timbre  = Cl(P.timbre, 0.f, 1.f);
        P.breath  = Cl(P.breath, 0.f, 1.f);
        P.rate    = Cl(P.rate, 0.05f, 120.f);
        P.spread  = Cl(P.spread, 0.f, 1.f);
        P.env     = (int)Cl((float)P.env, 0.f, 2.f);
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
        P.glide   = Cl(P.glide, 0.005f, 60.f);
        P.inLevel = Cl(P.inLevel, 0.f, 2.f);
        P.gain    = Cl(P.gain, 0.f, 2.f);
        P.vpan    = Cl(P.vpan, 0.f, 1.f);
        P.strike  = Cl(P.strike, 0.f, 1.f);
    }

    void  SetFrozen(bool f) { frozen_ = f; }
    bool  Frozen() const { return frozen_; }
    float MasterDb() const { return mdb_; }
    float Wander() const { return fw_; }
    float Activity() const { return act_; } // average note envelope of the active voices, 0..1

    void Render(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        Sanitize();
        const float wetT = P.space * 1.1f;
        const float dryT = 1.f - 0.5f * P.space;
        const float lv   = P.level;
        const float inG  = P.inLevel;
        const float md   = 0.0006f * sr_;
        for(int k = 0; k < n; k++)
        {
            if(tc_-- <= 0)
            {
                Tick();
                tc_ = kTick - 1;
            }
            float l = 0.f, r = 0.f;
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
                const float Tv = tv_[i / kCopies];
                const float s  = (1.f - Tv) * FastSin01(p) + Tv * saw;
                l += s * gl;
                r += s * gr;
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
            for(int i = 0; i < kLines; i++)
                buf_[i][w_] = ((i & 1) ? fr : fl) * 0.5f + v[i] * 0.35355339f;
            w_ = w_ + 1 >= kBuf ? 0 : w_ + 1;
            const float wl = (d[0] + d[2] + d[4] + d[6]) * 0.5f;
            const float wr = (d[1] + d[3] + d[5] + d[7]) * 0.5f;
            // one-pole high-pass at about 15 Hz removes any DC before the output stage
            wetS_ += (wetT - wetS_) * 0.002f; // about 10 ms, so a switch flip does not click
            dryS_ += (dryT - dryS_) * 0.002f;
            const float ml = dryS_ * fl + wetS_ * wl, mr = dryS_ * fr + wetS_ * wr;
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

    // Notes the voice may play, in semitones above the root, for the current scale and spread.
    int Cands(int s, int* c) const
    {
        static const int sc[8][7]  = {{0, 7, 14, -1, -1, -1, -1},   // open fifths
                                      {0, 3, 5, 7, 10, -1, -1},     // minor pentatonic
                                      {0, 2, 3, 7, 8, -1, -1},      // hirajoshi
                                      {0, 1, 3, 5, 7, 8, 10},       // phrygian
                                      {0, 2, 3, 5, 7, 9, 10},       // dorian
                                      {0, 2, 4, 5, 7, 9, 11},       // major (ionian)
                                      {0, 2, 4, 6, 7, 9, 11},       // lydian
                                      {0, 2, 4, 6, 8, 10, -1}};     // whole tone
        static const int scn[8]    = {3, 5, 5, 7, 7, 7, 7, 6};
        const int        si        = P.scale < 0 ? 0 : (P.scale > 7 ? 7 : P.scale);
        const float      sp        = P.spread < 0.f ? 0.f : (P.spread > 1.f ? 1.f : P.spread);
        const int        hi0       = (int)(sp * 36.f + 0.5f);
        const int        lo        = (int)((float)lo_[s] * sp);
        const int        hi        = hi0 < lo ? lo : hi0;
        int              nc        = 0;
        for(int o = 0; o <= 36; o += 12)
            for(int j = 0; j < scn[si]; j++)
            {
                const int v = sc[si][j] + o;
                if(v >= lo && v <= hi && nc < 40)
                    c[nc++] = v;
            }
        if(!nc)
            c[nc++] = 0;
        return nc;
    }

    float PitchFromY(int s, float y) const
    {
        int c[40];
        const int nc = Cands(s, c);
        int       i  = (int)(y * (float)nc);
        if(i < 0)
            i = 0;
        if(i >= nc)
            i = nc - 1;
        return (float)c[i];
    }

    // Start the voice's next note. Free: the next pitch comes from the last note length, the next length from the
    // new pitch. Locked: the same pitch and length repeat. force = true always works out a new note.
    void StartNote(int s, bool force)
    {
        if(force || !frozen_)
        {
            float y = y_[s] * 3.1f + dm0_[s] * 2.3f + 0.137f;
            y -= floorf(y);
            y_[s]   = y;
            yn_[s]  = y;
            const float pit = PitchFromY(s, y);
            stgt_[s]        = pit;
            dm0_[s]         = sqrtf(exp2f(pit / 12.f));
            dm_[s]          = dm0_[s] * (0.97f + 0.06f * Rnd());
        }
        xph_[s] = 0.f;
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
        const float tauG  = P.glide < 0.005f ? 0.005f : P.glide;
        const float tauR  = tauG < 0.03f ? 0.03f : (tauG > 2.f ? 2.f : tauG);
        rootS_ += (P.root - rootS_) * (1.f - expf(-dt / tauR));
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
        const float kgate = 1.f - expf(-dt / 0.25f);
        const float rateS = P.rate < 0.05f ? 0.05f : P.rate;
        {
            // scale or spread changed: every voice re-picks its current note at once, from the same chain value
            const int sig = P.scale * 100 + (int)(P.spread * 64.f);
            if(sig != sig_)
            {
                sig_ = sig;
                for(int s = 0; s < kVoices; s++)
                    stgt_[s] = PitchFromY(s, yn_[s]);
            }
        }
        static const float kEA[3] = {0.5f, 0.3f, 0.01f};  // attack share of the note
        static const float kEP[3] = {1.2f, 1.6f, 3.0f};   // decay curve
        static const float kEF[3] = {0.18f, 0.f, 0.f};    // level the note never drops below
        const int          ev    = P.env < 0 ? 0 : (P.env > 2 ? 2 : P.env);
        float              actS  = 0.f;
        const float kA    = 1.f - expf(-dt / (1.f + 3.f * (1.f - P.breath)));
        const float kc    = 1.f - expf(-dt / 3.f);
        const float wd    = P.width;
        for(int s = 0; s < kVoices; s++)
        {
            const float dur = rateS * dm_[s];
            xph_[s] += dt / dur;
            if(xph_[s] >= 1.f)
            {
                xph_[s] -= floorf(xph_[s]);
                StartNote(s, false);
            }
            {
                float a = kEA[ev];
                const float amin = 0.008f / dur; // never faster than 8 ms, so no click
                if(a < amin)
                    a = amin;
                if(a > 0.5f)
                    a = 0.5f;
                const float x = xph_[s];
                float       e;
                if(x < a)
                {
                    const float t = x / a;
                    e             = t * t * (3.f - 2.f * t);
                }
                else
                    e = powf(1.f - (x - a) / (1.f - a), kEP[ev]);
                const float amp = kEF[ev] + (1.f - kEF[ev]) * e;
                envS_[s] += (amp - envS_[s]) * 0.4f;
            }
            tv_[s] = P.timbre * (1.f - P.strike * (1.f - envS_[s] * envS_[s])); // bright at the strike, pure as it rings down
            semi_[s] += (stgt_[s] - semi_[s]) * kg;
            gate_[s] += ((s < P.dens ? 1.f : 0.f) - gate_[s]) * kgate;
            atm_[s] -= dt;
            if(atm_[s] <= 0.f)
            {
                atm_[s] = (10.f - 7.5f * P.breath) * (0.6f + 0.8f * Rnd());
                atg_[s] = -Rnd() * (4.f + 18.f * P.breath);
            }
            ampdb_[s] += (atg_[s] - ampdb_[s]) * kA;
            const float vl = gate_[s] * vg_[s] * envS_[s] * powf(10.f, ampdb_[s] / 20.f) * mg * 0.75f * P.gain; // 0.75 keeps six dense voices out of the limiter
            if(s < P.dens)
                actS += envS_[s] * gate_[s];
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
                const float cc    = kCopies > 1 ? ((float)c - 0.5f * (float)(kCopies - 1)) / (0.5f * (float)(kCopies - 1)) : 0.f; // -1 .. +1
                const float cents = P.clus * cc * 0.75f + cw_[i] * P.clus * (0.25f + 0.5f * P.drift);
                const float f     = rootHz_ * exp2f(semi_[s] / 12.f + cents / 1200.f);
                inc_[i]           = f / sr_;
                const float pan0  = ((c % 2 == 0) ? -1.f : 1.f) * (c < 2 ? 1.f : 0.55f) * ((s % 2) ? -1.f : 1.f);
                const float pan   = (pan0 + (((s % 2) ? 1.f : -1.f) - pan0) * P.vpan) * wd;
                const float th    = (pan + 1.f) * 0.78539816f;
                gL_[i]            = cosf(th) * vl;
                gR_[i]            = sinf(th) * vl;
            }
        }
        act_     = actS / (float)(P.dens < 1 ? 1 : P.dens);
        binc_[0] = rootHz_ * exp2f(4.f / 1200.f) / sr_;
        binc_[1] = rootHz_ * exp2f(-4.f / 1200.f) / sr_;
        bodyG_   = P.body * 0.2f;
        // low-pass biquad
        float fc = 120.f * exp2f(P.dark * 5.5f + fw_ * 0.7f * P.drift + (rootS_ - 34.f) / 24.f); // follows the root by half
        {
            const float topHz = rootHz_ * exp2f(P.spread * 3.f); // never close the filter on the highest note the voices can play
            if(fc < 1.4f * topHz)
                fc = 1.4f * topHz;
        }
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
    }

    // ---- state ----
    float     sr_ = 48000.f;
    float (*buf_)[kBuf] = nullptr;
    uint32_t  rs_ = 1;
    int       tc_ = 0;
    bool      frozen_ = false;
    int       lo_[kVoices];
    float     tv_[kVoices] = {0};
    float     xph_[kVoices], dm_[kVoices], dm0_[kVoices], y_[kVoices], yn_[kVoices], envS_[kVoices];
    int       rr_ = 0, sig_ = -1;
    float     act_ = 0.f, wetS_ = 0.f, dryS_ = 1.f;
    float     vg_[kVoices];
    float     ph_[kOsc], inc_[kOsc], gL_[kOsc], gR_[kOsc], cw_[kOsc], ctg_[kOsc], ct_[kOsc];
    float     semi_[kVoices], stgt_[kVoices];
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
};

} // namespace seraph
