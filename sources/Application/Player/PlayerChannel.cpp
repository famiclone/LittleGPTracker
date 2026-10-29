#include "PlayerChannel.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/fixed.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float gbPadeTanh(float x) {
    if (x > 3.2f)
        return 1.0f;
    if (x < -3.2f)
        return -1.0f;
    float x2 = x * x;
    float denom = 27.0f + 9.0f * x2;
    if (denom == 0.0f)
        return 0.0f;
    float y = x * (27.0f + x2) / denom;
    if (y > 1.0f)
        y = 1.0f;
    if (y < -1.0f)
        y = -1.0f;
    return y;
}

PlayerChannel::PlayerChannel(int index) {
    index_ = index;
    instr_ = 0;
    muted_ = false;
    mixBus_ = 0;
    busIndex_ = -1;
    volume_ = i2fp(1);
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    hpfAlpha_ = i2fp(0);
    hpfMode_ = 0;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    lpfAlpha_ = i2fp(0);
    lpfFreq_ = 0;
    delaySteps_ = 0;
    delayAmt_ = 0;
    delayLen_ = 0;
    delayWritePos_ = 0;
    delayBuf_ = 0;
    reverbSize_ = 0;
    reverbAmt_ = 0;
    reverbFb_ = i2fp(0);
    reverbBuf_ = 0;
    for (int i = 0; i < CHANNEL_REVERB_COMBS; i++) {
        combOff_[i] = combLen_[i] = combPos_[i] = 0;
        combDamp_[i][0] = combDamp_[i][1] = i2fp(0);
    }
    for (int i = 0; i < CHANNEL_REVERB_APS; i++) {
        apOff_[i] = apLen_[i] = apPos_[i] = 0;
    }

    tapeAmt_ = 0;
    tapeWritePos_ = 0;
    tapeBuf_ = 0;
    tapeWowPhase_ = 0.0f;
    tapeFlutPhase_ = 0.0f;
    tapeWowDrift_ = 0.0f;
    tapeFlutDrift_ = 0.0f;
    tapeCrinkleEnv_ = 0.0f;
    tapeDropoutEnv_ = 0.0f;
    tapeLpState_[0] = tapeLpState_[1] = 0.0f;
    tapeHissLp_[0] = tapeHissLp_[1] = 0.0f;
    tapeDcIn_[0] = tapeDcIn_[1] = 0.0f;
    tapeDcOut_[0] = tapeDcOut_[1] = 0.0f;
    tapeRng_ = 0xA341316Cu ^ ((unsigned int)index * 0x6D2B79F5u);
}

PlayerChannel::~PlayerChannel() {
    delete[] delayBuf_;
    delayBuf_ = 0;
    delete[] reverbBuf_;
    reverbBuf_ = 0;
    delete[] tapeBuf_;
    tapeBuf_ = 0;
}

void PlayerChannel::StartInstrument(I_Instrument *instr, unsigned char note,
                                    bool trigger) {
    if (instr_) {
        StopInstrument();
    }
    if (instr->Start(index_, note, trigger | (muted_ << 1))) {
        instr_ = instr;
    } else {
        instr_ = 0;
    }
}

void PlayerChannel::StopInstrument() {
    if (instr_) {
        instr_->Stop(index_);
    }
    instr_ = 0;
}

void PlayerChannel::ensureDelayBuffer() {
    if (delayBuf_) {
        return;
    }
    delayBuf_ = new fixed[CHANNEL_DELAY_MAX_FRAMES * 2];
    memset(delayBuf_, 0, sizeof(fixed) * CHANNEL_DELAY_MAX_FRAMES * 2);
    delayWritePos_ = 0;
}

void PlayerChannel::syncDelayLength() {
    if (delaySteps_ == 0) {
        delayLen_ = 0;
        delayWritePos_ = 0;
        return;
    }
    ensureDelayBuffer();
    float stepSamples = SyncMaster::GetInstance()->GetStepSampleCount();
    if (stepSamples < 1.0f) {
        stepSamples = 1.0f;
    }
    int len = (int)(delaySteps_ * stepSamples + 0.5f);
    if (len < 1) {
        len = 1;
    }
    if (len > CHANNEL_DELAY_MAX_FRAMES) {
        len = CHANNEL_DELAY_MAX_FRAMES;
    }
    if (len != delayLen_) {
        delayLen_ = len;
        delayWritePos_ = 0;
        memset(delayBuf_, 0, sizeof(fixed) * CHANNEL_DELAY_MAX_FRAMES * 2);
    }
}

