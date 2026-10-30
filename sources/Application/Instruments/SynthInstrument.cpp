#include "SynthInstrument.h"
#include "Application/Player/SyncMaster.h"
#include "CommandList.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char *synthEngineModes[SYM_LAST] = {(char *)"synth", (char *)"shapes"};

static char *synthWaveforms[SYW_LAST] = {
    (char *)"pulse12",  (char *)"pulse25",  (char *)"square",
    (char *)"pulse75",  (char *)"saw",      (char *)"triangle",
    (char *)"sine",     (char *)"noise",    (char *)"supersaw",
    (char *)"overflow"};

static const char *synthShapeLabels[SYW_LAST] = {
    "PULSE 12%", "PULSE 25%", "PULSE 50%", "PULSE 75%", "SAW",
    "TRIANGLE",  "SINE",      "NOISE",     "SUPERSAW",  "OVERFLOW"};

static char *macroShapeNames[SMS_LAST] = {
    (char *)"csaw",     (char *)"morph",    (char *)"saw_sq",
    (char *)"sine_tri", (char *)"buzz",     (char *)"sub_saw",
    (char *)"sub_sq",   (char *)"swarm",    (char *)"fm",
    (char *)"fb_fm",    (char *)"vowel",    (char *)"bell",
    (char *)"pluck",    (char *)"kick",     (char *)"snare",
    (char *)"cymbal",   (char *)"chord",    (char *)"shimmer",
    (char *)"ensemble", (char *)"grainpad", (char *)"orbix"};

static const char *macroShapeLabels[SMS_LAST] = {
    "CSAW",     "MORPH",    "SAW SQ",  "SINE TRI", "BUZZ",     "SUB SAW",
    "SUB SQ",   "SWARM",    "FM",      "FB FM",    "VOWEL",    "BELL",
    "PLUCK",    "KICK",     "SNARE",   "CYMBAL",   "CHORD",    "SHIMMER",
    "ENSEMBLE", "GRAINPAD", "ORBIX"};

static char *synthFilterModes[SYF_LAST] = {(char *)"OFF", (char *)"LOWPASS",
                                           (char *)"HIGHPASS",
                                           (char *)"BANDPASS", (char *)"LP+HP"};

static const char *synthFilterLabels[SYF_LAST] = {"OFF", "LOWPASS", "HIGHPASS",
                                                  "BANDPASS", "LP+HP"};

static char *synthLimiterModes[SYL_LAST] = {(char *)"CLIP", (char *)"SIN",
                                            (char *)"FOLD", (char *)"WRAP",
                                            (char *)"POST"};

static const char *synthLimiterLabels[SYL_LAST] = {"CLIP", "SIN", "FOLD",
                                                   "WRAP", "POST"};

static char *synthOnOff[2] = {(char *)"OFF", (char *)"ON"};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYNTH_KRATE_SAMPLE_COUNT 64

const char *SynthInstrument::GetShapeLabel(int shape) {
    if (shape < 0 || shape >= SYW_LAST) {
        shape = 0;
    }
    return synthShapeLabels[shape];
}

const char *SynthInstrument::GetMacroShapeLabel(int shape) {
    if (shape < 0 || shape >= SMS_LAST) {
        shape = 0;
    }
    return macroShapeLabels[shape];
}

const char *SynthInstrument::GetFilterLabel(int mode) {
    if (mode < 0 || mode >= SYF_LAST) {
        mode = 0;
    }
    return synthFilterLabels[mode];
}

const char *SynthInstrument::GetLimiterLabel(int mode) {
    if (mode < 0 || mode >= SYL_LAST) {
        mode = 0;
    }
    return synthLimiterLabels[mode];
}

SynthInstrument::SynthInstrument() {
    strcpy(name_, "SYNTH");

    mode_ = new Variable("mode", SYP_MODE, synthEngineModes, SYM_LAST, SYM_SYNTH);
    Insert(mode_);

    waveform_ = new Variable("waveform", SYP_WAVEFORM, synthWaveforms, SYW_LAST,
                             SYW_PULSE25);
    Insert(waveform_);

    macroShape_ = new Variable("macro shape", SYP_MACROSHAPE, macroShapeNames,
                               SMS_LAST, SMS_CSAW);
    Insert(macroShape_);

    transpose_ = new Variable("transpose", SYP_TRANSPOSE, synthOnOff, 2, 1);
    Insert(transpose_);

    tblTic_ = new Variable("tbl tic", SYP_TBLTIC, 1);
    Insert(tblTic_);

    size_ = new Variable("size", SYP_SIZE, 0x20);
    Insert(size_);

    mult_ = new Variable("mult", SYP_MULT, 0x00);
    Insert(mult_);

    warp_ = new Variable("warp", SYP_WARP, 0x00);
    Insert(warp_);

    scan_ = new Variable("scan", SYP_SCAN, 0x00);
    Insert(scan_);

    timbre_ = new Variable("timbre", SYP_TIMBRE, 0x80);
    Insert(timbre_);

    color_ = new Variable("color", SYP_COLOR, 0x80);
    Insert(color_);

    degrade_ = new Variable("degrade", SYP_DEGRADE, 0x00);
    Insert(degrade_);

    redux_ = new Variable("redux", SYP_REDUX, 0x00);
    Insert(redux_);

    filterMode_ = new Variable("filter mode", SYP_FILTMODE, synthFilterModes,
                               SYF_LAST, SYF_OFF);
    Insert(filterMode_);

    filterCut_ = new Variable("filter cut", SYP_FILTCUT, 0xFF);
    Insert(filterCut_);

    filterRes_ = new Variable("filter res", SYP_FILTRES, 0x00);
    Insert(filterRes_);

    amp_ = new Variable("amp", SYP_AMP, 0x00);
    Insert(amp_);

    lim_ = new Variable("lim", SYP_LIM, synthLimiterModes, SYL_LAST, SYL_CLIP);
    Insert(lim_);

    volume_ = new Variable("volume", SYP_VOLUME, 0xC0);
    Insert(volume_);

    pan_ = new Variable("pan", SYP_PAN, 0x80);
    Insert(pan_);

    chorus_ = new Variable("cho", SYP_CHORUS, 0x00);
    Insert(chorus_);

    delSend_ = new Variable("del", SYP_DELSEND, 0x00);
    Insert(delSend_);

    revSend_ = new Variable("rev", SYP_REVSEND, 0x00);
    Insert(revSend_);

    tapeSend_ = new Variable("tape", SYP_TAPESEND, 0x00);
    Insert(tapeSend_);

    rootNote_ = new Variable("root note", SYP_ROOTNOTE, 60);
    Insert(rootNote_);

    fineTune_ = new Variable("fine tune", SYP_FINETUNE, 0x7F);
    Insert(fineTune_);

    attack_ = new Variable("attack", SYP_ATTACK, 0x04);
    Insert(attack_);

    decay_ = new Variable("decay", SYP_DECAY, 0x30);
    Insert(decay_);

    sustain_ = new Variable("sustain", SYP_SUSTAIN, 0xC0);
    Insert(sustain_);

    release_ = new Variable("release", SYP_RELEASE, 0x28);
    Insert(release_);

    lfoRate_ = new Variable("lfo rate", SYP_LFORATE, 0x40);
    Insert(lfoRate_);

    lfoDepth_ = new Variable("lfo depth", SYP_LFODEPTH, 0x00);
    Insert(lfoDepth_);

    table_ = new Variable("table", SYP_TABLE, -1);
    Insert(table_);

    tableAuto_ = new Variable("table automation", SYP_TABLEAUTO, false);
    Insert(tableAuto_);

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        Voice &v = voice_[i];
        v.active_ = false;
        v.stage_ = ENV_OFF;
        v.env_ = 0.0f;
        v.noteAge_ = 0.0f;
        v.phase_ = 0.0f;
        v.phase2_ = 0.0f;
        v.multPhase_ = 0.0f;
        v.baseInc_ = 0.0f;
        v.curInc_ = 0.0f;
        v.lfoPhase_ = 0.0f;
        v.pitchMod_ = 1.0f;
        v.note_ = 0;
        v.lastNote_ = -1;
        v.noise_ = 0xACE1u + (unsigned)i * 97u;
        v.holdSampleL_ = 0.0f;
        v.holdSampleR_ = 0.0f;
        v.degradeCounter_ = 0;
        v.fmPrev_ = 0.0f;
        v.baseVol_ = 0xC0;
        v.vol_ = 0xC0;
        v.basePan_ = 0x80;
        v.pan_ = 0x80;
        v.baseCut_ = 1.0f;
        v.cut_ = 1.0f;
        v.baseRes_ = 0.0f;
        v.res_ = 0.0f;
        v.svfLpL_ = v.svfBpL_ = v.svfLpR_ = v.svfBpR_ = 0.0f;
        v.krateCount_ = 0;
        v.retrig_ = false;
        v.retrigLoop_ = 0;
        v.retrigCount_ = 0;
        v.updaters_.clear();
        v.updaters_.push_back(&v.volumeRamp_);
        v.updaters_.push_back(&v.panner_);
        v.updaters_.push_back(&v.speedRamp_);
        v.updaters_.push_back(&v.legato_);
        v.updaters_.push_back(&v.pfin_);
        v.updaters_.push_back(&v.cutRamp_);
        v.updaters_.push_back(&v.resRamp_);
        v.updaters_.push_back(&v.arp_);
        v.activeUpdaters_.clear();
    }
}

