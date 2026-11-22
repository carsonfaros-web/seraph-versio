// Seraph for Noise Engineering Versio.
//
// A drone and pad engine whose notes work like the Sdkc Instruments Helical: each voice plays a note, and when
// it ends the next pitch comes from the length of the last note, and the next length from the new pitch.
// One flat panel, no setup pages, no hidden values: every knob is live all the time. See README.md.
//
//   Knobs (7, all take CV):  1 Root | 2 Scale | 3 Voices | 4 Rate | 5 Glide | 6 Spread | 7 Wave
//   Switch 1:                Env    (left soft swell, centre even, right pluck)
//   Switch 2:                Space  (left room, centre hall, right vast)
//   Tap, short:              new seed (every voice starts a new note, new pattern)
//   Tap, hold 0.8 s or more: Lock on or off (notes and lengths repeat, scale and spread still work)
//   Gate in (Tap jack):      each rising edge starts the next voice's note, so a clock can play it
#include "daisy_versio.h"
#include "seraph.h"
#include <cmath>

using namespace daisy;

// ---- set to 1 to show CPU load on LED 4 while you test (needs libDaisy CpuLoadMeter) ----
#define SERAPH_CPU_METER 0
#if SERAPH_CPU_METER
#include "util/CpuLoadMeter.h"
static CpuLoadMeter gMeter;
#endif

static DaisyVersio hw;
static seraph::Engine gEng;
// 8 lines x 16384 samples x 4 bytes = 512 KB, kept in external SDRAM
static float DSY_SDRAM_BSS gBuf[seraph::kLines][seraph::kBuf];

// ---- output trim: lower this if the module clips your mixer, raise it for more level ----
static const float kOutTrim = 0.5f; // about -6 dB. Raise toward 1.0 if too quiet; the output is limited to full scale either way

// ---- the one place that reads a three-way switch. If your libDaisy names the positions
//      differently, this is the only function to change. Returns 0 up/left, 1 centre, 2 down/right. ----
static int SwPos(Switch3& s)
{
    int p = s.Read();
    if(p == Switch3::POS_UP)
        return 0;
    if(p == Switch3::POS_DOWN)
        return 2;
    return 1;
}

// ---- control state ----
static float gKnob[7];           // smoothed 0..1
static int   gRootN  = 12;       // root, 0..36 semitones above A#0
static int   gScaleI = 4;        // scale, 0..7
static int   gVoices = 5;        // voices, 1..6
static bool  gFirst  = true;     // first block: take knob positions as they are
static float gFade   = 0.f;      // slow fade-in after power-up

// Switch settings: Space is reverb amount, decay and size together
static const float kSpace[3] = {0.35f, 0.62f, 0.80f};
static const float kDecay[3] = {0.25f, 0.55f, 0.90f}; // about 4 s, 10 s, 30 s
static const float kSize[3]  = {0.35f, 0.50f, 0.75f};

// Tap button
static bool     gTapDown  = false;
static uint32_t gTapStart = 0;
static uint32_t gSeedCtr  = 1;

// Gate
static bool gGatePrev = false;

// Values the main loop reads for the LEDs
static volatile float gLedPeak   = 0.f;
static volatile float gLedFlash  = 0.f; // seed flash, decays
static volatile float gLedGate   = 0.f;
static volatile bool  gLedFrozen = false;
static volatile int   gLedScale  = 4;
static volatile float gLedAct    = 0.f;
#if SERAPH_CPU_METER
static volatile float gLedCpu = 0.f;
#endif

// Moves a stepped value to a new step only once the knob is well inside it, so noise on the knob or CV
// sitting on a step edge cannot make it flutter. pos is the knob 0..1, n the number of steps.
static int Step(float pos, int n, int cur)
{
    const float f = pos * (float)n;
    if(f > (float)cur - 0.12f && f < (float)(cur + 1) + 0.12f)
        return cur;
    int i = (int)f;
    return i < 0 ? 0 : (i > n - 1 ? n - 1 : i);
}

