#include "AudioFileStreamer.h"
#include "Application/Utils/fixed.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"

AudioFileStreamer::AudioFileStreamer() {
	wav_ = 0;
	shift_ = 1;
	mode_ = AFSM_STOPPED;
	position_ = 0.0f;
	step_ = 1.0f;
	channelCount_ = 1;
	size_ = 0;
	pcm_ = 0;
}

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_);
}

bool AudioFileStreamer::Start(const Path &path) {
	Trace::Debug("Starting to stream %s", path.GetPath().c_str());
	path_ = path;
	const char *shift = Config::GetInstance()->GetValue("PRELISTENATTENUATION");
	shift_ = (shift) ? atoi(shift) : 1;
	Trace::Debug("Streaming shift is %d", shift_);

	// Load on the UI / caller thread — never open or read the file from the
	// audio callback (that was causing underruns: crackle + "slow" preview).
	SAFE_DELETE(wav_);
	pcm_ = 0;
	size_ = 0;
	position_ = 0.0f;

	wav_ = WavFile::Open(path.GetPath().c_str());
	if (!wav_) {
		Trace::Error("Failed to open streaming of %s", path.GetPath().c_str());
		mode_ = AFSM_STOPPED;
		return false;
	}

	size_ = wav_->GetSize(-1);
	channelCount_ = wav_->GetChannelCount(-1);
	if (size_ < 1 || channelCount_ < 1) {
		SAFE_DELETE(wav_);
		mode_ = AFSM_STOPPED;
		return false;
	}

	if (!wav_->GetBuffer(0, size_)) {
		Trace::Error("Failed to load streaming buffer for %s",
		             path.GetPath().c_str());
		SAFE_DELETE(wav_);
		mode_ = AFSM_STOPPED;
		return false;
	}
	pcm_ = (short *)wav_->GetSampleBuffer(-1);
	if (!pcm_) {
		SAFE_DELETE(wav_);
		mode_ = AFSM_STOPPED;
		return false;
	}

	int srcRate = wav_->GetSampleRate(-1);
	int dstRate = Audio::GetInstance()->GetSampleRate();
	if (srcRate < 8000) {
		srcRate = 44100;
	}
	if (dstRate < 8000) {
		dstRate = 44100;
	}
	step_ = (float)srcRate / (float)dstRate;
	position_ = 0.0f;
	mode_ = AFSM_PLAYING;
	return true;
}

void AudioFileStreamer::Stop() {
	mode_ = AFSM_STOPPED;
	pcm_ = 0;
	SAFE_DELETE(wav_);
	Trace::Debug("Streaming stopped");
}

bool AudioFileStreamer::Render(fixed *buffer, int samplecount) {
	if (mode_ == AFSM_STOPPED || !pcm_ || size_ < 1) {
		return false;
	}

	memset(buffer, 0, 2 * samplecount * sizeof(fixed));

	const int shift = 1 + shift_;
	const int ch = channelCount_;
	bool stillPlaying = true;

	for (int i = 0; i < samplecount; i++) {
		int idx = (int)position_;
		if (idx >= size_ - 1) {
			stillPlaying = false;
			break;
		}

		float frac = position_ - (float)idx;
		int i0 = idx * ch;
		int i1 = (idx + 1) * ch;

		int sL0 = pcm_[i0] >> shift;
		int sL1 = pcm_[i1] >> shift;
		int sL = sL0 + (int)((sL1 - sL0) * frac);

		int sR = sL;
		if (ch >= 2) {
			int sR0 = pcm_[i0 + 1] >> shift;
			int sR1 = pcm_[i1 + 1] >> shift;
			sR = sR0 + (int)((sR1 - sR0) * frac);
		}

		buffer[i * 2] = i2fp(sL);
		buffer[i * 2 + 1] = i2fp(sR);

		position_ += step_;
		if (position_ >= (float)(size_ - 1)) {
			stillPlaying = false;
			break;
		}
	}

	if (!stillPlaying) {
		mode_ = AFSM_STOPPED;
		pcm_ = 0;
		SAFE_DELETE(wav_);
	}
	return true;
}