SynthInstrument::~SynthInstrument() {}

bool SynthInstrument::Init() {
    tableState_.Reset();
    return true;
}

void SynthInstrument::OnStart() { tableState_.Reset(); }

int SynthInstrument::GetEngineMode() const {
    return mode_ ? mode_->GetInt() : SYM_SYNTH;
}

void SynthInstrument::SetEngineMode(int mode) {
    if (mode_) {
        mode_->SetInt((mode == SYM_SHAPES) ? SYM_SHAPES : SYM_SYNTH, false);
    }
}

unsigned char SynthInstrument::GetDelaySend() {
    return delSend_ ? (unsigned char)delSend_->GetInt() : 0;
}

unsigned char SynthInstrument::GetReverbSend() {
    return revSend_ ? (unsigned char)revSend_->GetInt() : 0;
}

unsigned char SynthInstrument::GetTapeSend() {
    return tapeSend_ ? (unsigned char)tapeSend_->GetInt() : 0;
}

float SynthInstrument::envTimeToInc(int param, float sampleRate) const {
    float t = (float)param / 255.0f;
    float seconds = t * t * 2.0f;
    if (seconds < 1.0f / sampleRate) {
        seconds = 1.0f / sampleRate;
    }
    return 1.0f / (seconds * sampleRate);
}

float SynthInstrument::shapePhase(float phase, int sizeParam, int warpParam,
                                  int scanParam) const {
    float p = phase - (float)floor(phase);

    if (scanParam > 0) {
        float sc = 1.0f + (scanParam / 255.0f) * 3.0f;
        p = p * sc;
        int cyc = (int)floor(p);
        float frac = p - (float)cyc;
        p = (cyc & 1) ? (1.0f - frac) : frac;
    }

    if (warpParam > 0) {
        float w = (warpParam / 255.0f);
        float mid = 0.5f - w * 0.42f;
        if (mid < 0.05f) {
            mid = 0.05f;
        }
        if (p < mid) {
            p = 0.5f * (p / mid);
        } else {
            p = 0.5f + 0.5f * ((p - mid) / (1.0f - mid));
        }
    }

    if (sizeParam > 0x20) {
        int steps = 64 - ((sizeParam - 0x20) * 60) / (255 - 0x20);
        if (steps < 3) {
            steps = 3;
        }
        p = (float)floor(p * (float)steps) / (float)steps;
    }

    return p;
}

float SynthInstrument::applyLimiter(float s, int ampParam, int limMode) const {
    if (ampParam > 0) {
        float gain = 1.0f + (ampParam / 255.0f) * 7.0f;
        s *= gain;
    }

    switch (limMode) {
    case SYL_SIN:
        if (s > 1.0f || s < -1.0f || ampParam > 0) {
            s = (float)sin(s * (M_PI * 0.5));
        }
        break;
    case SYL_FOLD:
        while (s > 1.0f || s < -1.0f) {
            if (s > 1.0f) {
                s = 2.0f - s;
            }
            if (s < -1.0f) {
                s = -2.0f - s;
            }
        }
        break;
    case SYL_WRAP:
        if (s > 1.0f || s < -1.0f) {
            s = s + 1.0f;
            s = s - 2.0f * (float)floor(s * 0.5f) - 1.0f;
        }
        break;
    case SYL_POST: {
        float x = s;
        s = x / (1.0f + 0.6f * (float)fabs(x));
        if (s > 1.0f)
            s = 1.0f;
        if (s < -1.0f)
            s = -1.0f;
        break;
    }
    case SYL_CLIP:
    default:
        if (s > 1.0f)
            s = 1.0f;
        if (s < -1.0f)
            s = -1.0f;
        break;
    }
    return s;
}