void PlayerChannel::SetDelay(unsigned char steps, unsigned char amt) {
    bool stepsChanged = (steps != delaySteps_);
    delaySteps_ = steps;
    delayAmt_ = amt;
    if (steps == 0) {
        delayLen_ = 0;
        delayWritePos_ = 0;
        return;
    }
    if (stepsChanged || delayLen_ == 0) {
        syncDelayLength();
    } else {
        float stepSamples = SyncMaster::GetInstance()->GetStepSampleCount();
        if (stepSamples < 1.0f) {
            stepSamples = 1.0f;
        }
        int len = (int)(delaySteps_ * stepSamples + 0.5f);
        if (len < 1) {
            len = 1;
        }
        if (len > CHANNEL_DELAY_MAX_FRAMES) {
            len = CHANNEL_DELAY_MAX_FRAMES;
        }
        if (len != delayLen_) {
            delayLen_ = len;
            delayWritePos_ = 0;
            if (delayBuf_) {
                memset(delayBuf_, 0,
                       sizeof(fixed) * CHANNEL_DELAY_MAX_FRAMES * 2);
            }
        }
    }
}

bool PlayerChannel::applyDelay(fixed *buffer, int samplecount) {
    if (delaySteps_ == 0 || delayLen_ < 1 || !delayBuf_) {
        return false;
    }

    fixed wet = fl2fp(delayAmt_ / 255.0f);
    fixed dry = fp_sub(i2fp(1), wet);
    fixed fb = fl2fp(delayAmt_ / 255.0f * 0.85f);
    bool any = false;

    for (int n = 0; n < samplecount; n++) {
        int idx = n * 2;
        int di = delayWritePos_ * 2;
        fixed delayedL = delayBuf_[di];
        fixed delayedR = delayBuf_[di + 1];
        fixed inL = buffer[idx];
        fixed inR = buffer[idx + 1];

        fixed outL = fp_add(fp_mul(dry, inL), fp_mul(wet, delayedL));
        fixed outR = fp_add(fp_mul(dry, inR), fp_mul(wet, delayedR));
        buffer[idx] = outL;
        buffer[idx + 1] = outR;

        delayBuf_[di] = fp_add(inL, fp_mul(fb, delayedL));
        delayBuf_[di + 1] = fp_add(inR, fp_mul(fb, delayedR));

        delayWritePos_++;
        if (delayWritePos_ >= delayLen_) {
            delayWritePos_ = 0;
        }

        if (outL != 0 || outR != 0 || inL != 0 || inR != 0 || delayedL != 0 ||
            delayedR != 0) {
            any = true;
        }
    }
    return any;
}

void PlayerChannel::ensureReverbBuffer() {
    if (reverbBuf_) {
        return;
    }
    reverbBuf_ = new fixed[CHANNEL_REVERB_BUF_FRAMES];
    memset(reverbBuf_, 0, sizeof(fixed) * CHANNEL_REVERB_BUF_FRAMES);
}

void PlayerChannel::syncReverb() {
    if (reverbSize_ == 0) {
        return;
    }
    ensureReverbBuffer();

    static const int kCombBase[CHANNEL_REVERB_COMBS] = {1116, 1188, 1277, 1356};
    static const int kApBase[CHANNEL_REVERB_APS] = {225, 556};

    int offset = 0;
    for (int i = 0; i < CHANNEL_REVERB_COMBS; i++) {
        int len = (kCombBase[i] * (int)reverbSize_) / 8;
        if (len < 2) {
            len = 2;
        }
        combOff_[i] = offset;
        combLen_[i] = len;
        combPos_[i] = 0;
        combDamp_[i][0] = combDamp_[i][1] = i2fp(0);
        offset += len * 2;
    }
    for (int i = 0; i < CHANNEL_REVERB_APS; i++) {
        int len = (kApBase[i] * (int)reverbSize_) / 8;
        if (len < 2) {
            len = 2;
        }
        apOff_[i] = offset;
        apLen_[i] = len;
        apPos_[i] = 0;
        offset += len * 2;
    }
    if (offset > CHANNEL_REVERB_BUF_FRAMES) {
        reverbSize_ = 0;
        return;
    }
    reverbFb_ = fl2fp(0.58f + reverbSize_ * 0.023f);
    memset(reverbBuf_, 0, sizeof(fixed) * CHANNEL_REVERB_BUF_FRAMES);
}

