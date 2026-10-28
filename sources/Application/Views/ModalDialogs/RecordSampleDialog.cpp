#include "RecordSampleDialog.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Player/Player.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"
#include "System/System/typedefs.h"
#include <stdio.h>
#include <string.h>


RecordSampleDialog::RecordSampleDialog(View &view)
    : ModalView(view), state_(IDLE), selected_(BTN_REC), toInstr_(0),
      maxSeconds_(kMaxSeconds) {
    toInstr_ = viewData_->currentInstrument_;
}

RecordSampleDialog::~RecordSampleDialog() { capture_.Stop(); }

void RecordSampleDialog::OnFocus() {
    // Stop any sample-lib preview so the mic isn't fighting playback.
    Player::GetInstance()->StopStreaming();
}

void RecordSampleDialog::DrawView() {
    SetWindow(28, 8);
    GUITextProperties props;

    SetColor(CD_NORMAL);
    DrawString(1, 0, "Record sample (mic)", props);

    char line[40];
    int frames = capture_.GetFrameCount();
    int rate = capture_.GetSampleRate();
    if (rate < 1) {
        rate = 44100;
    }
    int secs = frames / rate;
    int cs = (frames % rate) * 100 / rate;
    sprintf(line, "%02d:%02d / %02d:00", secs, cs / 10, maxSeconds_);
    DrawString(1, 2, line, props);

    // Status: blank while idle, REC while capturing, DONE after stop.
    const char *st =
        (state_ == RECORDING) ? "REC" : (state_ == STOPPED) ? "DONE" : "   ";
    DrawString(20, 2, st, props);

    // Peak meter. Brick mic peaks ~500 raw — scale against 4k so the bar
    // actually moves (full-scale 32767 left it stuck at empty).
    int peak = capture_.GetPeak();
    int absPeak = capture_.GetAbsPeak();
    const int meterFull = 4096;
    int bars = (peak * 20) / meterFull;
    if (bars > 20) {
        bars = 20;
    }
    char meter[24];
    memset(meter, '-', 20);
    meter[20] = 0;
    for (int i = 0; i < bars; i++) {
        meter[i] = '#';
    }
    if (absPeak > 0) {
        int mark = (absPeak * 20) / meterFull;
        if (mark > 19) {
            mark = 19;
        }
        if (meter[mark] == '-') {
            meter[mark] = '|';
        }
    }
    DrawString(1, 3, meter, props);

    DrawString(1, 5, "A: select   L/R: move", props);

    int y = 7;
    int offset = 28 / 4;
    const char *labels[3];
    labels[BTN_REC] = (state_ == RECORDING) ? "Stop" : "Rec";
    labels[BTN_SAVE] = "Save";
    labels[BTN_EXIT] = "Exit";
    for (int i = 0; i < 3; i++) {
        const char *text = labels[i];
        int x = offset * (i + 1) - (int)strlen(text) / 2;
        props.invert_ = (i == selected_);
        if (i == BTN_SAVE && state_ != STOPPED) {
            SetColor(CD_BORDER);
        } else {
            SetColor(CD_NORMAL);
        }
        DrawString(x, y, text, props);
    }
}

void RecordSampleDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
    // Playback is paused during capture — live refresh is in AnimationUpdate.
}

void RecordSampleDialog::AnimationUpdate() {
    if (state_ != RECORDING) {
        return;
    }
    // Auto-stop when capture filled its buffer.
    if (!capture_.IsRecording() && capture_.GetFrameCount() > 0) {
        state_ = STOPPED;
    }
    // Draw into the char buffer; AppWindow::onUpdate Flushes right after.
    DrawView();
}

void RecordSampleDialog::startRecording() {
    capture_.Stop();
    int rate = Audio::GetInstance()->GetSampleRate();
    if (rate < 8000) {
        rate = 44100;
    }
    if (!capture_.Start(rate, maxSeconds_)) {
        Trace::Error("RECORD", "Failed to open mic capture");
        state_ = IDLE;
        return;
    }
    state_ = RECORDING;
    selected_ = BTN_REC;
}

void RecordSampleDialog::stopRecording() {
    capture_.Stop();
    if (capture_.GetFrameCount() > 0) {
        state_ = STOPPED;
    } else {
        state_ = IDLE;
    }
}

