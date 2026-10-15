# name=Mikro Bridge
# supportedDevices=Mikro MK2 Bridge
# Script controller di FL Studio per Mikro Bridge (Maschine Mikro MK2 senza software NI).
# I tasti arrivano come CC sul canale controlli (16): CC 20-46, con SHIFT CC 70-96, SHIFT + pad 1-12 CC 102-113,
# encoder CC 14 (SHIFT: CC 15) relativo, 64 = fermo. Le note dei pad passano allo strumento selezionato.
# Le luci tornano al ponte come CC sullo stesso canale (porta d'uscita "Mikro MK2 Bridge", stesso numero di Port).
# I CC non usati qui restano liberi: si collegano con "Link to controller".

import channels
import device
import general
import midi
import mixer
import patterns
import transport
import ui

CTRL_CHANNEL = 15   # canale 16, contato da 0 come in FL

# Indici dei tasti come in Mikro Bridge (MikroProtocol.h, kButtons): CC = 20 + indice, con SHIFT 70 + indice
PLAY, REC, RESTART, ERASE, GRID, STEP_L, STEP_R, NOTE_REPEAT, SAMPLING, BROWSE, GROUP, MAIN, \
    NAV_L, NAV_R, NAV, CONTROL, F1, F2, F3, SCENE, PATTERN, VIEW, DUPLICATE, SELECT, SOLO, MUTE, ENCODER = range(27)

CC_BUTTON, CC_SHIFT_BUTTON, CC_SHIFT_PAD = 20, 70, 102
CC_ENCODER, CC_SHIFT_ENCODER = 14, 15


# ------------------------------------------------------------------ azioni

def transportButton(command):
    """Comandi di FL che vogliono premuto/rilasciato (2 = premuto, 0 = rilasciato)."""
    return lambda pressed, flags: transport.globalTransport(command, 2 if pressed else 0, flags)


def onPress(action):
    """Azioni che scattano solo alla pressione."""
    def run(pressed, flags):
        if pressed:
            action()
    return run


def toggleWindow(window):
    def run():
        if ui.getVisible(window) and ui.getFocused(window):
            ui.hideWindow(window)
        else:
            ui.showWindow(window)
            ui.setFocused(window)
    return run


def stepChannel(delta):
    def run():
        count = channels.channelCount()
        if count > 0:
            channels.selectOneChannel((channels.selectedChannel() + delta) % count)
    return run


def stepMixerTrack(delta):
    def run():
        mixer.setTrackNumber(max(0, min(mixer.trackCount() - 1, mixer.trackNumber() + delta)))
    return run


def stepPattern(delta):
    def run():
        patterns.jumpToPattern(max(1, patterns.patternNumber() + delta))
    return run


def selectedChannel():
    return channels.selectedChannel()


def togglePlugin():
    if channels.channelCount() > 0:
        channels.showEditor(selectedChannel())


def toggleChannelSettings():
    if channels.channelCount() > 0:
        channels.showCSForm(selectedChannel(), -1)


def muteChannel():
    if channels.channelCount() > 0:
        channels.muteChannel(selectedChannel())


def soloChannel():
    if channels.channelCount() > 0:
        channels.soloChannel(selectedChannel())


def quantize(startOnly):
    def run():
        if channels.channelCount() > 0:
            channels.quickQuantize(selectedChannel(), startOnly)
    return run