void PlayerChannel::SetReverb(unsigned char size, unsigned char amt) {
    if (size > CHANNEL_REVERB_MAX_SIZE) {
        size = CHANNEL_REVERB_MAX_SIZE;
    }
    bool sizeChanged = (size != reverbSize_);
    reverbSize_ = size;
    reverbAmt_ = amt;
    if (size == 0) {
        return;
    }
    if (sizeChanged) {
        syncReverb();
    }
}

bool PlayerChannel::applyReverb(fixed *buffer, int samplecount) {
    if (reverbSize_ == 0 || !reverbBuf_) {
        return false;
    }

    fixed wet = fl2fp(reverbAmt_ / 255.0f);
    fixed dry = fp_sub(i2fp(1), wet);
    fixed apG = fl2fp(0.5f);
    // Warm one-pole damping in comb feedback loop (like GrainBrick reverb.damp)
    fixed dampA = fl2fp(0.72f);
    fixed dampB = fl2fp(0.28f);
    fixed xfeed = fl2fp(0.16f);
    bool any = false;

    for (int n = 0; n < samplecount; n++) {
        int idx = n * 2;
        fixed inL = buffer[idx];
        fixed inR = buffer[idx + 1];
        fixed sendL = fp_add(inL, fp_mul(xfeed, inR));
        fixed sendR = fp_add(inR, fp_mul(xfeed, inL));

        fixed sumL = i2fp(0);
        fixed sumR = i2fp(0);
        for (int c = 0; c < CHANNEL_REVERB_COMBS; c++) {
            int pos = combPos_[c];
            int bi = combOff_[c] + pos * 2;
            fixed dL = reverbBuf_[bi];
            fixed dR = reverbBuf_[bi + 1];
            combDamp_[c][0] =
                fp_add(fp_mul(dL, dampA), fp_mul(combDamp_[c][0], dampB));
            combDamp_[c][1] =
                fp_add(fp_mul(dR, dampA), fp_mul(combDamp_[c][1], dampB));
            reverbBuf_[bi] =
                fp_add(sendL, fp_mul(reverbFb_, combDamp_[c][0]));
            reverbBuf_[bi + 1] =
                fp_add(sendR, fp_mul(reverbFb_, combDamp_[c][1]));
            sumL = fp_add(sumL, dL);
            sumR = fp_add(sumR, dR);
            pos++;
            if (pos >= combLen_[c]) {
                pos = 0;
            }
            combPos_[c] = pos;
        }

        sumL = fp_mul(sumL, fl2fp(0.25f));
        sumR = fp_mul(sumR, fl2fp(0.25f));

        for (int a = 0; a < CHANNEL_REVERB_APS; a++) {
            int pos = apPos_[a];
            int bi = apOff_[a] + pos * 2;
            fixed bufL = reverbBuf_[bi];
            fixed bufR = reverbBuf_[bi + 1];
            fixed outL = fp_add(fp_mul(fp_sub(i2fp(0), apG), sumL), bufL);
            fixed outR = fp_add(fp_mul(fp_sub(i2fp(0), apG), sumR), bufR);
            reverbBuf_[bi] = fp_add(sumL, fp_mul(apG, outL));
            reverbBuf_[bi + 1] = fp_add(sumR, fp_mul(apG, outR));
            sumL = outL;
            sumR = outR;
            pos++;
            if (pos >= apLen_[a]) {
                pos = 0;
            }
            apPos_[a] = pos;
        }

        fixed outL = fp_add(fp_mul(dry, inL), fp_mul(wet, sumL));
        fixed outR = fp_add(fp_mul(dry, inR), fp_mul(wet, sumR));
        buffer[idx] = outL;
        buffer[idx + 1] = outR;

        if (outL != 0 || outR != 0 || inL != 0 || inR != 0 || sumL != 0 ||
            sumR != 0) {
            any = true;
        }
    }
    return any;
}

