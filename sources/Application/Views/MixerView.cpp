#include "MixerView.h"
#include "Application/AppWindow.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/Player.h"
#include "Application/Player/PlayerChannel.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/char.h"
#include "Application/Views/BaseClasses/ModalView.h"
#include "UIController.h"
#include "VuMeterUtil.h"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>

#define ADD_FX_DIALOG_WIDTH 24
#define ADD_FX_VISIBLE_ROWS 6

class AddMixerFxModal : public ModalView {
public:
    AddMixerFxModal(View &view)
        : ModalView(view), selected_(0), topIndex_(0) {}
    virtual ~AddMixerFxModal() {}

    virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {}

    virtual void OnFocus() {
        Mixer *m = Mixer::GetInstance();
        int count = Mixer::GetAvailableFxCount();
        selected_ = 0;
        for (int i = 0; i < count; i++) {
            if (!m->IsFxAdded((unsigned char)i)) {
                selected_ = i;
                break;
            }
        }
        topIndex_ = 0;
        if (selected_ >= ADD_FX_VISIBLE_ROWS) {
            topIndex_ = selected_ - ADD_FX_VISIBLE_ROWS + 1;
        }
    }

    virtual void DrawView() {
        SetWindow(ADD_FX_DIALOG_WIDTH, ADD_FX_VISIBLE_ROWS + 3);

        GUITextProperties props;
        SetColor(CD_NORMAL);
        DrawString(1, 0, "Add Mixer FX", props);

        Mixer *m = Mixer::GetInstance();
        int count = Mixer::GetAvailableFxCount();

        for (int row = 0; row < ADD_FX_VISIBLE_ROWS; row++) {
            int idx = topIndex_ + row;
            if (idx >= count) {
                break;
            }
            const MixerFxInfo &info = Mixer::GetFxInfo(idx);
            bool added = m->IsFxAdded(info.id);
            char line[32];
            snprintf(line, sizeof(line), "%c %-3s %s", added ? '+' : ' ',
                     info.shortName, info.fullName);

            props.invert_ = (idx == selected_);
            SetColor((idx == selected_) ? CD_HILITE2 : CD_NORMAL);
            DrawString(1, 2 + row, line, props);
        }

        props.invert_ = false;
        SetColor(CD_NORMAL);
        DrawString(1, ADD_FX_VISIBLE_ROWS + 2, "A=add/rem B=close", props);
    }

    virtual void ProcessButtonMask(unsigned short mask, bool pressed) {
        if (!pressed) {
            return;
        }
        if (mask == EPBM_B) {
            EndModal(0);
            return;
        }
        int count = Mixer::GetAvailableFxCount();
        if (count <= 0) {
            return;
        }
        if (mask == EPBM_A) {
            Mixer *m = Mixer::GetInstance();
            if (m->IsFxAdded((unsigned char)selected_)) {
                EndModal(100 + selected_);
            } else {
                EndModal(1 + selected_);
            }
            return;
        }
        if (mask & EPBM_UP) {
            if (selected_ > 0) {
                selected_--;
            } else {
                selected_ = count - 1;
            }
            if (selected_ < topIndex_) {
                topIndex_ = selected_;
            } else if (selected_ >= topIndex_ + ADD_FX_VISIBLE_ROWS) {
                topIndex_ = selected_ - ADD_FX_VISIBLE_ROWS + 1;
            }
            isDirty_ = true;
        } else if (mask & EPBM_DOWN) {
            if (selected_ < count - 1) {
                selected_++;
            } else {
                selected_ = 0;
            }
            if (selected_ < topIndex_) {
                topIndex_ = selected_;
            } else if (selected_ >= topIndex_ + ADD_FX_VISIBLE_ROWS) {
                topIndex_ = selected_ - ADD_FX_VISIBLE_ROWS + 1;
            }
            isDirty_ = true;
        }
    }

private:
    int selected_;
    int topIndex_;
};

static void AddFxModalCallback(View &v, ModalView &d) {
    MixerView &mv = (MixerView &)v;
    mv.OnAddFxModalClosed(d.GetReturnCode());
}

