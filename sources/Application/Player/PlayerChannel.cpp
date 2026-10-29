
#include "PlayerChannel.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/fixed.h"
#include <math.h>
#include <string.h>

PlayerChannel::PlayerChannel(int index) {
    index_=index ;
    instr_=0 ;
    muted_=false ;
	mixBus_=0 ;
	busIndex_=-1 ;
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
    }
    for (int i = 0; i < CHANNEL_REVERB_APS; i++) {
        apOff_[i] = apLen_[i] = apPos_[i] = 0;
    }
}

PlayerChannel::~PlayerChannel() {
    delete[] delayBuf_;
    delayBuf_ = 0;
    delete[] reverbBuf_;
    reverbBuf_ = 0;
}

void PlayerChannel::StartInstrument(I_Instrument *instr,unsigned char note,bool trigger) {
   if (instr_) {
      StopInstrument() ;
   }
   /* The note will not trigger if it is out the keymap or, in the case of a
      MIDI instrument, if the channel is muted. */
   if (instr->Start(index_,note, trigger | (muted_ << 1))) {
	   instr_=instr ;
   } else {
	   instr_=0 ;
   };
} ;

void PlayerChannel::StopInstrument() {
     if (instr_) {
       instr_->Stop(index_) ;
     }
     instr_=0 ;
} ;

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
        // Tempo may have changed; refresh length without always clearing.
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
    // Feedback tracks mix but stays slightly under 1 to avoid runaway.
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

    // Freeverb-ish base lengths at size=8; scale linearly with size.
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
        // Should not happen with the chosen bases; disable rather than overrun.
        reverbSize_ = 0;
        return;
    }
    // Longer room → higher feedback (capped under 1).
    reverbFb_ = fl2fp(0.55f + reverbSize_ * 0.025f);
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
    bool any = false;

    for (int n = 0; n < samplecount; n++) {
        int idx = n * 2;
        fixed inL = buffer[idx];
        fixed inR = buffer[idx + 1];

        fixed sumL = i2fp(0);
        fixed sumR = i2fp(0);
        for (int c = 0; c < CHANNEL_REVERB_COMBS; c++) {
            int pos = combPos_[c];
            int bi = combOff_[c] + pos * 2;
            fixed dL = reverbBuf_[bi];
            fixed dR = reverbBuf_[bi + 1];
            reverbBuf_[bi] = fp_add(inL, fp_mul(reverbFb_, dL));
            reverbBuf_[bi + 1] = fp_add(inR, fp_mul(reverbFb_, dR));
            sumL = fp_add(sumL, dL);
            sumR = fp_add(sumR, dR);
            pos++;
            if (pos >= combLen_[c]) {
                pos = 0;
            }
            combPos_[c] = pos;
        }

        // Average combs then run through allpasses
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

bool PlayerChannel::Render(fixed *buffer,int samplecount) {
   bool gotInstr = false;
   if (instr_) {
     bool tableSlice=SyncMaster::GetInstance()->TableSlice() ;
     gotInstr = instr_->Render(index_, buffer, samplecount,
                               tableSlice | (muted_ << 1));
     if (muted_) {
         gotInstr = false;
     }
   }

   if (gotInstr) {
         // Apply HPF if enabled
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

         // Apply LPF if enabled
         if (lpfFreq_ != 0) {
             fixed one_minus_alpha = fp_sub(i2fp(1), lpfAlpha_);
             for (int n = 0; n < samplecount; n++) {
                 int idx = n * 2;
                 fixed in_l = buffer[idx];
                 fixed in_r = buffer[idx + 1];
                 fixed out_l = fp_add(fp_mul(lpfAlpha_, in_l),
                                      fp_mul(one_minus_alpha, lpfPrevOutput_[0]));
                 fixed out_r = fp_add(fp_mul(lpfAlpha_, in_r),
                                      fp_mul(one_minus_alpha, lpfPrevOutput_[1]));
                 buffer[idx] = out_l;
                 buffer[idx + 1] = out_r;
                 lpfPrevOutput_[0] = out_l;
                 lpfPrevOutput_[1] = out_r;
             }
         }
   } else if (delaySteps_ != 0 || reverbSize_ != 0) {
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

   if (!gotInstr && !gotFx) {
       return false;
   }

   // Apply per-channel volume
   if (volume_ != i2fp(1)) {
       for (int i = 0; i < samplecount * 2; i++) {
           buffer[i] = fp_mul(buffer[i], volume_);
       }
   }
   return true;
} ;

I_Instrument *PlayerChannel::GetInstrument() {
   return instr_ ;
} ;

void PlayerChannel::SetMute(bool muted) {
     muted_=muted ;
}

bool PlayerChannel::IsMuted() { return muted_; }

void PlayerChannel::SetVolume(fixed volume) { volume_ = volume; };

void PlayerChannel::SetHPFMode(unsigned char mode) {
    if (hpfMode_ == mode)
        return;
    hpfMode_ = mode;
    // reset state
    hpfPrevInput_[0] = hpfPrevInput_[1] = i2fp(0);
    hpfPrevOutput_[0] = hpfPrevOutput_[1] = i2fp(0);
    if (hpfMode_ == 0) {
        hpfAlpha_ = i2fp(0);
        return;
    }
    // compute alpha for one-pole HPF: alpha = RC/(RC+dt), RC=1/(2*pi*fc),
    // dt=1/fs
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
    // compute alpha for one-pole LPF: alpha = dt/(RC+dt), RC=1/(2*pi*fc),
    // dt=1/fs
    float fc = (float)lpfFreq_;
    float fs = 44100.0f;
    const float PI = 3.14159265358979323846f;
    float RC = 1.0f / (2.0f * PI * fc);
    float dt = 1.0f / fs;
    float alpha = dt / (RC + dt);
    lpfAlpha_ = fl2fp(alpha);
}

void PlayerChannel::SetMixBus(int i) {

	if (i==busIndex_) return ;

	if (mixBus_) {
		mixBus_->Remove(*this) ;
	}
    busIndex_ = i;
    mixBus_=MixerService::GetInstance()->GetMixBus(i) ;
	if (mixBus_) {
		mixBus_->Insert(*this) ;
	}
} ;

void PlayerChannel::Reset() {
    if (mixBus_) {
        mixBus_->Remove(*this) ;
    }
    muted_=false ;
  busIndex_=-1 ;
  hpfPrevInput_[0]=hpfPrevInput_[1]=i2fp(0);
  hpfPrevOutput_[0]=hpfPrevOutput_[1]=i2fp(0);
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
};
