#ifndef _AUDIO_FILE_STREAMER_H_
#define _AUDIO_FILE_STREAMER_H_

#include "Services/Audio/AudioModule.h"
#include "System/FileSystem/FileSystem.h"
#include "Application/Instruments/WavFile.h"

enum AudioFileStreamerMode {
	AFSM_STOPPED,
	AFSM_PLAYING
} ;

class AudioFileStreamer: public AudioModule {
public:
	AudioFileStreamer() ;
	virtual ~AudioFileStreamer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	bool Start(const Path &) ;
	void Stop() ;
protected:
	AudioFileStreamerMode mode_ ;
	Path path_ ;
	WavFile *wav_ ;
	// Fractional read head in source frames (supports rate conversion).
	float position_ ;
	float step_ ; // src frames advanced per output frame
	int shift_ ;
	int channelCount_ ;
	int size_ ; // source frames
	short *pcm_ ; // borrowed from wav_ while playing; not owned
} ;

#endif
