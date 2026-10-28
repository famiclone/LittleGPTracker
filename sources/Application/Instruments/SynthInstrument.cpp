#include "SynthInstrument.h"
#include "Application/Player/SyncMaster.h"
#include "CommandList.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char *synthWaveforms[] = {"saw", "triangle", "sine", "square", "noise"};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYNTH_KRATE_SAMPLE_COUNT 100

SynthInstrument::SynthInstrument() {
    strcpy(name_, "synth");

    waveform_ = new Variable("waveform", SYP_WAVEFORM, synthWaveforms, SYW_LAST, SYW_SAW);
    Insert(waveform_);

    volume_ = new Variable("volume", SYP_VOLUME, 0x80);
    Insert(volume_);

    pan_ = new Variable("pan", SYP_PAN, 0x7F);
    Insert(pan_);

    rootNote_ = new Variable("root note", SYP_ROOTNOTE, 60);
    Insert(rootNote_);

    fineTune_ = new Variable("fine tune", SYP_FINETUNE, 0x7F);
    Insert(fineTune_);

    attack_ = new Variable("attack", SYP_ATTACK, 0x10);
    Insert(attack_);

    decay_ = new Variable("decay", SYP_DECAY, 0x20);
    Insert(decay_);

    sustain_ = new Variable("sustain", SYP_SUSTAIN, 0xC0);
    Insert(sustain_);

    release_ = new Variable("release", SYP_RELEASE, 0x30);
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
        v.phase_ = 0.0f;
        v.baseInc_ = 0.0f;
        v.curInc_ = 0.0f;
        v.lfoPhase_ = 0.0f;
        v.pitchMod_ = 1.0f;
        v.note_ = 0;
        v.lastNote_ = -1;
        v.noise_ = 0xACE1u + (unsigned)i * 97u;
        v.baseVol_ = 0x80;
        v.vol_ = 0x80;
        v.basePan_ = 0x7F;
        v.pan_ = 0x7F;
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

float SynthInstrument::envTimeToInc(int param, float sampleRate) const {
    float t = (float)param / 255.0f;
    float seconds = t * t * 2.0f;
    if (seconds < 1.0f / sampleRate) {
        seconds = 1.0f / sampleRate;
    }
    return 1.0f / (seconds * sampleRate);
}

float SynthInstrument::sampleWave(int wave, float phase, unsigned int &noise) const {
    switch (wave) {
    case SYW_SAW:
        return phase * 2.0f - 1.0f;
    case SYW_TRIANGLE:
        return (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
    case SYW_SINE:
        return (float)sin(phase * 2.0 * M_PI);
    case SYW_SQUARE:
        return (phase < 0.5f) ? 1.0f : -1.0f;
    case SYW_NOISE:
    default:
        noise = noise * 1103515245u + 12345u;
        return ((noise >> 16) & 0xFFFF) / 32768.0f - 1.0f;
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

    v.curInc_ = v.baseInc_ * fp2fl(rup.speedOffset_);
}

void SynthInstrument::restartEnvelope(Voice &v) {
    v.env_ = 0.0f;
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
    int offset = int(note) - rootNote_->GetInt();
    float freq = 261.6255653006f * (float)pow(2.0, (offset + fine) / 12.0);

    Voice &v = voice_[channel];
    // Keep lastNote_ from previous voice for LEGA on this tick.
    if (v.lastNote_ < 0) {
        v.lastNote_ = note;
    }

    v.active_ = true;
    v.note_ = note;
    v.phase_ = 0.0f;
    v.lfoPhase_ = 0.0f;
    v.pitchMod_ = 1.0f;
    v.baseInc_ = freq / driverRate;
    v.curInc_ = v.baseInc_;
    v.baseVol_ = (float)volume_->GetInt();
    v.vol_ = v.baseVol_;
    v.basePan_ = (float)pan_->GetInt();
    if (v.basePan_ > 254.0f)
        v.basePan_ = 254.0f;
    v.pan_ = v.basePan_;
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
        // Sample-only FX (LPOF/PLOF/FLTR/CRSH/FB*) are intentionally ignored.
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

    // Latch previous note after first render of this voice (for next LEGA).
    v.lastNote_ = v.note_;

    float driverRate = (float)Audio::GetInstance()->GetSampleRate();
    if (driverRate < 8000.0f) {
        driverRate = 44100.0f;
    }

    int wave = waveform_->GetInt();
    if (wave < 0 || wave >= SYW_LAST) {
        wave = SYW_SAW;
    }

    float atkInc = envTimeToInc(attack_->GetInt(), driverRate);
    float decInc = envTimeToInc(decay_->GetInt(), driverRate);
    float relInc = envTimeToInc(release_->GetInt(), driverRate);
    float sustainLvl = sustain_->GetInt() / 255.0f;

    float lfoRate = 0.1f + (lfoRate_->GetInt() / 255.0f) * 19.9f;
    float lfoInc = lfoRate / driverRate;
    float lfoDepthSemis = (lfoDepth_->GetInt() / 255.0f) * 2.0f;

    bool hasUpdaters = !v.activeUpdaters_.empty();

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

    // Advance LFO/pitch at k-rate — per-sample sin()+pow() with a long
    // sliced sample + synth session was starving the audio callback.
    float lfoBlock = lfoInc * (float)SYNTH_KRATE_SAMPLE_COUNT;

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
            if (lfoDepthSemis > 0.0f) {
                float lfo = (float)sin(v.lfoPhase_ * 2.0 * M_PI);
                v.pitchMod_ =
                    (float)pow(2.0, (lfo * lfoDepthSemis) / 12.0);
            } else {
                v.pitchMod_ = 1.0f;
            }
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

        float s = sampleWave(wave, v.phase_, v.noise_) * v.env_;
        v.phase_ += v.curInc_ * v.pitchMod_;
        if (v.phase_ >= 1.0f) {
            v.phase_ -= (float)floor(v.phase_);
        } else if (v.phase_ < 0.0f) {
            v.phase_ -= (float)floor(v.phase_);
        }

        float vol = v.vol_ / 255.0f;
        float pan = v.pan_ / 254.0f;
        if (pan < 0.0f)
            pan = 0.0f;
        if (pan > 1.0f)
            pan = 1.0f;
        float gainL = vol * (1.0f - pan);
        float gainR = vol * pan;
        if ((int)(v.pan_ + 0.5f) == 0x7F) {
            gainL = gainR = vol * 0.7071f;
        }

        int outL = (int)(s * gainL * pcmScale);
        int outR = (int)(s * gainR * pcmScale);
        buffer[i * 2] = i2fp(outL);
        buffer[i * 2 + 1] = i2fp(outR);
    }

    return v.active_;
}

bool SynthInstrument::IsInitialized() { return true; }

bool SynthInstrument::IsEmpty() { return false; }

const char *SynthInstrument::GetName() {
    int w = waveform_->GetInt();
    if (w < 0 || w >= SYW_LAST) {
        w = 0;
    }
    sprintf(name_, "SYN %s", synthWaveforms[w]);
    return name_;
}

void SynthInstrument::Purge() {
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
    int wave = waveform_->GetInt();
    if (wave < 0 || wave >= SYW_LAST) {
        wave = SYW_SAW;
    }
    const float oscCycles = 3.0f;
    float lfoCycles = 0.5f + (lfoRate_->GetInt() / 255.0f) * 4.0f;
    float lfoDepth = lfoDepth_->GetInt() / 255.0f;
    unsigned int noise = 0xA5A5u;

    for (int i = 0; i < nBins; i++) {
        float t = (nBins == 1) ? 0.0f : (float)i / (float)(nBins - 1);
        float env = previewEnvAt(t);
        float phase = t * oscCycles;
        phase -= (float)floor(phase);
        float s = sampleWave(wave, phase, noise) * env;
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