void PlayerChannel::ensureTapeBuffer() {
    if (tapeBuf_) {
        return;
    }
    tapeBuf_ = new float[CHANNEL_TAPE_MAX_FRAMES * 2];
    memset(tapeBuf_, 0, sizeof(float) * CHANNEL_TAPE_MAX_FRAMES * 2);
    tapeWritePos_ = 0;
}

void PlayerChannel::SetTape(unsigned char amt) {
    tapeAmt_ = amt;
    if (amt > 0) {
        ensureTapeBuffer();
    }
}

bool PlayerChannel::applyTape(fixed *buffer, int samplecount) {
    if (tapeAmt_ == 0 || !tapeBuf_ || samplecount <= 0) {
        return false;
    }

    float t = tapeAmt_ / 255.0f;
    // Block-rate random drift updates (modeled on libgrainbrick_tape.so)
    unsigned int r = tapeRng_;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    float rand1 = ((int)(r & 0xFFFF) - 32768) / 32768.0f;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    float rand2 = ((int)(r & 0xFFFF) - 32768) / 32768.0f;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    tapeRng_ = r;

    tapeWowDrift_ = tapeWowDrift_ * 0.96f + rand1 * 0.04f;
    tapeFlutDrift_ = tapeFlutDrift_ * 0.82f + rand2 * 0.18f;

    // Occasional crinkle & oxide dropout triggers at higher tape amounts
    if (t > 0.25f) {
        if (tapeCrinkleEnv_ <= 0.001f && ((r & 0xFF) < (unsigned int)(t * 10.0f))) {
            tapeCrinkleEnv_ = 1.0f;
        }
        if (tapeDropoutEnv_ <= 0.001f && (((r >> 8) & 0x1FF) < (unsigned int)(t * 6.0f))) {
            tapeDropoutEnv_ = 1.0f;
        }
    }

    // Saturation coefficients from gb_sat_coeffs / gb_process_saturation
    float satAmt = t * 0.85f;
    float drive = 1.0f + satAmt * 2.65f;
    float bias = satAmt * 0.085f;
    float biasNorm = gbPadeTanh(drive * bias);
    float normDenom = (float)fabs(gbPadeTanh(drive));
    if (normDenom < 0.001f)
        normDenom = 1.0f;
    float makeup = (1.0f / normDenom) * (1.0f / (1.0f + 0.28f * satAmt));

    // Tape head-bump & high-frequency roll-off coefficient
    float lpAlpha = 0.86f - t * 0.54f;
    if (lpAlpha < 0.18f)
        lpAlpha = 0.18f;

    float wowDepth = (0.15f + t * 0.85f) * 48.0f;   // up to ~1.1 ms delay sweep
    float flutDepth = (0.15f + t * 0.85f) * 9.5f;   // fast flutter micro-sweep
    float wowInc = 0.62f / 44100.0f;
    float flutInc = 8.4f / 44100.0f;
    float hissGain = t * t * 0.0045f;

    const float inv32768 = 1.0f / 32768.0f;
    bool any = false;

    for (int n = 0; n < samplecount; n++) {
        int idx = n * 2;
        float inL = fp2fl(buffer[idx]) * inv32768;
        float inR = fp2fl(buffer[idx + 1]) * inv32768;

        int wp = tapeWritePos_;
        tapeBuf_[wp * 2] = inL;
        tapeBuf_[wp * 2 + 1] = inR;

        tapeWowPhase_ += wowInc;
        if (tapeWowPhase_ >= 1.0f)
            tapeWowPhase_ -= 1.0f;
        tapeFlutPhase_ += flutInc;
        if (tapeFlutPhase_ >= 1.0f)
            tapeFlutPhase_ -= 1.0f;

        float wow =
            ((float)sin(tapeWowPhase_ * 2.0 * M_PI) * 0.72f + tapeWowDrift_ * 0.28f) *
            wowDepth;
        float flut =
            ((float)sin(tapeFlutPhase_ * 2.0 * M_PI) * 0.65f + tapeFlutDrift_ * 0.35f) *
            flutDepth;

        float crinkle = 0.0f;
        if (tapeCrinkleEnv_ > 0.0f) {
            crinkle = (float)sin(tapeCrinkleEnv_ * M_PI) * 14.0f * t;
            tapeCrinkleEnv_ -= 1.0f / 1800.0f;
            if (tapeCrinkleEnv_ < 0.0f)
                tapeCrinkleEnv_ = 0.0f;
        }

        float dropGain = 1.0f;
        if (tapeDropoutEnv_ > 0.0f) {
            float d = (float)sin(tapeDropoutEnv_ * M_PI);
            dropGain = 1.0f - (0.35f * t) * d * d;
            tapeDropoutEnv_ -= 1.0f / 4200.0f;
            if (tapeDropoutEnv_ < 0.0f)
                tapeDropoutEnv_ = 0.0f;
        }

        float dlyL = 64.0f + wow + flut + crinkle;
        float dlyR = 64.0f + wow * 0.97f - flut * 0.85f + crinkle * 0.9f + t * 3.5f;
        if (dlyL < 2.0f)
            dlyL = 2.0f;
        if (dlyL > (float)(CHANNEL_TAPE_MAX_FRAMES - 4))
            dlyL = (float)(CHANNEL_TAPE_MAX_FRAMES - 4);
        if (dlyR < 2.0f)
            dlyR = 2.0f;
        if (dlyR > (float)(CHANNEL_TAPE_MAX_FRAMES - 4))
            dlyR = (float)(CHANNEL_TAPE_MAX_FRAMES - 4);

        float readPosL = (float)wp - dlyL;
        if (readPosL < 0.0f)
            readPosL += (float)CHANNEL_TAPE_MAX_FRAMES;
        int i0L = (int)readPosL;
        float fracL = readPosL - (float)i0L;
        int i1L = (i0L + 1) & (CHANNEL_TAPE_MAX_FRAMES - 1);
        i0L &= (CHANNEL_TAPE_MAX_FRAMES - 1);
        float sL = tapeBuf_[i0L * 2] * (1.0f - fracL) + tapeBuf_[i1L * 2] * fracL;

        float readPosR = (float)wp - dlyR;
        if (readPosR < 0.0f)
            readPosR += (float)CHANNEL_TAPE_MAX_FRAMES;
        int i0R = (int)readPosR;
        float fracR = readPosR - (float)i0R;
        int i1R = (i0R + 1) & (CHANNEL_TAPE_MAX_FRAMES - 1);
        i0R &= (CHANNEL_TAPE_MAX_FRAMES - 1);
        float sR =
            tapeBuf_[i0R * 2 + 1] * (1.0f - fracR) + tapeBuf_[i1R * 2 + 1] * fracR;

        tapeWritePos_ = (wp + 1) & (CHANNEL_TAPE_MAX_FRAMES - 1);

        // Asymmetric Padé tanh tape saturation + DC blocker
        float satL = (gbPadeTanh((sL + bias) * drive) - biasNorm) * makeup;
        float satR = (gbPadeTanh((sR + bias) * drive) - biasNorm) * makeup;
        sL = sL * (1.0f - satAmt) + satL * satAmt;
        sR = sR * (1.0f - satAmt) + satR * satAmt;

        float dcL = sL - tapeDcIn_[0] + 0.995f * tapeDcOut_[0];
        tapeDcIn_[0] = sL;
        tapeDcOut_[0] = dcL;
        sL = dcL;

        float dcR = sR - tapeDcIn_[1] + 0.995f * tapeDcOut_[1];
        tapeDcIn_[1] = sR;
        tapeDcOut_[1] = dcR;
        sR = dcR;

        // Warm tape HF roll-off + oxide dropout ducking
        tapeLpState_[0] += lpAlpha * (sL - tapeLpState_[0]);
        tapeLpState_[1] += lpAlpha * (sR - tapeLpState_[1]);
        sL = tapeLpState_[0] * dropGain;
        sR = tapeLpState_[1] * dropGain;

        // Subtle pink/tape hiss when audible signal or tape drive is active
        if (hissGain > 0.00001f && (inL != 0.0f || inR != 0.0f || sL != 0.0f || sR != 0.0f)) {
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            float wnL = ((int)(r & 0xFFFF) - 32768) / 32768.0f;
            float wnR = ((int)((r >> 16) & 0xFFFF) - 32768) / 32768.0f;
            tapeHissLp_[0] += 0.22f * (wnL - tapeHissLp_[0]);
            tapeHissLp_[1] += 0.22f * (wnR - tapeHissLp_[1]);
            sL += (wnL * 0.35f + tapeHissLp_[0] * 0.65f) * hissGain;
            sR += (wnR * 0.35f + tapeHissLp_[1] * 0.65f) * hissGain;
        }

        if (sL > 1.25f)
            sL = 1.25f;
        if (sL < -1.25f)
            sL = -1.25f;
        if (sR > 1.25f)
            sR = 1.25f;
        if (sR < -1.25f)
            sR = -1.25f;

        fixed outL = fl2fp(sL * 32768.0f);
        fixed outR = fl2fp(sR * 32768.0f);
        buffer[idx] = outL;
        buffer[idx + 1] = outR;

        if (outL != 0 || outR != 0 || inL != 0.0f || inR != 0.0f) {
            any = true;
        }
    }
    tapeRng_ = r;
    return any;
}