static void Audio(AudioHandle::InputBuffer  in,
                  AudioHandle::OutputBuffer out,
                  size_t                    size)
{
#if SERAPH_CPU_METER
    gMeter.OnBlockStart();
#endif
    hw.ProcessAllControls();
    // libDaisy's ProcessAllControls() only reads the knobs. The tap button must be debounced here, once per
    // block, and its edges checked right after, or it never registers.
    hw.tap.Debounce();
    const uint32_t now = System::GetNow();

    // ---- knobs, lightly smoothed against ADC and CV noise (about 10 ms) ----
    for(int i = 0; i < 7; i++)
    {
        float k = hw.GetKnobValue(i);
        if(k < 0.f)
            k = 0.f;
        if(k > 1.f)
            k = 1.f;
        if(gFirst)
            gKnob[i] = k;
        else
            gKnob[i] += 0.1f * (k - gKnob[i]);
    }
    if(gFirst)
    {
        gFirst = false;
        int n  = (int)lroundf(gKnob[0] * 36.f);
        gRootN = n < 0 ? 0 : (n > 36 ? 36 : n);
        int sc = (int)(gKnob[1] * 8.f);
        gScaleI = sc < 0 ? 0 : (sc > 7 ? 7 : sc);
        int vc  = (int)(gKnob[2] * 6.f);
        gVoices = (vc < 0 ? 0 : (vc > 5 ? 5 : vc)) + 1;
    }
    gFade += (1.f - gFade) * 0.0005f; // about 2 seconds

    seraph::Params& P = gEng.P;

    // ---- switches ----
    const int envPos   = SwPos(hw.sw[DaisyVersio::SW_0]); // 0 soft, 1 even, 2 pluck
    const int spacePos = SwPos(hw.sw[DaisyVersio::SW_1]); // 0 room, 1 hall, 2 vast
    P.env   = envPos;
    P.space = kSpace[spacePos];
    P.decay = kDecay[spacePos];
    P.size  = kSize[spacePos];

    // ---- tap button: short = new seed, hold 0.8 s or more = Lock ----
    if(hw.tap.RisingEdge())
    {
        gTapDown  = true;
        gTapStart = now;
    }
    if(hw.tap.FallingEdge() && gTapDown)
    {
        gTapDown            = false;
        const uint32_t held = now - gTapStart;
        if(held >= 800)
        {
            gEng.SetFrozen(!gEng.Frozen());
        }
        else
        {
            gSeedCtr = gSeedCtr * 1664525u + 1013904223u + now;
            gEng.Reseed(gSeedCtr, false);
            gLedFlash = 1.f;
        }
    }

    // ---- gate: each rising edge starts the next voice's note ----
    const bool g = hw.Gate();
    if(g && !gGatePrev)
    {
        gEng.Advance();
        gLedGate = 1.f;
    }
    gGatePrev = g;

    // ---- knobs. Every knob is live, there is nothing to pick up. ----
    {
        // 1 Root: 37 semitones from A#0 (29 Hz) to A#3, with hysteresis so CV noise cannot flutter it
        const float nf = gKnob[0] * 36.f;
        if(fabsf(nf - (float)gRootN) > 0.65f)
        {
            int n  = (int)lroundf(nf);
            gRootN = n < 0 ? 0 : (n > 36 ? 36 : n);
        }
        P.root = 22.f + (float)gRootN;
        // 2 Scale: eight scales, dark on the left to bright on the right
        gScaleI = Step(gKnob[1], 8, gScaleI);
        P.scale = gScaleI;
        // 3 Voices: one to six
        gVoices = Step(gKnob[2], 6, gVoices - 1) + 1;
        P.dens  = gVoices;
        // 4 Rate: note length at the root, 0.12 s to 48 s (higher notes are longer)
        P.rate = 0.12f * powf(400.f, gKnob[3]);
        // 5 Glide: 0.01 s to 12 s
        P.glide = 0.01f * powf(1200.f, gKnob[4]);
        // 6 Spread: pitch range above the root, root only to three octaves
        P.spread = gKnob[5];
        // 7 Wave: sine to saw, and the filter opens with it
        P.timbre = 0.9f * gKnob[6];
        P.dark   = 0.35f + 0.5f * gKnob[6];
        {
            // level trim measured across the Wave knob, so sine, mixed and saw sound equally loud
            static const float kWaveTrim[9] = {0.45f, 0.50f, 0.59f, 0.76f, 1.06f, 1.42f, 1.16f, 0.79f, 0.57f};
            const float        f            = gKnob[6] * 8.f;
            int                i            = (int)f;
            if(i > 7)
                i = 7;
            if(i < 0)
                i = 0;
            const float fr = f - (float)i;
            P.gain         = kWaveTrim[i] + (kWaveTrim[i + 1] - kWaveTrim[i]) * fr;
        }
    }
    // fixed to the settings measured from the album
    P.clus   = 16.f;
    P.drift  = 0.5f;
    P.breath = 0.1f;
    P.body   = 0.5f;
    P.damp   = 0.4f;
    P.wow    = 0.4f;
    P.tune   = 0.f;
    P.noise  = 0.15f;
    P.width  = 1.f;
    P.level  = 0.7f;
    P.inLevel = 0.f;

    // ---- audio ----
    float inL[256], inR[256], oL[256], oR[256];
    size_t done = 0;
    float  peak = 0.f;
    while(done < size)
    {
        const size_t n = size - done > 256 ? 256 : size - done;
        for(size_t i = 0; i < n; i++)
        {
            inL[i] = in[0][done + i];
            inR[i] = in[1][done + i];
        }
        gEng.Render(inL, inR, oL, oR, (int)n);
        for(size_t i = 0; i < n; i++)
        {
            out[0][done + i] = oL[i] * kOutTrim * gFade;
            out[1][done + i] = oR[i] * kOutTrim * gFade;
            const float a = fabsf(oL[i]) > fabsf(oR[i]) ? fabsf(oL[i]) : fabsf(oR[i]);
            if(a > peak)
                peak = a;
        }
        done += n;
    }
    gLedPeak   = gLedPeak * 0.92f > peak ? gLedPeak * 0.92f : peak;
    gLedFrozen = gEng.Frozen();
    gLedScale  = gScaleI;
    gLedAct    = gEng.Activity();
#if SERAPH_CPU_METER
    gMeter.OnBlockEnd();
    gLedCpu = gMeter.GetAvgCpuLoad();
#endif
}