float SynthInstrument::sampleWave(int wave, float phase,
                                  unsigned int &noise) const {
    switch (wave) {
    case SYW_PULSE12:
        return (phase < 0.125f) ? 1.0f : -1.0f;
    case SYW_PULSE25:
        return (phase < 0.25f) ? 1.0f : -1.0f;
    case SYW_SQUARE:
        return (phase < 0.5f) ? 1.0f : -1.0f;
    case SYW_PULSE75:
        return (phase < 0.75f) ? 1.0f : -1.0f;
    case SYW_SAW:
        return phase * 2.0f - 1.0f;
    case SYW_TRIANGLE:
        return (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
    case SYW_SINE:
        return (float)sin(phase * 2.0 * M_PI);
    case SYW_SUPERSAW: {
        float p1 = phase;
        float p2 = phase * 1.015f + 0.23f;
        p2 -= (float)floor(p2);
        float p3 = phase * 0.987f + 0.61f;
        p3 -= (float)floor(p3);
        return ((p1 * 2.0f - 1.0f) + (p2 * 2.0f - 1.0f) * 0.75f +
                (p3 * 2.0f - 1.0f) * 0.75f) *
               0.42f;
    }
    case SYW_OVERFLOW: {
        float v = (phase * 4.0f) - 2.0f;
        if (v > 1.0f)
            v -= 2.0f;
        if (v < -1.0f)
            v += 2.0f;
        return v;
    }
    case SYW_NOISE:
    default:
        noise = noise * 1103515245u + 12345u;
        return ((noise >> 16) & 0xFFFF) / 32768.0f - 1.0f;
    }
}

float SynthInstrument::sampleMacroShape(int shape, float phase, float subPhase,
                                        float noteAge, int timbreParam,
                                        int colorParam, unsigned int &noise,
                                        float &fmPrev,
                                        float &pitchMultiplierOut) const {
    pitchMultiplierOut = 1.0f;
    float p = phase - (float)floor(phase);
    float sp = subPhase - (float)floor(subPhase);
    float t = timbreParam / 255.0f;
    float c = colorParam / 255.0f;

    switch (shape) {
    case SMS_CSAW: {
        // Phase-notched sawtooth with sync edge
        float notchW = 0.05f + t * 0.45f;
        float saw = p * 2.0f - 1.0f;
        float notch = (p < notchW) ? (-1.0f + (c * 2.0f - 1.0f)) : saw;
        return notch;
    }
    case SMS_MORPH: {
        // Continuous morph: Triangle (0..0.33) -> Saw (0.33..0.66) -> Pulse (0.66..1.0)
        float tri = (p < 0.5f) ? (p * 4.0f - 1.0f) : (3.0f - p * 4.0f);
        float saw = p * 2.0f - 1.0f;
        float pw = 0.5f - c * 0.42f;
        float pls = (p < pw) ? 1.0f : -1.0f;
        float out;
        if (t < 0.5f) {
            float u = t * 2.0f;
            out = tri * (1.0f - u) + saw * u;
        } else {
            float u = (t - 0.5f) * 2.0f;
            out = saw * (1.0f - u) + pls * u;
        }
        if (c > 0.5f) {
            out = (float)sin(out * (1.0f + (c - 0.5f) * 4.0f));
        }
        return out;
    }
    case SMS_SAW_SQ: {
        float pw = 0.1f + t * 0.8f;
        float saw = p * 2.0f - 1.0f;
        float sq = (p < pw) ? 1.0f : -1.0f;
        return saw * (1.0f - c) + sq * c;
    }
    case SMS_SINE_TRI: {
        float skewed = (c > 0.01f) ? (float)pow(p, 0.3f + c * 1.4f) : p;
        float base = (float)sin(skewed * 2.0 * M_PI);
        float folded = base * (1.0f + t * 5.0f);
        while (folded > 1.0f || folded < -1.0f) {
            if (folded > 1.0f)
                folded = 2.0f - folded;
            if (folded < -1.0f)
                folded = -2.0f - folded;
        }
        return folded;
    }
    case SMS_BUZZ: {
        // Harmonic formant / impulse train
        float harm = 2.0f + t * 14.0f;
        float envWin = 1.0f - p;
        float carrier = (float)sin(p * 2.0 * M_PI) +
                        (float)sin(p * harm * 2.0 * M_PI) * (0.4f + c * 0.6f);
        return carrier * envWin * 0.7f;
    }
    case SMS_SUB_SAW: {
        float mainSaw = p * 2.0f - 1.0f;
        float sub1 = sp * 2.0f - 1.0f;
        float subSq = (sp < 0.5f) ? 1.0f : -1.0f;
        float subSig = sub1 * (1.0f - c) + subSq * c;
        return (mainSaw * (1.0f - t * 0.6f) + subSig * t) * 0.8f;
    }
    case SMS_SUB_SQ: {
        float pw = 0.12f + t * 0.38f;
        float mainSq = (p < pw) ? 1.0f : -1.0f;
        float subSq = (sp < 0.5f) ? 1.0f : -1.0f;
        return (mainSq * (1.0f - c * 0.6f) + subSq * c) * 0.8f;
    }
    case SMS_SWARM: {
        // 5-voice detuned saw swarm
        float spread = 0.002f + t * 0.045f;
        float acc = (p * 2.0f - 1.0f);
        for (int k = 1; k <= 2; k++) {
            float pk1 = p * (1.0f + spread * k) + 0.17f * k + noteAge * spread * k * 8.0f;
            pk1 -= (float)floor(pk1);
            float pk2 = p * (1.0f - spread * k) + 0.53f * k - noteAge * spread * k * 8.0f;
            pk2 -= (float)floor(pk2);
            acc += ((pk1 * 2.0f - 1.0f) + (pk2 * 2.0f - 1.0f)) * (0.4f + c * 0.4f);
        }
        return acc * 0.28f;
    }
    case SMS_FM: {
        static const float kRatios[8] = {0.5f, 1.0f, 1.5f, 2.0f,
                                         3.0f, 4.0f, 5.0f, 7.0f};
        int rIdx = (int)(c * 7.99f);
        if (rIdx < 0)
            rIdx = 0;
        if (rIdx > 7)
            rIdx = 7;
        float ratio = kRatios[rIdx];
        float index = t * 8.0f;
        float mod = (float)sin(p * ratio * 2.0 * M_PI);
        return (float)sin(p * 2.0 * M_PI + index * mod);
    }
    case SMS_FB_FM: {
        float index = t * 6.0f;
        float fb = c * 2.2f;
        float mod = (float)sin(p * 2.0 * M_PI + fb * fmPrev);
        fmPrev = mod;
        return (float)sin(p * 2.0 * M_PI + index * mod);
    }
    case SMS_VOWEL: {
        // 2-formant vowel synthesizer (A -> E -> I -> O -> U)
        static const float f1Ratio[5] = {3.0f, 2.0f, 1.5f, 2.2f, 1.4f};
        static const float f2Ratio[5] = {6.0f, 9.0f, 11.0f, 4.5f, 3.8f};
        float vPos = t * 4.0f;
        int vi = (int)floor(vPos);
        if (vi > 3)
            vi = 3;
        float vf = vPos - (float)vi;
        float r1 = (f1Ratio[vi] * (1.0f - vf) + f1Ratio[vi + 1] * vf) *
                   (0.7f + c * 0.8f);
        float r2 = (f2Ratio[vi] * (1.0f - vf) + f2Ratio[vi + 1] * vf) *
                   (0.7f + c * 0.8f);
        float win = 0.5f * (1.0f - (float)cos(p * 2.0 * M_PI));
        float form1 = (float)sin(p * r1 * 2.0 * M_PI);
        float form2 = (float)sin(p * r2 * 2.0 * M_PI) * 0.55f;
        return (form1 + form2) * win;
    }
    case SMS_BELL: {
        // Inharmonic metallic FM bell with natural partial decay
        float ratio = 1.414f + c * 2.05f;
        float decay = (float)exp(-noteAge * (3.0f + (1.0f - c) * 8.0f));
        float idx = (0.5f + t * 6.0f) * decay;
        float mod = (float)sin(p * ratio * 2.0 * M_PI);
        return (float)sin(p * 2.0 * M_PI + idx * mod) * (0.3f + 0.7f * decay);
    }
    case SMS_PLUCK: {
        // Plucked string with fast harmonic damping
        float dampRate = 3.0f + (1.0f - c) * 18.0f;
        float hDecay = (float)exp(-noteAge * dampRate);
        float bright = (0.2f + t * 0.8f) * hDecay;
        float f1 = (float)sin(p * 2.0 * M_PI);
        float f2 = (float)sin(p * 4.0 * M_PI) * bright;
        float f3 = (float)sin(p * 6.0 * M_PI) * bright * bright;
        float f4 = (p * 2.0f - 1.0f) * bright * 0.6f;
        return (f1 + f2 + f3 + f4) * 0.55f;
    }
    case SMS_KICK: {
        // Punchy drum kick: exponential pitch drop + transient click + body drive
        float sweepRate = 18.0f + (1.0f - c) * 45.0f;
        float sweepEnv = (float)exp(-noteAge * sweepRate);
        pitchMultiplierOut = 1.0f + (1.5f + t * 6.5f) * sweepEnv;
        float bodyDecay = (float)exp(-noteAge * (2.5f + (1.0f - c) * 8.0f));
        float body = (float)sin(p * 2.0 * M_PI);
        if (t > 0.25f) {
            body = (float)sin(body * (1.0f + (t - 0.25f) * 2.5f));
        }
        float clickEnv = (noteAge < 0.008f) ? (1.0f - noteAge / 0.008f) : 0.0f;
        noise = noise * 1103515245u + 12345u;
        float click = (((noise >> 16) & 0xFFFF) / 32768.0f - 1.0f) * clickEnv * t;
        return (body * bodyDecay + click * 0.6f);
    }
    case SMS_SNARE: {
        // Drum snare: pitched body sweep + snappy noise wires
        float sweepEnv = (float)exp(-noteAge * 38.0f);
        pitchMultiplierOut = 1.0f + (0.8f + t * 2.5f) * sweepEnv;
        float bodyDecay = (float)exp(-noteAge * 14.0f);
        float noiseDecay = (float)exp(-noteAge * (6.0f + (1.0f - c) * 16.0f));
        float body = (float)sin(p * 2.0 * M_PI) * bodyDecay;
        noise = noise * 1103515245u + 12345u;
        float n = (((noise >> 16) & 0xFFFF) / 32768.0f - 1.0f) * noiseDecay;
        return body * (1.0f - c * 0.6f) + n * (0.35f + c * 0.65f);
    }
    case SMS_CYMBAL: {
        // 6-oscillator inharmonic metallic matrix + noise sizzle
        static const float kCymRatios[6] = {1.0f,   1.342f, 1.665f,
                                            1.923f, 2.514f, 3.179f};
        float det = 0.8f + t * 0.6f;
        float sqSum = 0.0f;
        for (int k = 0; k < 6; k++) {
            float pk = p * kCymRatios[k] * det;
            pk -= (float)floor(pk);
            sqSum += (pk < 0.5f) ? 1.0f : -1.0f;
        }
        noise = noise * 1103515245u + 12345u;
        float n = ((noise >> 16) & 0xFFFF) / 32768.0f - 1.0f;
        float cymDecay = (float)exp(-noteAge * (3.0f + (1.0f - c) * 18.0f));
        return ((sqSum * (1.0f / 6.0f)) * (1.0f - c * 0.5f) + n * (c * 0.6f)) *
               cymDecay;
    }
    case SMS_CHORD: {
        // 4-voice ambient chord pad with slow organic drift
        static const float kChords[6][4] = {
            {1.0f, 1.189207f, 1.498307f, 1.781797f}, // Min7
            {1.0f, 1.259921f, 1.498307f, 1.887749f}, // Maj7
            {1.0f, 1.334840f, 1.498307f, 2.244924f}, // Sus4/9
            {1.0f, 1.189207f, 1.498307f, 2.244924f}, // Min9
            {1.0f, 1.498307f, 2.000000f, 2.996614f}, // Fifths+Oct
            {1.0f, 1.259921f, 1.681793f, 2.244924f}  // Maj6/9
        };
        int cIdx = (int)(c * 5.99f);
        if (cIdx < 0)
            cIdx = 0;
        if (cIdx > 5)
            cIdx = 5;
        float drift = 0.001f + t * 0.012f;
        float sawMix = t * 0.55f;
        float sum = 0.0f;
        for (int v = 0; v < 4; v++) {
            float vp = phase * kChords[cIdx][v] +
                       noteAge * drift * (v + 1) * ((v & 1) ? 1.0f : -1.0f) +
                       0.21f * v;
            vp -= (float)floor(vp);
            float tri = (vp < 0.5f) ? (vp * 4.0f - 1.0f) : (3.0f - vp * 4.0f);
            float warm = tri * (1.5f - 0.5f * tri * tri);
            float saw = vp * 2.0f - 1.0f;
            sum += warm * (1.0f - sawMix) + saw * sawMix;
        }
        return sum * 0.28f;
    }
    case SMS_SHIMMER: {
        // Lush ambient shimmer pad: warm fundamental + sub + slowly breathing
        // +1 oct, +5th+oct, and +2 oct harmonic halo
        float sub = (sp < 0.5f) ? (sp * 4.0f - 1.0f) : (3.0f - sp * 4.0f);
        sub = sub * (1.5f - 0.5f * sub * sub);
        float fund = (float)sin(p * 2.0 * M_PI);

        float modSpeed = 0.6f + c * 3.5f;
        float breathe1 = 0.5f + 0.5f * (float)sin(noteAge * modSpeed);
        float breathe2 =
            0.5f + 0.5f * (float)sin(noteAge * modSpeed * 1.37f + 1.57f);

        float p2 = (phase * 2.002f + noteAge * 0.03f);
        p2 -= (float)floor(p2);
        float p3 = (phase * 2.997f - noteAge * 0.02f);
        p3 -= (float)floor(p3);
        float p4 = (phase * 4.005f + noteAge * 0.05f);
        p4 -= (float)floor(p4);

        float oct1 = (float)sin(p2 * 2.0 * M_PI) * breathe1;
        float fifth = (float)sin(p3 * 2.0 * M_PI) * breathe2 * c;
        float oct2 = (float)sin(p4 * 2.0 * M_PI) * (1.0f - breathe1) * t;

        float halo = (oct1 * 0.55f + fifth * 0.35f + oct2 * 0.45f) * t;
        return (fund * 0.55f + sub * 0.30f + halo) * 0.85f;
    }
    case SMS_ENSEMBLE: {
        // String-machine ensemble: 3 phase-modulated string voices
        float depth = 0.02f + t * 0.18f;
        float m1 = (float)sin(noteAge * 5.2f) * depth;
        float m2 = (float)sin(noteAge * 6.7f + 2.1f) * depth;
        float m3 = (float)sin(noteAge * 1.8f + 4.2f) * (depth * 1.5f);

        float p1 = p + m1;
        p1 -= (float)floor(p1);
        float p2 = p + m2 + 0.33f;
        p2 -= (float)floor(p2);
        float p3 = sp + m3;
        p3 -= (float)floor(p3);

        float pw = 0.25f + c * 0.25f;
        float v1 = (p1 * 2.0f - 1.0f) * (0.4f + c * 0.6f) +
                   ((p1 < pw) ? 0.5f : -0.5f) * (1.0f - c * 0.5f);
        float v2 = (p2 * 2.0f - 1.0f) * (0.4f + c * 0.6f) +
                   ((p2 < (1.0f - pw)) ? 0.5f : -0.5f) * (1.0f - c * 0.5f);
        float v3 = (p3 < 0.5f) ? (p3 * 4.0f - 1.0f) : (3.0f - p3 * 4.0f);

        return (v1 + v2 + v3 * 0.7f) * 0.36f;
    }
    case SMS_GRAINPAD: {
        // GrainBrick AMBIENT.json granular + dual-shimmer cloud pad:
        // 4 overlapping Hann-windowed grains (-12 sub, detuned reverse-swell
        // fundamental, +12 octave shimmer, +19 octave+fifth crystal shimmer)
        float spray = 0.2f + c * 1.6f;
        float gRate = 1.4f + t * 2.8f;

        float gp0 = noteAge * gRate;
        gp0 -= (float)floor(gp0);
        float gp1 = gp0 + 0.25f;
        if (gp1 >= 1.0f)
            gp1 -= 1.0f;
        float gp2 = gp0 + 0.50f;
        if (gp2 >= 1.0f)
            gp2 -= 1.0f;
        float gp3 = gp0 + 0.75f;
        if (gp3 >= 1.0f)
            gp3 -= 1.0f;

        float w0 = 4.0f * gp0 * (1.0f - gp0);
        float w1 = gp1 * gp1 * (1.0f - gp1) * 6.75f;
        float w2 = 4.0f * gp2 * (1.0f - gp2);
        w2 = w2 * w2 * (3.0f - 2.0f * w2);
        float w3 = 4.0f * gp3 * (1.0f - gp3);
        w3 = w3 * w3 * (3.0f - 2.0f * w3);

        float subOsc = (float)sin((sp + noteAge * 0.015f * spray) * 2.0 * M_PI);
        float fundOsc = (float)sin((p - noteAge * 0.022f * spray) * 2.0 * M_PI);
        float g0 = (subOsc * 0.55f + fundOsc * 0.45f) * w0;

        float revPh = 1.0f - p + noteAge * (0.04f + 0.08f * c);
        revPh -= (float)floor(revPh);
        float tri1 = (revPh < 0.5f) ? (revPh * 4.0f - 1.0f)
                                    : (3.0f - revPh * 4.0f);
        tri1 = tri1 * (1.5f - 0.5f * tri1 * tri1);
        float g1 = (tri1 * (1.0f - c * 0.4f) +
                    (float)sin(revPh * 2.0 * M_PI) * (c * 0.4f)) *
                   w1;

        float sh1Ph = phase * 2.003f + noteAge * 0.05f * spray;
        sh1Ph -= (float)floor(sh1Ph);
        float g2 = (float)sin(sh1Ph * 2.0 * M_PI) * w2 * (0.35f + t * 0.65f);

        float sh2Ph = phase * 2.996614f - noteAge * 0.037f * spray;
        sh2Ph -= (float)floor(sh2Ph);
        float g3 = (float)sin(sh2Ph * 2.0 * M_PI) * w3 * (t * (0.25f + c * 0.55f));

        return (g0 * 0.50f + g1 * 0.45f + g2 * 0.42f + g3 * 0.38f) * 0.85f;
    }
    case SMS_ORBIX:
    default: {
        // GrainBrick ORBIX generative bouncing-particle modal synth:
        // Exact 4-partial modal tables from libgrainbrick_orbix.so (.rodata):
        //   0: Warm Pad / Fifths {1.0, 1.4983, 2.0, 2.9966}
        //   1: Glass             {1.0, 2.0, 2.76, 4.18}
        //   2: Bell              {1.0, 2.01, 3.38, 5.41}
        static const float kOrbixRatios[3][4] = {
            {1.0f, 1.498307f, 2.000000f, 2.996614f},
            {1.0f, 2.000000f, 2.760000f, 4.180000f},
            {1.0f, 2.010000f, 3.380000f, 5.410000f}};
        static const float kOrbixAmps[4] = {0.50f, 0.35f, 0.25f, 0.15f};
        static const float kOrbixDecays[4] = {1.2f, 2.1f, 3.2f, 4.6f};

        float morph = c * 2.0f;
        int mIdx = (int)floor(morph);
        if (mIdx < 0)
            mIdx = 0;
        if (mIdx > 1)
            mIdx = 1;
        float mf = morph - (float)mIdx;

        float bounceRate = 0.8f + t * 4.2f;
        float sum = 0.0f;
        for (int k = 0; k < 4; k++) {
            float ratio = kOrbixRatios[mIdx][k] * (1.0f - mf) +
                          kOrbixRatios[mIdx + 1][k] * mf;
            float bCycle =
                noteAge * bounceRate * (0.65f + 0.27f * k) + 0.31f * k;
            float bFrac = bCycle - (float)floor(bCycle);
            float atk = (bFrac < 0.12f) ? (bFrac / 0.12f) : 1.0f;
            float dec = (float)exp(-bFrac * kOrbixDecays[k] * (1.2f - t * 0.5f));
            float envK = 0.28f + 0.72f * atk * dec;

            float pk = phase * ratio + noteAge * 0.004f * (k + 1);
            pk -= (float)floor(pk);
            sum += (float)sin(pk * 2.0 * M_PI) * kOrbixAmps[k] * envK;
        }
        float sub = (float)sin(sp * 2.0 * M_PI) * 0.28f;
        return (sum + sub) * 0.85f;
    }
    }
}

void SynthInstrument::enableUpdater(Voice &v, I_SRPUpdater &u) {
    if (!u.Enabled()) {
        u.Enable();
        v.activeUpdaters_.push_back(&u);
    }
}

void SynthInstrument::applyUpdaters(Voice &v) {
    struct RUParams rup;
    rup.cutOffset_ = rup.resOffset_ = rup.volumeOffset_ = rup.panOffset_ =
        rup.fbMixOffset_ = rup.fbTunOffset_ = 0;
    rup.speedOffset_ = FP_ONE;

    for (std::vector<I_SRPUpdater *>::iterator it = v.activeUpdaters_.begin();
         it != v.activeUpdaters_.end(); it++) {
        (*it)->UpdateSRP(rup);
    }

    v.vol_ = v.baseVol_ + fp2fl(rup.volumeOffset_);
    if (v.vol_ < 0.0f)
        v.vol_ = 0.0f;
    if (v.vol_ > 255.0f)
        v.vol_ = 255.0f;

    v.pan_ = v.basePan_ + fp2fl(rup.panOffset_);
    if (v.pan_ < 0.0f)
        v.pan_ = 0.0f;
    if (v.pan_ > 254.0f)
        v.pan_ = 254.0f;

    v.cut_ = v.baseCut_ + fp2fl(rup.cutOffset_);
    if (v.cut_ < 0.0f)
        v.cut_ = 0.0f;
    if (v.cut_ > 1.0f)
        v.cut_ = 1.0f;

    v.res_ = v.baseRes_ + fp2fl(rup.resOffset_);
    if (v.res_ < 0.0f)
        v.res_ = 0.0f;
    if (v.res_ > 1.0f)
        v.res_ = 1.0f;

    v.curInc_ = v.baseInc_ * fp2fl(rup.speedOffset_);
}

void SynthInstrument::restartEnvelope(Voice &v) {
    v.env_ = 0.0f;
    v.noteAge_ = 0.0f;
    v.stage_ = ENV_ATTACK;
    if (attack_->GetInt() == 0) {
        v.env_ = 1.0f;
        v.stage_ = ENV_DECAY;
        if (decay_->GetInt() == 0) {
            v.env_ = sustain_->GetInt() / 255.0f;
            v.stage_ = ENV_SUSTAIN;
        }
    }
}

bool SynthInstrument::Start(int channel, unsigned char note, int flags) {
    bool muted = (flags & 2) != 0;
    if (muted) {
        voice_[channel].active_ = false;
        voice_[channel].stage_ = ENV_OFF;
        return true;
    }

    float driverRate = (float)Audio::GetInstance()->GetSampleRate();
    if (driverRate < 8000.0f) {
        driverRate = 44100.0f;
    }

    float fine = (fineTune_->GetInt() - 0x7F) / 128.0f;
    int offset = 0;
    if (!transpose_ || transpose_->GetInt() != 0) {
        offset = int(note) - rootNote_->GetInt();
    }
    float freq = 261.6255653006f * (float)pow(2.0, (offset + fine) / 12.0);

    Voice &v = voice_[channel];
    if (v.lastNote_ < 0) {
        v.lastNote_ = note;
    }

    v.active_ = true;
    v.note_ = note;
    v.noteAge_ = 0.0f;
    v.phase_ = 0.0f;
    v.phase2_ = 0.37f;
    v.multPhase_ = 0.0f;
    v.lfoPhase_ = 0.0f;
    v.pitchMod_ = 1.0f;
    v.baseInc_ = freq / driverRate;
    v.curInc_ = v.baseInc_;
    v.holdSampleL_ = 0.0f;
    v.holdSampleR_ = 0.0f;
    v.degradeCounter_ = 0;
    v.fmPrev_ = 0.0f;
    v.baseVol_ = (float)volume_->GetInt();
    v.vol_ = v.baseVol_;
    v.basePan_ = (float)pan_->GetInt();
    if (v.basePan_ > 254.0f)
        v.basePan_ = 254.0f;
    v.pan_ = v.basePan_;
    v.baseCut_ = filterCut_->GetInt() / 255.0f;
    v.cut_ = v.baseCut_;
    v.baseRes_ = filterRes_->GetInt() / 255.0f;
    v.res_ = v.baseRes_;
    v.svfLpL_ = v.svfBpL_ = v.svfLpR_ = v.svfBpR_ = 0.0f;
    v.krateCount_ = 0;
    v.retrig_ = false;
    v.retrigLoop_ = 0;
    v.retrigCount_ = 0;

    for (std::vector<I_SRPUpdater *>::iterator it = v.updaters_.begin();
         it != v.updaters_.end(); it++) {
        (*it)->Disable();
    }
    v.activeUpdaters_.clear();

    restartEnvelope(v);
    return true;
}

void SynthInstrument::Stop(int channel) {
    Voice &v = voice_[channel];
    if (!v.active_) {
        return;
    }
    if (release_->GetInt() == 0) {
        v.active_ = false;
        v.stage_ = ENV_OFF;
        v.env_ = 0.0f;
        return;
    }
    v.stage_ = ENV_RELEASE;
}

void SynthInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
    Voice &v = voice_[channel];
    if (!v.active_) {
        return;
    }

    switch (cc) {
    case I_CMD_ARPG: {
        v.arp_.SetData(value);
        enableUpdater(v, v.arp_);
        break;
    }
    case I_CMD_VOLM: {
        float targetVolume = float(value & 0xFF);
        float speed = float(value >> 8);
        float startVolume = v.vol_;
        float baseVolume = v.baseVol_;
        int sampleCount =
            int(4 * SyncMaster::GetInstance()->GetTickSampleCount());
        speed = (speed == 0)
                    ? 0
                    : fabs(targetVolume - startVolume) *
                          SYNTH_KRATE_SAMPLE_COUNT / float(speed) / sampleCount;
        v.volumeRamp_.SetData(targetVolume - baseVolume, speed,
                              startVolume - baseVolume);
        enableUpdater(v, v.volumeRamp_);
        break;
    }
    case I_CMD_PAN_: {
        float targetPan = float(value & 0xFF);
        if (targetPan == 0xFF) {
            targetPan = 0xFE;
        }
        float basePan = v.basePan_;
        float speed = float(value >> 8);
        float startPan = v.pan_;
        int sampleCount =
            int(4 * SyncMaster::GetInstance()->GetTickSampleCount());
        speed = (speed == 0)
                    ? 0
                    : fabs(targetPan - startPan) * SYNTH_KRATE_SAMPLE_COUNT /
                          float(speed) / sampleCount;
        v.panner_.SetData(targetPan - basePan, speed, startPan - basePan);
        enableUpdater(v, v.panner_);
        break;
    }
    case I_CMD_FCUT: {
        float target = float(value & 0xFF) / 255.0f;
        float speed = float(value >> 8);
        float start = v.cut_;
        float baseCut = v.baseCut_;
        int sampleCount =
            int(4 * SyncMaster::GetInstance()->GetTickSampleCount());
        speed = (speed == 0)
                    ? 0
                    : fabs(target - start) * SYNTH_KRATE_SAMPLE_COUNT /
                          float(speed) / sampleCount;
        v.cutRamp_.SetData(target - baseCut, speed, start - baseCut);
        enableUpdater(v, v.cutRamp_);
        break;
    }
    case I_CMD_FRES: {
        float target = float(value & 0xFF) / 255.0f;
        float speed = float(value >> 8);
        float start = v.res_;
        float baseRes = v.baseRes_;
        int sampleCount =
            int(4 * SyncMaster::GetInstance()->GetTickSampleCount());
        speed = (speed == 0)
                    ? 0
                    : fabs(target - start) * SYNTH_KRATE_SAMPLE_COUNT /
                          float(speed) / sampleCount;
        v.resRamp_.SetData(target - baseRes, speed, start - baseRes);
        enableUpdater(v, v.resRamp_);
        break;
    }
    case I_CMD_FLTR: {
        v.cut_ = v.baseCut_ = (value >> 8) / 255.0f;
        v.res_ = v.baseRes_ = (value & 0xFF) / 255.0f;
        break;
    }
    case I_CMD_PTCH: {
        int pitch = (char)(value & 0xFF);
        float speed = float(value >> 8);
        if (pitch > 127)
            pitch = pitch - 256;
        float targetSpeed = float(pow(2.0, pitch / 12.0));
        float srcSpeed = (v.baseInc_ != 0.0f) ? (v.curInc_ / v.baseInc_) : 1.0f;
        speed = (speed == 0)
                    ? 0.0f
                    : srcSpeed * 255.0f / speed / SYNTH_KRATE_SAMPLE_COUNT /
                          32.0f;
        v.speedRamp_.SetData(targetSpeed, speed, srcSpeed);
        enableUpdater(v, v.speedRamp_);
        break;
    }
    case I_CMD_LEGA: {
        int pitch = (char)(value & 0xFF);
        float speed = float(value >> 8);
        if (pitch > 127)
            pitch = pitch - 256;

        float targetSpeed, initSpeed;
        if (pitch == 0) {
            pitch = v.lastNote_ - v.note_;
            targetSpeed = 1.0f;
            initSpeed = float(pow(2.0f, pitch / 12.0f));
        } else {
            initSpeed = (v.baseInc_ != 0.0f) ? (v.curInc_ / v.baseInc_) : 1.0f;
            targetSpeed = float(pow(2.0f, pitch / 12.0f));
        }
        speed = (speed == 0)
                    ? 0.0f
                    : float(1 + 50.0 / SYNTH_KRATE_SAMPLE_COUNT / speed);
        v.legato_.SetData(targetSpeed, speed, initSpeed);
        enableUpdater(v, v.legato_);
        break;
    }
    case I_CMD_PFIN: {
        float semi = (value & 0xFF) / float(0x80);
        if (semi > 1)
            semi = semi - 2;
        float speed = float(value >> 8);
        float initSpeed = v.pfin_.Enabled() ? v.pfin_.GetCurrent() : 1.0f;
        float targetSpeed = float(pow(2.0f, semi / 12.0f));
        speed = (speed == 0)
                    ? 0.0f
                    : float(1 + 50.0 / SYNTH_KRATE_SAMPLE_COUNT / speed);
        v.pfin_.SetData(targetSpeed, speed, initSpeed);
        enableUpdater(v, v.pfin_);
        break;
    }
    case I_CMD_RTRG: {
        unsigned char loop = (value & 0xFF);
        if (loop != 0) {
            v.retrig_ = true;
            v.retrigLoop_ = loop;
            v.retrigCount_ = loop;
        } else {
            v.retrig_ = false;
        }
        break;
    }
    default:
        break;
    }
}