bool PlayerChannel::Render(fixed *buffer, int samplecount) {
    if (muted_) {
        if (instr_) {
            bool tableSlice = SyncMaster::GetInstance()->TableSlice();
            instr_->Render(index_, buffer, samplecount, tableSlice | 2);
        }
        return false;
    }
    bool gotInstr = false;
    if (instr_) {
        bool tableSlice = SyncMaster::GetInstance()->TableSlice();
        gotInstr = instr_->Render(index_, buffer, samplecount,
                                  tableSlice | (muted_ << 1));
        if (muted_) {
            gotInstr = false;
        }
    }

    if (gotInstr) {
        if (hpfMode_ != 0) {
            for (int n = 0; n < samplecount; n++) {
                int idx = n * 2;
                fixed in_l = buffer[idx];
                fixed in_r = buffer[idx + 1];
                fixed out_l =
                    fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[0],
                                             fp_sub(in_l, hpfPrevInput_[0])));
                fixed out_r =
                    fp_mul(hpfAlpha_, fp_add(hpfPrevOutput_[1],
                                             fp_sub(in_r, hpfPrevInput_[1])));
                buffer[idx] = out_l;
                buffer[idx + 1] = out_r;
                hpfPrevInput_[0] = in_l;
                hpfPrevInput_[1] = in_r;
                hpfPrevOutput_[0] = out_l;
                hpfPrevOutput_[1] = out_r;
            }
        }

        if (lpfFreq_ != 0) {
            fixed one_minus_alpha = fp_sub(i2fp(1), lpfAlpha_);
            for (int n = 0; n < samplecount; n++) {
                int idx = n * 2;
                fixed in_l = buffer[idx];
                fixed in_r = buffer[idx + 1];
                fixed out_l =
                    fp_add(fp_mul(lpfAlpha_, in_l),
                           fp_mul(one_minus_alpha, lpfPrevOutput_[0]));
                fixed out_r =
                    fp_add(fp_mul(lpfAlpha_, in_r),
                           fp_mul(one_minus_alpha, lpfPrevOutput_[1]));
                buffer[idx] = out_l;
                buffer[idx + 1] = out_r;
                lpfPrevOutput_[0] = out_l;
                lpfPrevOutput_[1] = out_r;
            }
        }
    } else if (delaySteps_ != 0 || reverbSize_ != 0 || tapeAmt_ != 0) {
        for (int i = 0; i < samplecount * 2; i++) {
            buffer[i] = i2fp(0);
        }
    } else {
        return false;
    }

    bool gotFx = false;
    if (delaySteps_ != 0) {
        gotFx = applyDelay(buffer, samplecount) || gotFx;
    }
    if (reverbSize_ != 0) {
        gotFx = applyReverb(buffer, samplecount) || gotFx;
    }
    if (tapeAmt_ != 0 && (gotInstr || gotFx)) {
        gotFx = applyTape(buffer, samplecount) || gotFx;
    }

    if (!gotInstr && !gotFx) {
        return false;
    }

    if (volume_ != i2fp(1)) {
        for (int i = 0; i < samplecount * 2; i++) {
            buffer[i] = fp_mul(buffer[i], volume_);
        }
    }
    return true;
}

