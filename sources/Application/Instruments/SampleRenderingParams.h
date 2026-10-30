
#ifndef _SAMPLE_RENDER_PARAMS_H_
#define _SAMPLE_RENDER_PARAMS_H_

#include "Foundation/Types/Types.h"
#include "SRPUpdaters.h"
#include <vector>

enum FeedbackMode {
	FB_NONE,
	FB_ADD,
	FB_SUB
} ;

struct renderParams {

	void *sampleBuffer_ ; // wavdata
	int channelCount_ ;
	int sampleSize_ ; // PCM frames in sampleBuffer_

	int krateCount_ ;   // K-rate counter
    float position_;    // Position in the sample stream
    int rendFirst_;     // position of the first sample (can be either start or loop depending on the mode)
    int rendLoopStart_ ;// Loop start position
    int rendLoopEnd_ ;  // Loop end position
    
	fixed baseSpeed_ ;  // The base speed with respect to current note
    fixed speed_ ;      // speed at which we currently travel the stream
	fixed baseVolume_ ;  // Base volume the instrument was triggered with
	fixed volume_ ;     // Current volume
	bool reverse_ ;     // true if we we go backwards in stream

	bool retrig_ ;       // true if we're retriggering
	int retrigLoop_ ;   // number of ticks before retrig
	int retrigCount_ ;  // current tick countdown before retrig
	int retrigOffset_ ; // offset in ticks after retrig
    int printFx_;       // Impulse response-based printable reverb

	bool finished_; // the instrument has cut off
	// Sliced "to end": play from slice start through sample end as oneshot
	// (do not wrap — looping mid-sample → end sounds like crackle).
	bool sliceToEnd_;

	fixed baseFCut_;
    fixed baseFRes_ ;

	fixed cutoff_ ; // filter cutoff
	fixed reso_ ;   // filter reso

	fixed baseFbTun_ ;
	fixed baseFbMix_ ;

	fixed fbTun_ ;
	fixed fbMix_ ;
	
	int feedbackIn_ ;  // Position in ring buffer where start of feedback is
	int feedbackOut_ ; // Position in ring buffer where we 'pick' the samples
	FeedbackMode feedbackMode_ ;
	unsigned char crush_ ; // crush
	unsigned char drive_ ; // crush drive
	fixed attenuate_ ; // filter attenuate

	unsigned char downsample_ ; // downsampling

	fixed basePan_ ; // panning
	fixed pan_ ;

	std::vector<I_SRPUpdater *> updaters_ ;
	std::vector<I_SRPUpdater *> activeUpdaters_ ;

	VolumeRamp volumeRamp_ ;
	Panner panner_ ;
	FCRamp cutRamp_ ;
	FRRamp resRamp_ ;
	FBMixRamp fbMixRamp_ ;
	FBTunRamp fbTunRamp_ ;
	LinSpeedRamp speedRamp_ ;
	LogSpeedRamp legato_ ;
	LogSpeedRamp pfin_ ;
	Arp arp_ ;

	bool couldClick_ ;

	char midiNote_ ;  // Current midi note

	float grainPos_[4];
	float grainPhase_[4];
	float grainDir_[4];
	unsigned int grainRng_;

	float tsPhase_;
	float tsPos_[2];
	float tsBase_;
	float tsCurrent_;
	float tsTarget_;
	float tsSpeed_;

	float svfLp_[2];
	float svfBp_[2];
} ;
#endif
