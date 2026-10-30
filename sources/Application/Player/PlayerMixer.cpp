#include "PlayerMixer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Application/Utils/fixed.h"
#include "Services/Midi/MidiService.h"
#include "SyncMaster.h"
#include "TablePlayback.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdlib.h>

PlayerMixer::PlayerMixer() {

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        lastInstrument_[i] = 0;
		channel_[i] = new PlayerChannel(i);
		isChannelPlaying_[i] = false;
    }
    masterChannel_ = new PlayerChannel(MASTER_CHANNEL_INDEX);
}

bool PlayerMixer::Init(Project *project) {

	MixerService *ms=MixerService::GetInstance() ;
	if (!ms->Init()) {
			return false ;
	}

	AudioMixer *mixer=ms->GetMixBus(STREAM_MIX_BUS) ;
	mixer->Insert(fileStreamer_) ;

    masterChannel_->SetSourceModule(ms->GetBusMix());
    masterChannel_->SetMixBusPtr(ms->GetMasterBus());

	project_=project ;

	// Init states

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
	}
    if (masterChannel_) {
        masterChannel_->Reset();
    }


	MixerService *ms=MixerService::GetInstance() ;
	ms->Close() ;

}

bool PlayerMixer::Start() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->AddObserver(*this) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        notes_[i]=0xFF ;
    } ;

	return ms->Start() ;
} ;

void PlayerMixer::Stop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Stop() ;
	ms->RemoveObserver(*this) ;
} ;

void PlayerMixer::StartChannel(int channel) {
	isChannelPlaying_[channel]=true ;
} ;

void PlayerMixer::StopChannel(int channel) {

    StopInstrument(channel) ;
	isChannelPlaying_[channel]=false ;
} ;


bool PlayerMixer::IsChannelPlaying(int channel) {
	return isChannelPlaying_[channel] ;
} ;

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
	return lastInstrument_[channel] ;
} ;


bool PlayerMixer::Clipped() {
     return clipped_ ;
}

void PlayerMixer::Update(Observable &o,I_ObservableData *d) {

  // Notifies the player so that pattern data is processed
  SetChanged();
  NotifyObservers();

  // Transfer the mixer data
  Mixer *mixer = Mixer::GetInstance();

  for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
      channel_[i]->SetMixBus(mixer->GetBus(i));
      channel_[i]->SetVolume(fl2fp(mixer->GetChannelVolume(i) / 255.0f));
      channel_[i]->SetHPFMode((unsigned char)mixer->GetChannelHPF(i));
      channel_[i]->SetLPFFreq(mixer->GetChannelLPF(i));

      unsigned char dlySteps = mixer->GetChannelDelaySteps(i);
      unsigned char dlyAmt = mixer->GetChannelDelayAmt(i);
      unsigned char revSize = mixer->GetChannelReverbSize(i);
      unsigned char revAmt = mixer->GetChannelReverbAmt(i);
      unsigned char tapeAmt = mixer->GetChannelTapeAmt(i);

      I_Instrument *activeInstr = channel_[i]->GetInstrument();
      if (!activeInstr) {
          activeInstr = lastInstrument_[i];
      }
      if (activeInstr) {
          unsigned char instDel = activeInstr->GetDelaySend();
          if (instDel > 0) {
              if (dlySteps == 0) {
                  dlySteps = 3;
                  dlyAmt = instDel;
              } else {
                  int sum = (int)dlyAmt + (int)instDel;
                  dlyAmt = (unsigned char)(sum > 255 ? 255 : sum);
              }
          }
          unsigned char instRev = activeInstr->GetReverbSend();
          if (instRev > 0) {
              if (revSize == 0) {
                  revSize = 6;
                  revAmt = instRev;
              } else {
                  int sum = (int)revAmt + (int)instRev;
                  revAmt = (unsigned char)(sum > 255 ? 255 : sum);
              }
          }
          unsigned char instTape = activeInstr->GetTapeSend();
          if (instTape > 0) {
              int sum = (int)tapeAmt + (int)instTape;
              tapeAmt = (unsigned char)(sum > 255 ? 255 : sum);
          }
      }

      channel_[i]->SetDelay(dlySteps, dlyAmt);
      channel_[i]->SetReverb(revSize, revAmt);
      channel_[i]->SetTape(tapeAmt);
      channel_[i]->SetSidechain(mixer->GetChannelSidechainSrc(i),
                                mixer->GetChannelSidechainAmt(i));
  }
  if (masterChannel_) {
      masterChannel_->SetVolume(
          fl2fp(mixer->GetChannelVolume(MASTER_CHANNEL_INDEX) / 255.0f));
      masterChannel_->SetHPFMode(
          (unsigned char)mixer->GetChannelHPF(MASTER_CHANNEL_INDEX));
      masterChannel_->SetLPFFreq(mixer->GetChannelLPF(MASTER_CHANNEL_INDEX));
      masterChannel_->SetDelay(
          mixer->GetChannelDelaySteps(MASTER_CHANNEL_INDEX),
          mixer->GetChannelDelayAmt(MASTER_CHANNEL_INDEX));
      masterChannel_->SetReverb(
          mixer->GetChannelReverbSize(MASTER_CHANNEL_INDEX),
          mixer->GetChannelReverbAmt(MASTER_CHANNEL_INDEX));
      masterChannel_->SetTape(mixer->GetChannelTapeAmt(MASTER_CHANNEL_INDEX));
      masterChannel_->SetSidechain(
          mixer->GetChannelSidechainSrc(MASTER_CHANNEL_INDEX),
          mixer->GetChannelSidechainAmt(MASTER_CHANNEL_INDEX));
  }
  MixerService *ms=MixerService::GetInstance();
  ms->SetPregain(project_->GetPregain());
  ms->SetSoftclip(project_->GetSoftclip(), project_->GetSoftclipGain());
  ms->SetMasterVolume(project_->GetMasterVolume());
  clipped_=ms->Clipped();
} ;