I_Instrument *PlayerChannel::GetInstrument() { return instr_; }

void PlayerChannel::SetMute(bool muted) { muted_ = muted; }

bool PlayerChannel::IsMuted() { return muted_; }

void PlayerChannel::SetVolume(fixed volume) { volume_ = volume; }

void PlayerChannel::SetHPFMode(unsigned char mode) {
    if (hpfMode_ == mode)
        return;
    hpfMode_ = mode;
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    if (hpfMode_ == 0) {
        hpfAlpha_ = i2fp(0);
        return;
    }
    float fc = (hpfMode_ == 1) ? 20.0f : 90.0f;
    float fs = 44100.0f;
    const float PI = 3.14159265358979323846f;
    float RC = 1.0f / (2.0f * PI * fc);
    float dt = 1.0f / fs;
    float alpha = RC / (RC + dt);
    hpfAlpha_ = fl2fp(alpha);
}

void PlayerChannel::SetLPFFreq(unsigned short freq) {
    if (lpfFreq_ == freq)
        return;
    lpfFreq_ = freq;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    if (lpfFreq_ == 0) {
        lpfAlpha_ = i2fp(0);
        return;
    }
    float fc = (float)lpfFreq_;
    float fs = 44100.0f;
    const float PI = 3.14159265358979323846f;
    float RC = 1.0f / (2.0f * PI * fc);
    float dt = 1.0f / fs;
    float alpha = dt / (RC + dt);
    lpfAlpha_ = fl2fp(alpha);
}