bool RecordSampleDialog::saveRecording() {
    if (state_ == RECORDING) {
        stopRecording();
    }
    int frames = capture_.GetFrameCount();
    if (frames < 1) {
        return false;
    }

    int rate = capture_.GetSampleRate();
    short *pcm = (short *)SYS_MALLOC(frames * sizeof(short));
    if (!pcm) {
        return false;
    }
    frames = capture_.CopyFrames(pcm, frames);

    int absPeak = capture_.GetAbsPeak();
    // Recompute peak from the buffer in case AbsPeak was lost across Stop().
    if (absPeak < 1) {
        for (int i = 0; i < frames; i++) {
            int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (a > absPeak) {
                absPeak = a;
            }
        }
    }
    Trace::Log("RECORD", "frames=%d rate=%d absPeak=%d", frames, rate, absPeak);

    if (absPeak < 16) {
        // Mic path still muted / wrong device — don't save a flat sample.
        Trace::Error("RECORD", "Capture is silence (peak=%d)", absPeak);
        SYS_FREE(pcm);
        MessageBox *mb =
            new MessageBox(*this, "Mic silent - check capture path");
        View::DoModal(mb);
        return false;
    }

    // Always peak-normalize quiet Brick captures (absPeak often ~500 raw).
    // Target ~90% FS so the instrument waveform is readable.
    if (absPeak > 0 && absPeak < 28000) {
        int gainNum = 29490; // ~0.9 * 32767
        int gainDen = absPeak;
        for (int i = 0; i < frames; i++) {
            int v = ((int)pcm[i] * gainNum) / gainDen;
            if (v > 32767) {
                v = 32767;
            }
            if (v < -32768) {
                v = -32768;
            }
            pcm[i] = (short)v;
        }
        Trace::Log("RECORD", "normalized peak %d -> ~29490 (x%d)", absPeak,
                   gainNum / gainDen);
    }

    // Unique name under the project samples folder.
    char name[64];
    unsigned tick = SDL_GetTicks();
    sprintf(name, "rec_%08x.wav", (unsigned)tick);
    std::string dpath = "samples:";
    dpath += name;
    Path dstPath(dpath.c_str());

    I_File *file =
        FileSystem::GetInstance()->Open(dstPath.GetPath().c_str(), "wb");
    if (!file) {
        Trace::Error("RECORD", "Cannot write %s",
                     dstPath.GetCanonicalPath().c_str());
        SYS_FREE(pcm);
        return false;
    }

    // Minimal mono 16-bit WAV
    unsigned int chunk;
    chunk = Swap32(0x46464952); // RIFF
    file->Write(&chunk, 1, 4);
    unsigned int riffSize = Swap32(36 + frames * 2);
    file->Write(&riffSize, 1, 4);
    chunk = Swap32(0x45564157); // WAVE
    file->Write(&chunk, 1, 4);
    chunk = Swap32(0x20746D66); // fmt
    file->Write(&chunk, 1, 4);
    unsigned int fmtSize = Swap32(16);
    file->Write(&fmtSize, 1, 4);
    unsigned short ushort;
    ushort = Swap16(1); // PCM
    file->Write(&ushort, 1, 2);
    ushort = Swap16(1); // mono
    file->Write(&ushort, 1, 2);
    unsigned int sampleRate = Swap32(rate);
    file->Write(&sampleRate, 1, 4);
    unsigned int byteRate = Swap32(rate * 2);
    file->Write(&byteRate, 1, 4);
    ushort = Swap16(2); // block align
    file->Write(&ushort, 1, 2);
    ushort = Swap16(16); // bits
    file->Write(&ushort, 1, 2);
    chunk = Swap32(0x61746164); // data
    file->Write(&chunk, 1, 4);
    unsigned int dataSize = Swap32(frames * 2);
    file->Write(&dataSize, 1, 4);
    file->Write(pcm, 2, frames);
    file->Close();
    delete file;
    SYS_FREE(pcm);

    // Reload from the resolved filesystem path (not the samples: alias).
    Path loadPath(dstPath.GetCanonicalPath().c_str());
    int sampleID = SamplePool::GetInstance()->AddSampleFromPath(loadPath);
    if (sampleID < 0) {
        Trace::Error("RECORD", "AddSampleFromPath failed (%d)", sampleID);
        return false;
    }

    I_Instrument *instr =
        viewData_->project_->GetInstrumentBank()->GetInstrument(toInstr_);
    if (instr && instr->GetType() == IT_SAMPLE) {
        SampleInstrument *si = (SampleInstrument *)instr;
        si->AssignSample(sampleID);
    }
    Trace::Log("RECORD", "Saved %s as sample %d", name, sampleID);
    return true;
}

void RecordSampleDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed) {
        return;
    }

    isDirty_ = true;

    if (mask & EPBM_LEFT) {
        selected_ = (selected_ + 2) % 3;
        return;
    }
    if (mask & EPBM_RIGHT) {
        selected_ = (selected_ + 1) % 3;
        return;
    }

    if (mask & EPBM_A) {
        switch (selected_) {
        case BTN_REC:
            if (state_ == RECORDING) {
                stopRecording();
            } else {
                startRecording();
            }
            break;
        case BTN_SAVE:
            if (state_ == STOPPED ||
                (state_ == RECORDING && capture_.GetFrameCount() > 0)) {
                if (saveRecording()) {
                    EndModal(1);
                }
            }
            break;
        case BTN_EXIT:
            capture_.Stop();
            EndModal(0);
            break;
        }
        return;
    }

    if (mask & EPBM_B) {
        capture_.Stop();
        EndModal(0);
    }
}