bool SynthInstrument::Render(int channel, fixed *buffer, int size, int flags) {
    bool mute = (flags & 2) != 0;
    bool updateTick = (flags & 1) != 0;
    Voice &v = voice_[channel];

    if (mute) {
        if (v.active_) {
            Stop(channel);
            v.active_ = false;
            v.stage_ = ENV_OFF;
            v.env_ = 0.0f;
        }
        return false;
    }

    if (!v.active_ || v.stage_ == ENV_OFF) {
        return false;
    }

    v.lastNote_ = v.note_;

    float driverRate = (float)Audio::GetInstance()->GetSampleRate();
    if (driverRate < 8000.0f) {
        driverRate = 44100.0f;
    }
    float dt = 1.0f / driverRate;

    bool isShapesMode = (GetEngineMode() == SYM_SHAPES);
    int wave = waveform_->GetInt();
    if (wave < 0 || wave >= SYW_LAST) {
        wave = SYW_SAW;
    }
    int macroShape = macroShape_->GetInt();
    if (macroShape < 0 || macroShape >= SMS_LAST) {
        macroShape = SMS_CSAW;
    }

    int sizeParam = size_->GetInt();
    int multParam = mult_->GetInt();
    int warpParam = warp_->GetInt();
    int scanParam = scan_->GetInt();
    int timbreParam = timbre_->GetInt();
    int colorParam = color_->GetInt();
    int degradeParam = degrade_->GetInt();
    int reduxParam = redux_->GetInt();
    int filtMode = filterMode_->GetInt();
    int ampParam = amp_->GetInt();
    int limMode = lim_->GetInt();
    int choParam = chorus_->GetInt();

    int degradeStride = 1 + (degradeParam * 31) / 255;
    int reduxLevels = (reduxParam > 0) ? (128 - (reduxParam * 124) / 255) : 0;
    if (reduxLevels < 2 && reduxParam > 0) {
        reduxLevels = 2;
    }

    float atkInc = envTimeToInc(attack_->GetInt(), driverRate);
    float decInc = envTimeToInc(decay_->GetInt(), driverRate);
    float relInc = envTimeToInc(release_->GetInt(), driverRate);
    float sustainLvl = sustain_->GetInt() / 255.0f;

    float lfoRate = 0.1f + (lfoRate_->GetInt() / 255.0f) * 19.9f;
    float lfoInc = lfoRate / driverRate;
    float lfoDepthSemis = (lfoDepth_->GetInt() / 255.0f) * 2.0f;

    bool hasUpdaters = !v.activeUpdaters_.empty();
    if (!hasUpdaters) {
        v.cut_ = v.baseCut_ = filterCut_->GetInt() / 255.0f;
        v.res_ = v.baseRes_ = filterRes_->GetInt() / 255.0f;
        v.vol_ = v.baseVol_ = (float)volume_->GetInt();
        v.pan_ = v.basePan_ = (float)pan_->GetInt();
    }

    if (updateTick) {
        if (hasUpdaters) {
            for (std::vector<I_SRPUpdater *>::iterator it =
                     v.activeUpdaters_.begin();
                 it != v.activeUpdaters_.end(); it++) {
                (*it)->Trigger(true);
            }
            applyUpdaters(v);
        }
        if (v.retrig_) {
            if (v.retrigCount_ == 0) {
                restartEnvelope(v);
                v.phase_ = 0.0f;
                v.retrigCount_ = v.retrigLoop_;
            }
            v.retrigCount_--;
        }
    }

    const float pcmScale = 8191.0f;
    SYS_MEMSET(buffer, 0, size * 2 * sizeof(fixed));

    float lfoBlock = lfoInc * (float)SYNTH_KRATE_SAMPLE_COUNT;
    float multRatio = 1.0f + (multParam / 255.0f) * 7.0f;
    float choDetune = 1.0f + (choParam / 255.0f) * 0.035f;
    float choMix = (choParam / 255.0f) * 0.65f;

    float cutNorm = v.cut_;
    if (cutNorm < 0.02f)
        cutNorm = 0.02f;
    if (cutNorm > 0.99f)
        cutNorm = 0.99f;
    float svfF = cutNorm * cutNorm * 0.85f;
    float svfQ = 1.0f - (v.res_ * 0.92f);
    if (svfQ < 0.08f)
        svfQ = 0.08f;

    int modTimbre = timbreParam;
    for (int i = 0; i < size; i++) {
        if (--v.krateCount_ <= 0) {
            v.krateCount_ = SYNTH_KRATE_SAMPLE_COUNT;
            if (hasUpdaters) {
                for (std::vector<I_SRPUpdater *>::iterator it =
                         v.activeUpdaters_.begin();
                     it != v.activeUpdaters_.end(); it++) {
                    (*it)->Trigger(false);
                }
                applyUpdaters(v);
            }
            v.lfoPhase_ += lfoBlock;
            if (v.lfoPhase_ >= 1.0f) {
                v.lfoPhase_ -= (float)floor(v.lfoPhase_);
            }
            float lfo = 0.0f;
            if (lfoDepthSemis > 0.0f) {
                lfo = (float)sin(v.lfoPhase_ * 2.0 * M_PI);
                bool isPadShape = isShapesMode && (macroShape >= SMS_CHORD);
                float pDepth = isPadShape ? (lfoDepthSemis * 0.15f) : lfoDepthSemis;
                v.pitchMod_ = (float)pow(2.0, (lfo * pDepth) / 12.0);
                int tDelta = (int)(lfo * (lfoDepth_->GetInt() * 0.35f));
                modTimbre = timbreParam + tDelta;
                if (modTimbre < 0)
                    modTimbre = 0;
                if (modTimbre > 255)
                    modTimbre = 255;
            } else {
                v.pitchMod_ = 1.0f;
                modTimbre = timbreParam;
            }
            cutNorm = v.cut_ + lfo * (lfoDepth_->GetInt() / 255.0f) * 0.25f;
            if (cutNorm < 0.02f)
                cutNorm = 0.02f;
            if (cutNorm > 0.99f)
                cutNorm = 0.99f;
            svfF = cutNorm * cutNorm * 0.85f;
            svfQ = 1.0f - (v.res_ * 0.92f);
            if (svfQ < 0.08f)
                svfQ = 0.08f;
        }

        switch (v.stage_) {
        case ENV_ATTACK:
            v.env_ += atkInc;
            if (v.env_ >= 1.0f) {
                v.env_ = 1.0f;
                v.stage_ = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            v.env_ -= decInc;
            if (v.env_ <= sustainLvl) {
                v.env_ = sustainLvl;
                v.stage_ = ENV_SUSTAIN;
            }
            break;
        case ENV_SUSTAIN:
            v.env_ = sustainLvl;
            break;
        case ENV_RELEASE:
            v.env_ -= relInc;
            if (v.env_ <= 0.0f) {
                v.env_ = 0.0f;
                v.stage_ = ENV_OFF;
                v.active_ = false;
            }
            break;
        case ENV_OFF:
        default:
            break;
        }

        if (!v.active_) {
            break;
        }

        float pitchMult = 1.0f;
        float sL = 0.0f;
        float sR = 0.0f;

        if (!isShapesMode) {
            float shapedP =
                shapePhase(v.phase_, sizeParam, warpParam, scanParam);
            float s1 = sampleWave(wave, shapedP, v.noise_);
            if (multParam > 0) {
                float mp = shapePhase(v.multPhase_, sizeParam, 0, 0);
                float mWave = sampleWave(wave, mp, v.noise_);
                float mAmt = multParam / 255.0f;
                s1 = s1 * (1.0f - mAmt * 0.6f) + (s1 * mWave) * (mAmt * 0.6f);
            }
            sL = s1;
            sR = s1;
            if (choParam > 0) {
                float shapedP2 =
                    shapePhase(v.phase2_, sizeParam, warpParam, scanParam);
                float s2 = sampleWave(wave, shapedP2, v.noise_);
                sL = (s1 + s2 * choMix) / (1.0f + choMix * 0.6f);
                sR = (s1 - s2 * choMix) / (1.0f + choMix * 0.6f);
            }
        } else {
            float s1 = sampleMacroShape(macroShape, v.phase_, v.multPhase_,
                                        v.noteAge_, modTimbre, colorParam,
                                        v.noise_, v.fmPrev_, pitchMult);
            sL = s1;
            sR = s1;
            if (choParam > 0) {
                float dummyPm = 1.0f;
                float s2 = sampleMacroShape(macroShape, v.phase2_, v.multPhase_,
                                            v.noteAge_, modTimbre, colorParam,
                                            v.noise_, v.fmPrev_, dummyPm);
                sL = (s1 + s2 * choMix) / (1.0f + choMix * 0.6f);
                sR = (s1 - s2 * choMix) / (1.0f + choMix * 0.6f);
            }
            if (degradeStride > 1) {
                if (v.degradeCounter_ <= 0) {
                    v.holdSampleL_ = sL;
                    v.holdSampleR_ = sR;
                    v.degradeCounter_ = degradeStride;
                } else {
                    sL = v.holdSampleL_;
                    sR = v.holdSampleR_;
                }
                v.degradeCounter_--;
            }
            if (reduxLevels > 0) {
                sL = (float)floor(sL * (float)reduxLevels + 0.5f) /
                     (float)reduxLevels;
                sR = (float)floor(sR * (float)reduxLevels + 0.5f) /
                     (float)reduxLevels;
            }
        }

        sL = applyLimiter(sL * v.env_, ampParam, limMode);
        sR = applyLimiter(sR * v.env_, ampParam, limMode);

        if (filtMode != SYF_OFF) {
            float hpL = sL - v.svfLpL_ - svfQ * v.svfBpL_;
            v.svfBpL_ += svfF * hpL;
            v.svfLpL_ += svfF * v.svfBpL_;

            float hpR = sR - v.svfLpR_ - svfQ * v.svfBpR_;
            v.svfBpR_ += svfF * hpR;
            v.svfLpR_ += svfF * v.svfBpR_;

            if (v.svfBpL_ > 4.0f)
                v.svfBpL_ = 4.0f;
            if (v.svfBpL_ < -4.0f)
                v.svfBpL_ = -4.0f;
            if (v.svfLpL_ > 4.0f)
                v.svfLpL_ = 4.0f;
            if (v.svfLpL_ < -4.0f)
                v.svfLpL_ = -4.0f;
            if (v.svfBpR_ > 4.0f)
                v.svfBpR_ = 4.0f;
            if (v.svfBpR_ < -4.0f)
                v.svfBpR_ = -4.0f;
            if (v.svfLpR_ > 4.0f)
                v.svfLpR_ = 4.0f;
            if (v.svfLpR_ < -4.0f)
                v.svfLpR_ = -4.0f;

            switch (filtMode) {
            case SYF_LOWPASS:
                sL = v.svfLpL_;
                sR = v.svfLpR_;
                break;
            case SYF_HIGHPASS:
                sL = hpL;
                sR = hpR;
                break;
            case SYF_BANDPASS:
                sL = v.svfBpL_;
                sR = v.svfBpR_;
                break;
            case SYF_LPHP:
                sL = (v.svfLpL_ + hpL) * 0.7f;
                sR = (v.svfLpR_ + hpR) * 0.7f;
                break;
            default:
                break;
            }
        }

        float inc = v.curInc_ * v.pitchMod_ * pitchMult;
        v.phase_ += inc;
        if (v.phase_ >= 1.0f) {
            v.phase_ -= (float)floor(v.phase_);
        } else if (v.phase_ < 0.0f) {
            v.phase_ -= (float)floor(v.phase_);
        }

        if (!isShapesMode) {
            if (multParam > 0) {
                v.multPhase_ += inc * multRatio;
                if (v.multPhase_ >= 1.0f) {
                    v.multPhase_ -= (float)floor(v.multPhase_);
                }
            }
        } else {
            // Sub-oscillator phase (0.5x frequency)
            v.multPhase_ += inc * 0.5f;
            if (v.multPhase_ >= 1.0f) {
                v.multPhase_ -= (float)floor(v.multPhase_);
            }
        }

        if (choParam > 0) {
            v.phase2_ += inc * choDetune;
            if (v.phase2_ >= 1.0f) {
                v.phase2_ -= (float)floor(v.phase2_);
            }
        }
        v.noteAge_ += dt;

        float vol = v.vol_ / 255.0f;
        float pan = v.pan_ / 254.0f;
        if (pan < 0.0f)
            pan = 0.0f;
        if (pan > 1.0f)
            pan = 1.0f;
        float gainL = vol * (1.0f - pan);
        float gainR = vol * pan;
        if ((int)(v.pan_ + 0.5f) == 0x80 || (int)(v.pan_ + 0.5f) == 0x7F) {
            gainL = gainR = vol * 0.7071f;
        }

        int outL = (int)(sL * gainL * pcmScale);
        int outR = (int)(sR * gainR * pcmScale);
        buffer[i * 2] = i2fp(outL);
        buffer[i * 2 + 1] = i2fp(outR);
    }

    return v.active_;
}

bool SynthInstrument::IsInitialized() { return true; }

bool SynthInstrument::IsEmpty() { return false; }

const char *SynthInstrument::GetName() {
    if (userName_[0] != '\0') {
        return userName_;
    }
    if (GetEngineMode() == SYM_SHAPES) {
        int m = macroShape_->GetInt();
        if (m < 0 || m >= SMS_LAST) {
            m = 0;
        }
        snprintf(name_, sizeof(name_), "SHP %s", macroShapeLabels[m]);
        return name_;
    }
    int w = waveform_->GetInt();
    if (w < 0 || w >= SYW_LAST) {
        w = 0;
    }
    snprintf(name_, sizeof(name_), "SYN %s", synthShapeLabels[w]);
    return name_;
}

void SynthInstrument::Purge() {
    userName_[0] = '\0';
    IteratorPtr<Variable> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        Variable &v = it->CurrentItem();
        v.Reset();
    }
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        voice_[i].active_ = false;
        voice_[i].stage_ = ENV_OFF;
        voice_[i].activeUpdaters_.clear();
    }
}