# Tasto -> (azione, azione con SHIFT). None = CC libero per "Link to controller"
BUTTONS = {
    PLAY:        (transportButton(midi.FPT_Play),        transportButton(midi.FPT_Loop)),          # play/pausa | pattern/song
    REC:         (transportButton(midi.FPT_Record),      transportButton(midi.FPT_CountDown)),     # rec | conto alla rovescia
    RESTART:     (transportButton(midi.FPT_Stop),        transportButton(midi.FPT_LoopRecord)),    # stop | loop record
    ERASE:       (transportButton(midi.FPT_Delete),      transportButton(midi.FPT_Cut)),           # elimina | taglia
    GRID:        (transportButton(midi.FPT_Snap),        transportButton(midi.FPT_SnapMode)),      # snap on/off | tipo di snap
    STEP_L:      (transportButton(midi.FPT_Rewind),      onPress(lambda: transport.globalTransport(midi.FPT_MarkerJumpJog, -1))),
    STEP_R:      (transportButton(midi.FPT_FastForward), onPress(lambda: transport.globalTransport(midi.FPT_MarkerJumpJog, 1))),
    NOTE_REPEAT: (transportButton(midi.FPT_Metronome),   transportButton(midi.FPT_TapTempo)),      # metronomo | tap tempo
    SAMPLING:    (transportButton(midi.FPT_WaitForInput), transportButton(midi.FPT_Overdub)),      # attendi input | overdub
    BROWSE:      (onPress(toggleWindow(midi.widBrowser)), onPress(lambda: ui.showWindow(midi.widPluginPicker))),
    GROUP:       (None, None),                            # la sua luce mostra il modo dei pad: libero
    MAIN:        (onPress(toggleWindow(midi.widMixer)),   None),
    NAV_L:       (onPress(stepChannel(-1)),               onPress(stepMixerTrack(-1))),
    NAV_R:       (onPress(stepChannel(1)),                onPress(stepMixerTrack(1))),
    NAV:         (transportButton(midi.FPT_Escape),       None),
    CONTROL:     (onPress(togglePlugin),                  onPress(toggleChannelSettings)),
    F1:          (onPress(toggleWindow(midi.widPlaylist)), transportButton(midi.FPT_Save)),
    F2:          (onPress(toggleWindow(midi.widPianoRoll)), transportButton(midi.FPT_SaveNew)),
    F3:          (onPress(toggleWindow(midi.widChannelRack)), None),
    SCENE:       (transportButton(midi.FPT_AddMarker),    None),
    PATTERN:     (onPress(stepPattern(1)),                onPress(stepPattern(-1))),
    VIEW:        (transportButton(midi.FPT_NextWindow),   None),
    DUPLICATE:   (transportButton(midi.FPT_Copy),         transportButton(midi.FPT_Paste)),
    SELECT:      (transportButton(midi.FPT_Insert),       None),
    SOLO:        (onPress(soloChannel),                   onPress(lambda: mixer.soloTrack(mixer.trackNumber()))),
    MUTE:        (onPress(muteChannel),                   onPress(lambda: mixer.muteTrack(mixer.trackNumber()))),
    ENCODER:     (transportButton(midi.FPT_Enter),        None),
}

# SHIFT + pad (indice 0 = pad 1), le funzioni stampate sotto i pad della Mikro
SHIFT_PADS = {
    0: onPress(general.undoUp),                    # UNDO
    1: onPress(general.undoDown),                  # REDO
    2: onPress(general.undoUp),                    # STEP UNDO
    3: onPress(general.undoDown),                  # STEP REDO
    4: onPress(quantize(0)),                       # QUANTIZE
    5: onPress(quantize(1)),                       # QUANTIZE 50%: solo gli inizi delle note
    6: transportButton(midi.FPT_NudgeMinus),       # NUDGE <
    7: transportButton(midi.FPT_NudgePlus),        # NUDGE >
    10: transportButton(midi.FPT_Copy),            # COPY
    11: transportButton(midi.FPT_Paste),           # PASTE
}


def OnMidiMsg(event):
    if event.midiId != midi.MIDI_CONTROLCHANGE or event.midiChan != CTRL_CHANNEL:
        return   # note dei pad e altro: a FL come sempre

    cc, value = event.data1, event.data2

    if cc in (CC_ENCODER, CC_SHIFT_ENCODER):
        delta = value - 64
        if delta != 0:
            if cc == CC_ENCODER:
                transport.globalTransport(midi.FPT_Jog, delta, event.pmeFlags)        # sposta la selezione / posizione
            else:
                transport.globalTransport(midi.FPT_TempoJog, delta * 10, event.pmeFlags)   # tempo, 1 BPM a scatto
        event.handled = True
        return

    action = None
    if CC_BUTTON <= cc < CC_BUTTON + 27:
        action = BUTTONS.get(cc - CC_BUTTON, (None, None))[0]
    elif CC_SHIFT_BUTTON <= cc < CC_SHIFT_BUTTON + 27:
        action = BUTTONS.get(cc - CC_SHIFT_BUTTON, (None, None))[1]
    elif CC_SHIFT_PAD <= cc < CC_SHIFT_PAD + 12:
        action = SHIFT_PADS.get(cc - CC_SHIFT_PAD)

    if action is not None:
        action(value > 0, event.pmeFlags)
        event.handled = True


# ------------------------------------------------------------------ luci

lastLeds = {}


def sendLed(button, on):
    cc = CC_BUTTON + button
    if lastLeds.get(cc) == on:
        return
    lastLeds[cc] = on
    if device.isAssigned():
        device.midiOutMsg(midi.MIDI_CONTROLCHANGE + CTRL_CHANNEL + (cc << 8) + ((127 if on else 0) << 16))


def refreshLeds():
    sendLed(PLAY, transport.isPlaying())
    sendLed(REC, transport.isRecording())
    sendLed(NOTE_REPEAT, ui.isMetronomeEnabled())
    sendLed(GRID, ui.getSnapMode() != midi.Snap_None)
    hasChannel = channels.channelCount() > 0
    sendLed(MUTE, hasChannel and channels.isChannelMuted(selectedChannel()))
    sendLed(SOLO, hasChannel and channels.isChannelSolo(selectedChannel()))


def OnInit():
    lastLeds.clear()
    refreshLeds()


def OnDeInit():
    for cc in list(lastLeds):
        sendLed(cc - CC_BUTTON, False)


def OnRefresh(flags):
    refreshLeds()


def OnIdle():
    refreshLeds()