MixerView::MixerView(GUIWindow &w,ViewData *viewData):View(w,viewData) {
	clipboard_.active_=false ;
	clipboard_.data_=0 ;
	invertBatt_=false;
    soloChannel_ = -1;
    mixerRow_ = 1; // start on volume row
    previousViewType_ = VT_SONG; // Default to SongView
    for (int i = 0; i < 9; i++) {
        vuBarHeightsL_[i] = 0;
        vuBarHeightsR_[i] = 0;
    }
}

MixerView::~MixerView() {}

void MixerView::clampMixerRow() {
    int maxRow = 2 + Mixer::GetInstance()->GetAddedFxCount();
    if (mixerRow_ < 1) {
        mixerRow_ = 1;
    }
    if (mixerRow_ > maxRow) {
        mixerRow_ = maxRow;
    }
}

void MixerView::openAddFxModal() {
    AddMixerFxModal *modal = new AddMixerFxModal(*this);
    DoModal(modal, AddFxModalCallback);
}

void MixerView::OnAddFxModalClosed(int returnCode) {
    Mixer *m = Mixer::GetInstance();
    if (returnCode >= 1 && returnCode < 100) {
        unsigned char fxId = (unsigned char)(returnCode - 1);
        m->AddFx(fxId);
        int idx = m->IndexOfAddedFx(fxId);
        if (idx >= 0) {
            mixerRow_ = 2 + idx;
        }
        char notif[48];
        snprintf(notif, sizeof(notif), "          Added FX: %s",
                 Mixer::GetFxInfo(fxId).shortName);
        SetNotification(notif);
    } else if (returnCode >= 100) {
        unsigned char fxId = (unsigned char)(returnCode - 100);
        m->RemoveFx(fxId);
        clampMixerRow();
        char notif[48];
        snprintf(notif, sizeof(notif), "        Removed FX: %s",
                 Mixer::GetFxInfo(fxId).shortName);
        SetNotification(notif);
    }
    ((AppWindow &)w_).Clear(true);
    isDirty_ = true;
}

void MixerView::onStart() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	player->OnStartButton(PM_SONG,from,false,to) ;
} ;

void MixerView::onStop() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	player->OnStartButton(PM_SONG,from,true,to) ;
}

// Stay on the same column as SongView (unless already on Master column)
void MixerView::OnFocus() {
    if (viewData_->mixerCol_ != MASTER_CHANNEL_INDEX) {
        viewData_->mixerCol_ = viewData_->songX_;
    }
    clampMixerRow();
}

void MixerView::updateCursor(int dx,int dy) {
    int maxRow = 2 + Mixer::GetInstance()->GetAddedFxCount();
    clampMixerRow();
    if (dy != 0) {
        // UP/DOWN cycles between rows 1..maxRow (VOL, added FX rows, "add fx")
        if (dy < 0) {
            mixerRow_ = (mixerRow_ == 1) ? maxRow : mixerRow_ - 1;
        } else {
            mixerRow_ = (mixerRow_ == maxRow) ? 1 : mixerRow_ + 1;
        }
		isDirty_ = true;
    }
    if (dx != 0) {
        // LEFT/RIGHT switches between channels 0..7 and 8 (Master)
        int x = viewData_->mixerCol_;
		x += dx;
		if (x < 0) x = 0;
		if (x > MASTER_CHANNEL_INDEX) x = MASTER_CHANNEL_INDEX;
		viewData_->mixerCol_ = x;
		isDirty_ = true;
	}
}

void MixerView::toggleMute() {
    int col = viewData_->mixerCol_;
    UIController::GetInstance()->ToggleMute(col, col);
    isDirty_ = true;
};

void MixerView::toggleSolo() {
	int col=viewData_->mixerCol_ ;
    if (col == MASTER_CHANNEL_INDEX) {
        unMuteAll();
        soloChannel_ = -1;
        return;
    }
	bool entering=(soloChannel_!=col) ;
	UIController::GetInstance()->SwitchSoloMode(col,col,entering) ;
    soloChannel_ = entering ? col : -1;
    isDirty_ = true;
}

void MixerView::unMuteAll() {
    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
    isDirty_ = true;
}