int SynthInstrument::GetTable() {
    int result = table_->GetInt();
    if (result > TABLE_COUNT) {
        return VAR_OFF;
    }
    return result;
}

bool SynthInstrument::GetTableAutomation() { return tableAuto_->GetBool(); }

void SynthInstrument::GetTableState(TableSaveState &state) {
    memcpy(state.hopCount_, tableState_.hopCount_, sizeof(uchar) * TABLE_STEPS * 3);
    memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
}

void SynthInstrument::SetTableState(TableSaveState &state) {
    memcpy(tableState_.hopCount_, state.hopCount_, sizeof(uchar) * TABLE_STEPS * 3);
    memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
}

float SynthInstrument::previewEnvAt(float t) const {
    float atk = (float)attack_->GetInt() / 255.0f;
    float dec = (float)decay_->GetInt() / 255.0f;
    float rel = (float)release_->GetInt() / 255.0f;
    float sus = (float)sustain_->GetInt() / 255.0f;
    if (atk < 0.02f)
        atk = 0.02f;
    if (dec < 0.02f)
        dec = 0.02f;
    if (rel < 0.02f)
        rel = 0.02f;
    const float susW = 0.35f;
    float sum = atk + dec + susW + rel;
    float aEnd = atk / sum;
    float dEnd = aEnd + dec / sum;
    float sEnd = dEnd + susW / sum;

    if (t <= aEnd) {
        float u = (aEnd > 0.0f) ? (t / aEnd) : 1.0f;
        return u;
    }
    if (t <= dEnd) {
        float u = (dEnd > aEnd) ? ((t - aEnd) / (dEnd - aEnd)) : 1.0f;
        return 1.0f + (sus - 1.0f) * u;
    }
    if (t <= sEnd) {
        return sus;
    }
    float u = (1.0f > sEnd) ? ((t - sEnd) / (1.0f - sEnd)) : 1.0f;
    float e = sus * (1.0f - u);
    return (e < 0.0f) ? 0.0f : e;
}