int main(void)
{
    hw.Init(true); // run at full clock speed
    const float sr = hw.seed.AudioSampleRate();
    gEng.Init(sr, gBuf, 1u);
    for(int i = 0; i < 7; i++)
        gKnob[i] = 0.f;
#if SERAPH_CPU_METER
    gMeter.Init(sr, hw.AudioBlockSize());
#endif
    hw.StartAdc();
    hw.StartAudio(Audio);

    // LED 3 shows the scale, one colour per scale from dark (left) to bright (right)
    static const float kScaleCol[8][3] = {{0.30f, 0.00f, 0.10f}, {0.40f, 0.00f, 0.30f}, {0.20f, 0.04f, 0.45f},
                                          {0.04f, 0.10f, 0.45f}, {0.00f, 0.30f, 0.40f}, {0.05f, 0.40f, 0.12f},
                                          {0.40f, 0.35f, 0.04f}, {0.45f, 0.38f, 0.30f}};

    uint32_t t = 0;
    while(1)
    {
        hw.DelayMs(6);
        t += 6;
        const float phase = 0.5f + 0.5f * sinf((float)t * 0.0031f);
        gLedFlash *= 0.93f;
        gLedGate *= 0.90f;
        // LED 1 output level, LED 2 note activity, LED 3 scale colour, LED 4 events
        const float p = gLedPeak;
        hw.SetLed(DaisyVersio::LED_0, p * 0.6f, p * 0.7f, 0.15f + 0.85f * p);
        const float a = gLedAct;
        hw.SetLed(DaisyVersio::LED_1, 0.f, 0.04f + 0.55f * a, 0.04f + 0.50f * a);
        const int sc = gLedScale < 0 ? 0 : (gLedScale > 7 ? 7 : gLedScale);
        hw.SetLed(DaisyVersio::LED_2, kScaleCol[sc][0], kScaleCol[sc][1], kScaleCol[sc][2]);
        float r = gLedFlash, gr = gLedFlash, b = gLedFlash; // white flash on a new seed
        gr += gLedGate * 0.8f;                              // green flash on a gate
        if(gLedFrozen)                                      // slow amber pulse while Locked
        {
            r += 0.5f * phase;
            gr += 0.25f * phase;
        }
#if SERAPH_CPU_METER
        r  = gLedCpu;
        gr = 1.f - gLedCpu;
        b  = 0.f;
#endif
        hw.SetLed(DaisyVersio::LED_3, r > 1.f ? 1.f : r, gr > 1.f ? 1.f : gr, b > 1.f ? 1.f : b);
        hw.UpdateLeds();
    }
}
