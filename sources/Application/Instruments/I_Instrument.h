#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Observable.h"
#include "Application/Utils/fixed.h"

#include "Application/Player/TablePlayback.h"

enum InstrumentType {
	IT_SAMPLE=0,
	IT_MIDI,
	IT_SYNTH,
	IT_LAST
} ;

#define MAX_INSTRUMENT_NAME_LENGTH 12

class I_Instrument:public VariableContainer, public Observable {
      
public:
	I_Instrument() {
		userName_[0] = '\0';
	} ;
	virtual ~I_Instrument() {} ;

	  // Initialisation routine

	  virtual bool Init()=0 ;

      // Start & stop the instument
      // flags & 1 indicates retriggers (default)
      // flags & 2 indicates muting (i.e. no trigger)
      virtual bool Start(int channel,unsigned char note,int flags=1)=0 ;
      virtual void Stop(int channel)=0 ;

	  // Engine playback  start callback

	  virtual void OnStart()=0 ;

      // size refers to the number of samples
      // should always fill interleaved stereo / 16bit

  // flags & 1 indicates 'updateTick'.
  // flags & 2 indicated muting (i.e. stop notes)
      virtual bool Render(int channel,fixed *buffer,int size,int flags)=0 ;

      virtual bool IsInitialized()=0 ;

	  virtual bool IsEmpty()=0 ;

	  virtual InstrumentType GetType()=0 ;

	  virtual const char *GetName()=0 ; 

	  const char *GetUserName() const { return userName_; }
	  void SetUserName(const char *name) {
		  if (!name) {
			  userName_[0] = '\0';
			  return;
		  }
		  int i = 0;
		  while (name[i] && i < MAX_INSTRUMENT_NAME_LENGTH) {
			  userName_[i] = name[i];
			  i++;
		  }
		  while (i > 0 && userName_[i - 1] == ' ') {
			  i--;
		  }
		  userName_[i] = '\0';
	  }

	  virtual unsigned char GetDelaySend() { return 0; }
	  virtual unsigned char GetReverbSend() { return 0; }
	  virtual unsigned char GetTapeSend() { return 0; }
	 
	  virtual void ProcessCommand(int channel,FourCC cc,ushort value)=0 ;

	  virtual void Purge()=0 ;

	  virtual int GetTable()=0 ;
	  virtual bool GetTableAutomation()=0 ;

	  virtual void GetTableState(TableSaveState &state)=0 ;	 
	  virtual void SetTableState(TableSaveState &state)=0 ;	 

protected:
	  char userName_[MAX_INSTRUMENT_NAME_LENGTH + 1];
};
#endif