void SynthInstrument::FillPhrasePreview(signed char *waveBins,
                                        signed char *lfoBins, int nBins) const {
    if (nBins <= 0) {
        return;
    }
    bool isShapesMode = (GetEngineMode() == SYM_SHAPES);
    int wave = waveform_->GetInt();
    if (wave < 0 || wave >= SYW_LAST) {
        wave = SYW_SAW;
    }
    int macroShape = macroShape_->GetInt();
    if (macroShape < 0 || macroShape >= SMS_LAST) {
        macroShape = SMS_CSAW;
    }
    int sizeParam = size_->GetInt();
    int multParam = mult_->GetInt();
    int warpParam = warp_->GetInt();
    int scanParam = scan_->GetInt();
    int timbreParam = timbre_->GetInt();
    int colorParam = color_->GetInt();
    int degradeParam = degrade_->GetInt();
    int reduxParam = redux_->GetInt();
    int ampParam = amp_->GetInt();
    int limMode = lim_->GetInt();
    float multRatio = 1.0f + (multParam / 255.0f) * 7.0f;

    const float oscCycles = 3.0f;
    float lfoCycles = 0.5f + (lfoRate_->GetInt() / 255.0f) * 4.0f;
    float lfoDepth = lfoDepth_->GetInt() / 255.0f;
    unsigned int noise = 0xA5A5u;
    float fmPrev = 0.0f;
    float accPhase = 0.0f;
    float holdVal = 0.0f;
    int degStep = 1 + (degradeParam * 6) / 255;
    int redLevels = (reduxParam > 0) ? (32 - (reduxParam * 29) / 255) : 0;
    if (redLevels < 2 && reduxParam > 0) {
        redLevels = 2;
    }

    for (int i = 0; i < nBins; i++) {
        float t = (nBins == 1) ? 0.0f : (float)i / (float)(nBins - 1);
        float env = previewEnvAt(t);
        float s = 0.0f;
        if (!isShapesMode) {
            float phase = t * oscCycles;
            phase -= (float)floor(phase);
            float sp = shapePhase(phase, sizeParam, warpParam, scanParam);
            s = sampleWave(wave, sp, noise);
            if (multParam > 0) {
                float mp = shapePhase(t * oscCycles * multRatio, sizeParam, 0, 0);
                float mWave = sampleWave(wave, mp, noise);
                float mAmt = multParam / 255.0f;
                s = s * (1.0f - mAmt * 0.6f) + (s * mWave) * (mAmt * 0.6f);
            }
        } else {
            float pm = 1.0f;
            s = sampleMacroShape(macroShape, accPhase, accPhase * 0.5f,
                                 t * 0.25f, timbreParam, colorParam, noise,
                                 fmPrev, pm);
            accPhase += (oscCycles / (float)nBins) * pm;
            if (degStep > 1) {
                if ((i % degStep) == 0) {
                    holdVal = s;
                } else {
                    s = holdVal;
                }
            }
            if (redLevels > 0) {
                s = (float)floor(s * (float)redLevels + 0.5f) /
                    (float)redLevels;
            }
        }
        s = applyLimiter(s * env, ampParam, limMode);
        int w = (int)(s * 127.0f);
        if (w > 127)
            w = 127;
        if (w < -128)
            w = -128;
        waveBins[i] = (signed char)w;

        float lfo = (float)sin(t * lfoCycles * 2.0 * M_PI) * lfoDepth;
        int l = (int)(lfo * 127.0f);
        if (l > 127)
            l = 127;
        if (l < -128)
            l = -128;
        lfoBins[i] = (signed char)l;
    }
}