void MixerView::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed) {
        return;
    }

    if (clipboard_.active_) {
		viewMode_=VM_SELECTION ;
	} ;
	// Process selection related keys

    if (viewMode_ == VM_SELECTION) {
        if (clipboard_.active_==false) {
            clipboard_.active_=true ;
            clipboard_.x_ = viewData_->songX_;
            clipboard_.y_=viewData_->songY_ ;
            clipboard_.offset_=viewData_->songOffset_ ;
			saveX_=clipboard_.x_ ;
			saveY_=clipboard_.y_ ;
			saveOffset_=clipboard_.offset_ ;
        }
        processSelectionButtonMask(mask) ;
    } else {

        // Switch back to normal mode

        viewMode_=VM_NORMAL ;
        processNormalButtonMask(mask);
    }
};

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void MixerView::processNormalButtonMask(unsigned int mask) {
    Mixer *m = Mixer::GetInstance();
    clampMixerRow();
    int addedCount = m->GetAddedFxCount();
    int addFxRow = 2 + addedCount;
    unsigned char currentFx =
        (mixerRow_ >= 2 && mixerRow_ < addFxRow)
            ? m->GetAddedFxAt(mixerRow_ - 2)
            : (unsigned char)MIXER_FX_NONE;

    // R Modifier
    if (mask & EPBM_R) {
        if (mask & EPBM_B) {
            toggleMute();
        } else if (mask & EPBM_A) {
            toggleSolo();
        } else if (mask & EPBM_L) {
            unMuteAll();
        } else if (mask & EPBM_UP) {
            // R + UP = go back to previous view
            ViewEvent ve(VET_SWITCH_VIEW, &previousViewType_);
            if (viewData_->mixerCol_ < SONG_CHANNEL_COUNT) {
                viewData_->songX_ = viewData_->mixerCol_;
            }
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_RIGHT) {
            ViewType vt = VT_TABLE;
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            if (viewData_->mixerCol_ < SONG_CHANNEL_COUNT) {
                viewData_->songX_ = viewData_->mixerCol_;
            }
            unsigned char *data = viewData_->GetCurrentSongPointer();
            if (*data != 0xFF) { // Set chain
                viewData_->currentChain_ = *data;
            }
            data = viewData_->GetCurrentChainPointer();
            if (*data != 0xFF) { // Set phrase
                viewData_->currentPhrase_ = *data;
            }
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_START) {
            onStop();
        }
    } else if (mask & EPBM_B) {
        if ((mask & EPBM_L) && currentFx != MIXER_FX_NONE) {
            // L + B on an added FX row removes that FX row from the mixer
            const char *shortName = Mixer::GetFxInfo(currentFx).shortName;
            m->RemoveFx(currentFx);
            clampMixerRow();
            char notifBuf[48];
            snprintf(notifBuf, sizeof(notifBuf), "        Removed FX: %s",
                     shortName);
            SetNotification(notifBuf);
            ((AppWindow &)w_).Clear(true);
            isDirty_ = true;
        } else if (mask & EPBM_A) {
            // B + A = reset parameter on current mixerRow_
            int col = viewData_->mixerCol_;
            if (mixerRow_ == 1) {
                m->SetChannelVolume(col, 0xFF);
                SetNotification("              Volume: FF");
                isDirty_ = true;
            } else if (currentFx != MIXER_FX_NONE) {
                m->ResetFxOnChannel(currentFx, col);
                switch (currentFx) {
                case MIXER_FX_HPF:
                    SetNotification("      High Pass Filter: OFF");
                    break;
                case MIXER_FX_LPF:
                    SetNotification("       Low Pass Filter: OFF");
                    break;
                case MIXER_FX_DLY:
                    SetNotification("              Delay: OFF");
                    break;
                case MIXER_FX_REV:
                    SetNotification("             Reverb: OFF");
                    break;
                case MIXER_FX_TPE:
                    SetNotification("               Tape: OFF");
                    break;
                case MIXER_FX_SCH:
                    SetNotification("          Sidechain: OFF");
                    break;
                default:
                    break;
                }
                isDirty_ = true;
            }
        }
    } else if (mask & EPBM_A) {
        if (mixerRow_ == addFxRow) {
            openAddFxModal();
            return;
        }
        int col = viewData_->mixerCol_;
        if (currentFx == MIXER_FX_SCH) {
            // SCH: A+Up/Down = select trigger source channel (OFF / 00..07),
            // A+Left/Right = adjust ducking amount (00..FF)
            int src = m->GetChannelSidechainSrc(col);
            int amt = m->GetChannelSidechainAmt(col);

            if (mask & EPBM_UP) {
                int cur = (src >= SONG_CHANNEL_COUNT) ? -1 : src;
                for (int step = 0; step < SONG_CHANNEL_COUNT + 1; step++) {
                    cur++;
                    if (cur >= SONG_CHANNEL_COUNT) {
                        cur = -1;
                        break;
                    }
                    if (cur != col) {
                        break;
                    }
                }
                src = (cur < 0) ? 0xFF : cur;
                if (src != 0xFF && amt == 0) {
                    amt = 0xB0;
                }
            } else if (mask & EPBM_DOWN) {
                int cur = (src >= SONG_CHANNEL_COUNT) ? SONG_CHANNEL_COUNT : src;
                for (int step = 0; step < SONG_CHANNEL_COUNT + 1; step++) {
                    cur--;
                    if (cur < 0) {
                        cur = -1;
                        break;
                    }
                    if (cur != col) {
                        break;
                    }
                }
                src = (cur < 0) ? 0xFF : cur;
                if (src != 0xFF && amt == 0) {
                    amt = 0xB0;
                }
            } else if (mask & EPBM_RIGHT) {
                if (src >= SONG_CHANNEL_COUNT) {
                    src = (col == 0) ? 1 : 0;
                    if (amt == 0) {
                        amt = 0xB0;
                    }
                } else {
                    amt += 16;
                    if (amt > 255) {
                        amt = 255;
                    }
                }
            } else if (mask & EPBM_LEFT) {
                if (src < SONG_CHANNEL_COUNT) {
                    amt -= 16;
                    if (amt <= 0) {
                        amt = 0;
                        src = 0xFF;
                    }
                }
            }

            if (src != m->GetChannelSidechainSrc(col) ||
                amt != m->GetChannelSidechainAmt(col)) {
                m->SetChannelSidechainSrc(col, (unsigned char)src);
                m->SetChannelSidechainAmt(col, (unsigned char)amt);
                isDirty_ = true;
            }
            if (src >= SONG_CHANNEL_COUNT || amt == 0) {
                SetNotification("          Sidechain: OFF");
            } else {
                char notifBuf[48];
                if (col == MASTER_CHANNEL_INDEX) {
                    sprintf(notifBuf, "   Sidechain: %02d->MS  amt:%02X", src, amt);
                } else {
                    sprintf(notifBuf, "   Sidechain: %02d->%02d  amt:%02X", src, col, amt);
                }
                SetNotification(notifBuf);
            }
        } else if (currentFx == MIXER_FX_TPE) {
            // TPE: A+Up/Down = coarse +-16, A+Left/Right = fine +-4
            int amt = m->GetChannelTapeAmt(col);
            if (mask & EPBM_UP) {
                amt += 16;
            } else if (mask & EPBM_DOWN) {
                amt -= 16;
            } else if (mask & EPBM_RIGHT) {
                amt += 4;
            } else if (mask & EPBM_LEFT) {
                amt -= 4;
            }
            if (amt < 0)
                amt = 0;
            if (amt > 255)
                amt = 255;
            if (amt != m->GetChannelTapeAmt(col)) {
                m->SetChannelTapeAmt(col, (unsigned char)amt);
                isDirty_ = true;
            }
            if (amt == 0) {
                SetNotification("               Tape: OFF");
            } else {
                char notifBuf[48];
                sprintf(notifBuf, "            Tape FX: %02X", amt);
                SetNotification(notifBuf);
            }
        } else if (currentFx == MIXER_FX_REV) {
            // REV: A+Up/Down = room size, A+Left/Right = wet mix
            int size = m->GetChannelReverbSize(col);
            int amt = m->GetChannelReverbAmt(col);

            if (mask & EPBM_UP) {
                if (size < CHANNEL_REVERB_MAX_SIZE) {
                    size++;
                }
                if (size > 0 && amt == 0) {
                    amt = 0x60;
                }
            } else if (mask & EPBM_DOWN) {
                if (size > 0) {
                    size--;
                }
            } else if (mask & EPBM_RIGHT) {
                amt = amt + 8;
                if (amt > 255) {
                    amt = 255;
                }
                if (size == 0 && amt > 0) {
                    size = 4;
                }
            } else if (mask & EPBM_LEFT) {
                amt = amt - 8;
                if (amt < 0) {
                    amt = 0;
                }
            }

            if (size != m->GetChannelReverbSize(col) ||
                amt != m->GetChannelReverbAmt(col)) {
                m->SetChannelReverbSize(col, (unsigned char)size);
                m->SetChannelReverbAmt(col, (unsigned char)amt);
                isDirty_ = true;
            }
            if (size == 0) {
                SetNotification("             Reverb: OFF");
            } else {
                char notifBuf[48];
                sprintf(notifBuf, "      Reverb: sz %d  mix:%02X", size, amt);
                SetNotification(notifBuf);
            }
        } else if (currentFx == MIXER_FX_DLY) {
            // DLY: A+Up/Down = delay time in phrase steps,
            // A+Left/Right = wet mix / feedback amount
            int steps = m->GetChannelDelaySteps(col);
            int amt = m->GetChannelDelayAmt(col);
            int maxSteps = 16;
            float stepSamples =
                SyncMaster::GetInstance()->GetStepSampleCount();
            if (stepSamples < 1.0f) {
                stepSamples = 1.0f;
            }
            int fit = (int)(CHANNEL_DELAY_MAX_FRAMES / stepSamples);
            if (fit < 1) {
                fit = 1;
            }
            if (fit < maxSteps) {
                maxSteps = fit;
            }

            if (mask & EPBM_UP) {
                if (steps < maxSteps) {
                    steps++;
                }
                if (steps > 0 && amt == 0) {
                    amt = 0x80;
                }
            } else if (mask & EPBM_DOWN) {
                if (steps > 0) {
                    steps--;
                }
            } else if (mask & EPBM_RIGHT) {
                amt = amt + 8;
                if (amt > 255) {
                    amt = 255;
                }
                if (steps == 0 && amt > 0) {
                    steps = 1;
                }
            } else if (mask & EPBM_LEFT) {
                amt = amt - 8;
                if (amt < 0) {
                    amt = 0;
                }
            }

            if (steps != m->GetChannelDelaySteps(col) ||
                amt != m->GetChannelDelayAmt(col)) {
                m->SetChannelDelaySteps(col, (unsigned char)steps);
                m->SetChannelDelayAmt(col, (unsigned char)amt);
                isDirty_ = true;
            }
            if (steps == 0) {
                SetNotification("              Delay: OFF");
            } else {
                char notifBuf[48];
                sprintf(notifBuf, "       Delay: %d st  mix:%02X", steps, amt);
                SetNotification(notifBuf);
            }
        } else if (currentFx == MIXER_FX_LPF) {
            // On LPF row: A+Up/Down = coarse, A+Left/Right = fine ±10Hz
            unsigned short freq = m->GetChannelLPF(col);
            unsigned short newFreq = freq;
            if (mask & EPBM_UP) {
                if (freq == 0)
                    newFreq = 20;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq =
                        (unsigned short)(freq + step > 20000 ? 0
                                                                : freq + step);
                }
            } else if (mask & EPBM_DOWN) {
                if (freq == 0)
                    newFreq = 20000;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq = (freq <= step)
                                    ? 0
                                    : (unsigned short)(freq - step < 20
                                                            ? 0
                                                            : freq - step);
                }
            } else if (mask & EPBM_RIGHT) {
                if (freq == 0)
                    newFreq = 20;
                else
                    newFreq =
                        (unsigned short)(freq + 10 > 20000 ? 0 : freq + 10);
            } else if (mask & EPBM_LEFT) {
                if (freq == 0)
                    newFreq = 20000;
                else
                    newFreq =
                        (freq <= 10)
                            ? 0
                            : (unsigned short)(freq - 10 < 20 ? 0
                                                                : freq - 10);
            }
            if (newFreq != freq) {
                m->SetChannelLPF(col, newFreq);
                isDirty_ = true;
            }
            if (newFreq == 0) {
                SetNotification("       Low Pass Filter: OFF");
            } else {
                char notifBuf[40];
                sprintf(notifBuf, "       Low Pass Filter: %dHz",
                        (int)newFreq);
                SetNotification(notifBuf);
            }
        } else if (currentFx == MIXER_FX_HPF) {
            // On HPF row: A+Right cycles forward, A+Left cycles backward
            if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
                int mode = m->GetChannelHPF(col);
                if (mask & EPBM_RIGHT) {
                    mode = (mode + 1) % 3;
                } else {
                    mode = (mode + 2) % 3;
                }
                m->SetChannelHPF(col, mode);
                isDirty_ = true;
                const char *modeStr = (mode == 0)   ? "OFF"
                                        : (mode == 1) ? "20Hz"
                                                    : "90Hz";
                std::string notif =
                    std::string("      High Pass Filter: ") + modeStr;
                SetNotification(notif.c_str());
            }
        } else if (mixerRow_ == 1) {
            // On volume row: A adjusts volume
            int currentVol = m->GetChannelVolume(col);
            int newVol = currentVol;

            // Coarse adjustment (UP/DOWN)
            if (mask & EPBM_UP) {
                newVol = currentVol + 16;
            }
            if (mask & EPBM_DOWN) {
                newVol = currentVol - 16;
            }

            // Fine adjustment (RIGHT/LEFT)
            if (mask & EPBM_RIGHT) {
                newVol = currentVol + 1;
            }
            if (mask & EPBM_LEFT) {
                newVol = currentVol - 1;
            }

            // Clamp to valid range (0-255)
            if (newVol < 0)
                newVol = 0;
            if (newVol > 255)
                newVol = 255;

            if (newVol != currentVol) {
                m->SetChannelVolume(col, newVol);
                isDirty_ = true;
            }
        }
    } else {
        // Normal cursor movement (no modifier)
		if (mask & EPBM_UP)
			updateCursor(0, -1);
		if (mask & EPBM_DOWN)
			updateCursor(0, 1);
		if (mask & EPBM_LEFT)
			updateCursor(-1, 0);
		if (mask & EPBM_RIGHT)
			updateCursor(1, 0);

		if (mask & EPBM_START) {
			onStart();
		}
    }
}

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void MixerView::processSelectionButtonMask(unsigned int mask) {

	// B Modifier

    if (mask & EPBM_B) {

    } else {

        // A modifier

        if (mask & EPBM_A) {

        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask&EPBM_START) {
				    onStop() ;
                }
            } else {

                // No modifier
                if (mask & EPBM_START) {
                    onStart();
                }
            }
        }
    }
}