void PlayerChannel::SetMixBus(int i) {
    if (i == busIndex_)
        return;

    if (mixBus_) {
        mixBus_->Remove(*this);
    }
    busIndex_ = i;
    mixBus_ = MixerService::GetInstance()->GetMixBus(i);
    if (mixBus_) {
        mixBus_->Insert(*this);
    }
}

void PlayerChannel::Reset() {
    if (mixBus_) {
        mixBus_->Remove(*this);
    }
    muted_ = false;
    busIndex_ = -1;
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    hpfAlpha_ = i2fp(0);
    hpfMode_ = 0;
    lpfPrevOutput_[0] = lpfPrevOutput_[1] = i2fp(0);
    lpfAlpha_ = i2fp(0);
    lpfFreq_ = 0;
    delaySteps_ = 0;
    delayAmt_ = 0;
    delayLen_ = 0;
    delayWritePos_ = 0;
    if (delayBuf_) {
        memset(delayBuf_, 0, sizeof(fixed) * CHANNEL_DELAY_MAX_FRAMES * 2);
    }
    reverbSize_ = 0;
    reverbAmt_ = 0;
    reverbFb_ = i2fp(0);
    if (reverbBuf_) {
        memset(reverbBuf_, 0, sizeof(fixed) * CHANNEL_REVERB_BUF_FRAMES);
    }
    tapeAmt_ = 0;
    tapeWritePos_ = 0;
    tapeLpState_[0] = tapeLpState_[1] = 0.0f;
    tapeHissLp_[0] = tapeHissLp_[1] = 0.0f;
    tapeDcIn_[0] = tapeDcIn_[1] = 0.0f;
    tapeDcOut_[0] = tapeDcOut_[1] = 0.0f;
    if (tapeBuf_) {
        memset(tapeBuf_, 0, sizeof(float) * CHANNEL_TAPE_MAX_FRAMES * 2);
    }
}
