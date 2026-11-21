// Seraph for Noise Engineering Versio.
//
// A slow, dark, wide drone and pad engine. See README.md for the control map.
//
//   Knobs (7, all take CV):  root note | change | cluster | darkness | breath | space | decay
//   Switch 1:                voicing   (up choir, centre drone, down mass)
//   Switch 2:                mode      (centre Play, up Setup page A, down Setup page B). Setup knobs use pick-up.
//   Tap, short:              new seed  (voices glide to a new chord)
//   Tap, hold 0.8 s or more: freeze or release the harmony
//   Gate in:                 each rising edge moves one voice to a new note
//   Audio in:                mixed into the filter and reverb (level set in Setup page B)
#include "daisy_versio.h"
#include "seraph.h"
#include <cmath>

using namespace daisy;

// ---- set to 1 to show CPU load on LED 3 while you test (needs libDaisy CpuLoadMeter) ----
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
//      differently, this is the only function to change. Returns 0 up, 1 centre, 2 down. ----
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
enum Mode { PLAY = 0, SETUP_A = 1, SETUP_B = 2 };
static volatile int   gMode = PLAY;
static float          gKnob[7]; // smoothed 0..1
static int            gRootN = 12;
// Play values. When you come back from Setup the knobs are somewhere else, so the Play settings hold still
// until each knob is moved past its held value (pick-up), the same way Setup works.
static float          gPlayVal[7];
static float          gPlayPrev[7];
static bool           gPlayCaught[7];
static volatile bool  gPlayWaiting = false; // true while any Play knob has not picked up yet
static bool           gFirst = true;  // first block: take knob positions as they are
static float          gFade  = 0.f;   // slow fade-in after power-up

// Setup values, normalised 0..1. Page A then page B, seven each. Page B uses only five.
static float gSetup[2][7] = {
    {0.50f, 0.40f, 0.50f, 0.50f, 0.40f, 1.00f, 0.70f}, // drift wow tune size damp width level
    {0.15f, 0.00f, 0.463f, 0.50f, 0.50f, 0.f, 0.f}};  // air input glide body scale
static bool  gCaught[7];
static float gPrev[7];

// Tap button
static bool     gTapDown   = false;
static uint32_t gTapStart  = 0;
static uint32_t gSeedCtr   = 1;

// Gate
static bool gGatePrev = false;

// Values the main loop reads for the LEDs
static volatile float gLedPeak   = 0.f;
static volatile float gLedFlash  = 0.f; // seed flash, decays
static volatile float gLedGate   = 0.f;
static volatile bool  gLedFrozen = false;
static volatile int   gVoicing   = 1;
static volatile float gLedSwell  = 0.f;
#if SERAPH_CPU_METER
static volatile float gLedCpu = 0.f;
#endif

static void ApplySetup()
{
    seraph::Params& P = gEng.P;
    P.drift   = gSetup[0][0];
    P.wow     = gSetup[0][1];
    P.tune    = (gSetup[0][2] - 0.5f) * 100.f;
    P.size    = gSetup[0][3];
    P.damp    = gSetup[0][4];
    P.width   = gSetup[0][5];
    P.level   = gSetup[0][6];
    P.noise   = gSetup[1][0];
    P.inLevel = gSetup[1][1] * 1.2f;
    P.glide   = powf(20.f, gSetup[1][2]); // 1 to 20 seconds
    P.body    = gSetup[1][3];
    // scale: left third minor pentatonic, middle third dorian, right third lydian
    {
        const int sc = (int)(gSetup[1][4] * 3.f);
        P.scale      = sc < 0 ? 0 : (sc > 2 ? 2 : sc);
    }
}

static void EnterPage(int mode)
{
    gMode = mode;
    for(int i = 0; i < 7; i++)
    {
        gCaught[i] = false;
        gPrev[i]   = gKnob[i];
    }
}