void MixerView::DrawView() {

    Clear();

    Mixer *mixer = Mixer::GetInstance();
    clampMixerRow();
    int addedCount = mixer->GetAddedFxCount();
    int addFxRow = 2 + addedCount;

    GUITextProperties props;
    GUIPoint pos=GetTitlePosition() ;
	GUIPoint anchor=GetAnchor() ;
	char hex[3] ;

// Draw title

	SetColor(CD_NORMAL) ;

    DrawString(pos._x, pos._y, "Mixer", props);

    // Draw mixer grid with row labels (8 tracks + Master column 8)
    pos = anchor;
    short dx = 3;
    short masterSpacing = 1;
    Player *playerInst = Player::GetInstance();
    GUITextProperties rowLabelProps;

    // Row 0: Track number / Master / Mute
    if (mixerRow_ == 0) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 3, pos._y, " ", rowLabelProps);

    for (int i = 0; i < MIXER_CHANNEL_COUNT; i++) {
        int colX = anchor._x + i * dx + ((i == MASTER_CHANNEL_INDEX) ? masterSpacing : 0);
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 0);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 0) ? CD_HILITE2
                                                               : CD_COL_TITLE);

        if (playerInst->IsChannelMuted(i)) {
            DrawString(colX, pos._y, "M ", props);
        } else if (i == MASTER_CHANNEL_INDEX) {
            DrawString(colX, pos._y, "MS", props);
        } else {
            int bus = mixer->GetBus(i);
            hex2char(bus, hex);
            DrawString(colX, pos._y, hex, props);
        }
    }

    // Row 1: volumes
    pos = anchor;
    pos._y += 1;
    if (mixerRow_ == 1) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 5, pos._y, "VOL", rowLabelProps);

    for (int i = 0; i < MIXER_CHANNEL_COUNT; i++) {
        int colX = anchor._x + i * dx + ((i == MASTER_CHANNEL_INDEX) ? masterSpacing : 0);
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 1);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 1) ? CD_HILITE2
                                                               : CD_NORMAL);

        int vol = mixer->GetChannelVolume(i);
        hex2char(vol, hex);
        DrawString(colX, pos._y, hex, props);
    }

    // Dynamically draw ONLY added FX rows starting at anchor._y + 3
    for (int fxIdx = 0; fxIdx < addedCount; fxIdx++) {
        unsigned char fxId = mixer->GetAddedFxAt(fxIdx);
        int rowId = 2 + fxIdx;
        pos = anchor;
        pos._y += 3 + fxIdx;

        if (mixerRow_ == rowId) {
            rowLabelProps.invert_ = true;
            SetColor(CD_HILITE2);
        } else {
            rowLabelProps.invert_ = false;
            SetColor(CD_NORMAL);
        }
        DrawString(pos._x - 5, pos._y, Mixer::GetFxInfo(fxId).shortName,
                   rowLabelProps);

        for (int i = 0; i < MIXER_CHANNEL_COUNT; i++) {
            int colX = anchor._x + i * dx +
                       ((i == MASTER_CHANNEL_INDEX) ? masterSpacing : 0);
            props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == rowId);
            SetColor((i == viewData_->mixerCol_ && mixerRow_ == rowId)
                         ? CD_HILITE2
                         : CD_NORMAL);

            char cellCode[5];
            cellCode[0] = '-';
            cellCode[1] = '-';
            cellCode[2] = ' ';
            cellCode[3] = '\0';

            switch (fxId) {
            case MIXER_FX_HPF: {
                int hpf = mixer->GetChannelHPF(i);
                if (hpf == 1) {
                    cellCode[0] = '2';
                    cellCode[1] = '0';
                } else if (hpf == 2) {
                    cellCode[0] = '9';
                    cellCode[1] = '0';
                }
                break;
            }
            case MIXER_FX_LPF: {
                unsigned short lpf = mixer->GetChannelLPF(i);
                if (lpf == 0) {
                    // "-- "
                } else if (lpf < 100) {
                    cellCode[0] = '0' + (lpf / 10);
                    cellCode[1] = '0' + (lpf % 10);
                } else if (lpf < 1000) {
                    cellCode[0] = '0' + (lpf / 100);
                    cellCode[1] = 'h';
                } else if (lpf < 10000) {
                    cellCode[0] = '0' + (lpf / 1000);
                    cellCode[1] = 'k';
                } else {
                    cellCode[0] = '0' + (lpf / 10000);
                    cellCode[1] = '0' + (lpf % 10000) / 1000;
                    cellCode[2] = 'k';
                    cellCode[3] = ' ';
                    cellCode[4] = '\0';
                }
                break;
            }
            case MIXER_FX_DLY: {
                unsigned char steps = mixer->GetChannelDelaySteps(i);
                if (steps > 0) {
                    hex2char(steps, cellCode);
                }
                break;
            }
            case MIXER_FX_REV: {
                unsigned char size = mixer->GetChannelReverbSize(i);
                if (size > 0) {
                    hex2char(size, cellCode);
                }
                break;
            }
            case MIXER_FX_TPE: {
                unsigned char tape = mixer->GetChannelTapeAmt(i);
                if (tape > 0) {
                    hex2char(tape, cellCode);
                }
                break;
            }
            case MIXER_FX_SCH: {
                unsigned char scSrc = mixer->GetChannelSidechainSrc(i);
                unsigned char scAmt = mixer->GetChannelSidechainAmt(i);
                if (scSrc < SONG_CHANNEL_COUNT && scAmt > 0) {
                    static const char kHexDigits[17] = "0123456789ABCDEF";
                    cellCode[0] = '0' + (scSrc & 0x07);
                    cellCode[1] = kHexDigits[(scAmt >> 4) & 0x0F];
                }
                break;
            }
            default:
                break;
            }
            DrawString(colX, pos._y, cellCode, props);
        }
    }

    // "add fx" button row below added effects
    pos = anchor;
    pos._y += 3 + addedCount + ((addedCount > 0 && addedCount < 5) ? 1 : 0);
    if (mixerRow_ == addFxRow) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 5, pos._y, "add fx", rowLabelProps);

    drawMap() ;
	drawNotes() ;
    EnableNotification();

    // Draw VU bars at the end so they appear on top of everything
    DrawVuBars();
};

void MixerView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {
    drawNotes() ;
};

/******************************************************
 DrawVuBars:
        Draw pixel-rectangle VU meters below each track
 ******************************************************/

void MixerView::DrawVuBars() {
    drawTrackVuOverlay(true);
}

void MixerView::AnimationUpdate() {
    if (HasModal()) {
        View::AnimationUpdate();
        return;
    }
    DrawVuBars();
}