void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
	channel_[channel]->StartInstrument(instrument,note,newInstrument) ;
	lastInstrument_[channel]=instrument ;
	notes_[channel]=note ;

} ;

void PlayerMixer::StopInstrument(int channel) {
    channel_[channel]->StopInstrument() ;
    notes_[channel]=0xFF ;
}

void PlayerMixer::DetachInstrumentLocked(I_Instrument *instrument) {
	if (!instrument) {
		return;
	}
	for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
		if (channel_[i]->GetInstrument() == instrument) {
			StopInstrument(i);
		}
		if (lastInstrument_[i] == instrument) {
			lastInstrument_[i] = 0;
		}
		TablePlayback &tp = TablePlayback::GetTablePlayback(i);
		if (tp.GetInstrument() == instrument) {
			tp.Stop();
		}
	}
}

void PlayerMixer::DetachInstrument(I_Instrument *instrument) {
	if (!instrument) {
		return;
	}
	Lock();
	DetachInstrumentLocked(instrument);
	Unlock();
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
    return channel_[channel]->GetInstrument();
}

int PlayerMixer::GetPlayedBufferPercentage() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetPlayedBufferPercentage() ;
};

void PlayerMixer::SetChannelMute(int channel,bool mode) {
     if (channel == MASTER_CHANNEL_INDEX && masterChannel_) {
         masterChannel_->SetMute(mode);
         return;
     }
     if (channel >= 0 && channel < SONG_CHANNEL_COUNT) {
         channel_[channel]->SetMute(mode) ;
     }
}

bool PlayerMixer::IsChannelMuted(int channel) {
     if (channel == MASTER_CHANNEL_INDEX && masterChannel_) {
         return masterChannel_->IsMuted();
     }
     if (channel >= 0 && channel < SONG_CHANNEL_COUNT) {
         return channel_[channel]->IsMuted() ;
     }
     return false;
}

float PlayerMixer::GetChannelSidechainEnv(int channel) const {
     if (channel >= 0 && channel < SONG_CHANNEL_COUNT && channel_[channel]) {
         return channel_[channel]->GetSidechainEnv();
     }
     return 0.0f;
}

void PlayerMixer::StartStreaming(const Path &path) {
	fileStreamer_.Start(path) ;
} ;

void PlayerMixer::StopStreaming() {
	fileStreamer_.Stop() ;
} ;

void PlayerMixer::OnPlayerStart() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStart();
}

void PlayerMixer::OnPlayerStop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStop();
}

static char noteBuffer[5] ;

int PlayerMixer::GetChannelNote(int channel) {
	return notes_[channel] ;
}

char *PlayerMixer::GetPlayedNote(int channel) {

    if (notes_[channel]!=0xFF) {
		note2visualizer(notes_[channel],noteBuffer) ; 
		return noteBuffer ;
    }
    return "  " ;
} ;

char *PlayerMixer::GetPlayedOctive(int channel) {
    if (notes_[channel]!=0xFF) {
		if (!IsChannelMuted(channel)) {
	        oct2visualizer(notes_[channel],noteBuffer) ; 
	        return noteBuffer ;
		} else {
			return "--" ;
		}
    }
    return "  " ;
} ;

AudioOut *PlayerMixer::GetAudioOut() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetAudioOut();
} ;

void PlayerMixer::Lock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
} ;

void PlayerMixer::Unlock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Unlock() ;
};