static void Audio(AudioHandle::InputBuffer  in,
                  AudioHandle::OutputBuffer out,
                  size_t                    size)
{
#if SERAPH_CPU_METER
    gMeter.OnBlockStart();
#endif
    hw.ProcessAllControls();
    const uint32_t now = System::GetNow();

    // ---- knobs, lightly smoothed against ADC and CV noise ----
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
        int n = (int)lroundf(gKnob[0] * 36.f);
        gRootN = n < 0 ? 0 : (n > 36 ? 36 : n);
        for(int i = 0; i < 7; i++)
        {
            gPlayVal[i]    = gKnob[i];
            gPlayPrev[i]   = gKnob[i];
            gPlayCaught[i] = true;
        }
    }
    gFade += (1.f - gFade) * 0.0005f; // about 2 seconds

    seraph::Params& P = gEng.P;

    // ---- switches ----
    const int voicing = SwPos(hw.sw[DaisyVersio::SW_0]);
    gVoicing = voicing;
    static const float kTimbre[3] = {0.25f, 0.60f, 0.85f}; // choir, drone, mass
    static const int   kDens[3]   = {4, 5, 6};
    P.timbre = kTimbre[voicing];
    P.dens   = kDens[voicing];

    // ---- switch 2 picks the mode: centre Play, up Setup A, down Setup B ----
    {
        const int sp     = SwPos(hw.sw[DaisyVersio::SW_1]);
        const int target = sp == 1 ? PLAY : (sp == 0 ? SETUP_A : SETUP_B);
        if(target != gMode)
        {
            if(target == PLAY)
            {
                gMode = PLAY;
                for(int i = 0; i < 7; i++) // hold the Play settings until each knob picks them up again
                {
                    gPlayCaught[i] = false;
                    gPlayPrev[i]   = gKnob[i];
                }
            }
            else
                EnterPage(target); // resets pick-up, so nothing jumps
            gLedFlash = 1.f;
        }
    }

    // ---- tap button, same in every mode: short = new seed, hold 0.8 s or more = freeze ----
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

    // ---- gate: each rising edge moves one voice ----
    const bool g = hw.Gate();
    if(g && !gGatePrev)
    {
        gEng.Advance();
        gLedGate = 1.f;
    }
    gGatePrev = g;

    // ---- knob mapping ----
    if(gMode == PLAY)
    {
        // root note: 37 semitones from A#0, with hysteresis so CV noise cannot flutter it
        bool waiting = false;
        for(int i = 0; i < 7; i++)
        {
            const float k = gKnob[i], v = gPlayVal[i];
            if(!gPlayCaught[i] && (fabsf(k - v) < 0.03f || (gPlayPrev[i] - v) * (k - v) <= 0.f))
                gPlayCaught[i] = true;
            gPlayPrev[i] = k;
            if(gPlayCaught[i])
                gPlayVal[i] = k;
            else
                waiting = true;
        }
        gPlayWaiting = waiting;
        const float nf = gPlayVal[0] * 36.f;
        if(fabsf(nf - (float)gRootN) > 0.65f)
        {
            int n = (int)lroundf(nf);
            gRootN = n < 0 ? 0 : (n > 36 ? 36 : n);
        }
        P.root   = 22.f + (float)gRootN;
        P.harm   = gPlayVal[1];
        P.clus   = gPlayVal[2] * 40.f;
        P.dark   = gPlayVal[3];
        P.breath = gPlayVal[4];
        P.space  = gPlayVal[5];
        P.decay  = gPlayVal[6];
    }
    else
    {
        // Setup: a knob only takes over once it passes the stored value (pick-up)
        const int page = gMode == SETUP_A ? 0 : 1;
        const int used = page == 0 ? 7 : 5;
        for(int i = 0; i < used; i++)
        {
            const float k = gKnob[i], s = gSetup[page][i];
            if(!gCaught[i] && (fabsf(k - s) < 0.03f || (gPrev[i] - s) * (k - s) <= 0.f))
                gCaught[i] = true;
            gPrev[i] = k;
            if(gCaught[i])
                gSetup[page][i] = k;
        }
        ApplySetup();
    }

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
    float sw   = (gEng.MasterDb() + 22.f) / 22.f;
    gLedSwell  = sw < 0.f ? 0.f : (sw > 1.f ? 1.f : sw);
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
    ApplySetup();
#if SERAPH_CPU_METER
    gMeter.Init(sr, hw.AudioBlockSize());
#endif
    hw.StartAdc();
    hw.StartAudio(Audio);

    uint32_t t = 0;
    while(1)
    {
        hw.DelayMs(6);
        t += 6;
        const float phase  = 0.5f + 0.5f * sinf((float)t * 0.0031f);
        const float phaseF = 0.5f + 0.5f * sinf((float)t * 0.0090f);
        gLedFlash *= 0.93f;
        gLedGate *= 0.90f;
        if(gMode == PLAY)
        {
            // LED 0 output level, LED 1 swell, LED 2 voicing colour, LED 3 events
            const float p = gLedPeak;
            hw.SetLed(DaisyVersio::LED_0, p * 0.6f, p * 0.7f, 0.15f + 0.85f * p);
            const float s = gLedSwell;
            hw.SetLed(DaisyVersio::LED_1, 0.f, 0.05f + 0.5f * s, 0.05f + 0.45f * s);
            switch(gVoicing)
            {
                case 0: hw.SetLed(DaisyVersio::LED_2, 0.15f, 0.30f, 0.35f); break; // choir
                case 1: hw.SetLed(DaisyVersio::LED_2, 0.25f, 0.10f, 0.40f); break; // drone
                default: hw.SetLed(DaisyVersio::LED_2, 0.45f, 0.05f, 0.25f); break; // mass
            }
            float r = gLedFlash, gr = gLedFlash, b = gLedFlash;
            gr += gLedGate * 0.8f;
            if(gLedFrozen)
            {
                r += 0.5f * phase;
                gr += 0.25f * phase;
            }
            if(gPlayWaiting) // a dim white glow while a knob is waiting to pick up its held value
            {
                r += 0.12f;
                gr += 0.12f;
                b += 0.12f;
            }
#if SERAPH_CPU_METER
            r = gLedCpu;
            gr = 1.f - gLedCpu;
            b  = 0.f;
#endif
            hw.SetLed(DaisyVersio::LED_3, r > 1.f ? 1.f : r, gr > 1.f ? 1.f : gr, b > 1.f ? 1.f : b);
        }
        else
        {
            // Setup: orange pulse on LED 0, page colour on LED 1, flash on LED 3 when leaving/entering
            hw.SetLed(DaisyVersio::LED_0, 0.8f * phaseF, 0.3f * phaseF, 0.f);
            if(gMode == SETUP_A)
                hw.SetLed(DaisyVersio::LED_1, 0.7f, 0.25f, 0.f);
            else
                hw.SetLed(DaisyVersio::LED_1, 0.6f, 0.f, 0.5f);
            hw.SetLed(DaisyVersio::LED_2, 0.f, 0.f, 0.f);
            hw.SetLed(DaisyVersio::LED_3, gLedFlash, gLedFlash, gLedFlash);
        }
        hw.UpdateLeds();
    }
}
