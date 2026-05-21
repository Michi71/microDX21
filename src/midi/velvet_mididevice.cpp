//
// velvet_mididevice.cpp
//

#include "velvet_mididevice.h"
#include "velvetkeys.h"
#include "config.h"
#include "opmemuadapter.h"
#include <circle/logger.h>

LOGMODULE("midi");

CVelvetMIDIDevice::CVelvetMIDIDevice(CVelvetKeys*   pSynth,
                                     CConfig*       pConfig)
: m_pSynth(pSynth)
, m_pConfig(pConfig)
, m_channel(Omni)
{
}

CVelvetMIDIDevice::~CVelvetMIDIDevice()
{
}

void CVelvetMIDIDevice::SetDeviceName(const std::string& name)
{
    m_deviceName = name;
}

const std::string& CVelvetMIDIDevice::GetDeviceName() const
{
    return m_deviceName;
}

void CVelvetMIDIDevice::SetChannel(u8 channel)
{
    if (channel <= Channels)
        m_channel = channel;
    else
        m_channel = Disabled;
}

u8 CVelvetMIDIDevice::GetChannel() const
{
    return m_channel;
}

void CVelvetMIDIDevice::HandleMIDI(const u8* msg, size_t len, unsigned cable)
{
    if (!msg || len == 0 || !m_pSynth)
        return;

    u8 status  = msg[0];
    u8 type    = status >> 4;
    u8 channel = status & 0x0F;

    // ───────────────────────────────────────────────
    // ROUTING FILTERS
    // ───────────────────────────────────────────────

    // Device enabled?
    if (!m_routing.enableDevice)
        return;

    // Channel filter (routing)
    if (m_routing.channel != Omni && m_routing.channel != channel)
        return;

    // Channel filter (device-level)
    if (m_channel != Omni && m_channel != channel)
        return;

    // ───────────────────────────────────────────────
    // GLOBAL CHANNEL FILTER (from velvetkeys.ini "MidiChannel")
    //
    // Only channel-voice messages (status 0x80-0xEF, type 0x8-0xE) are
    // gated.  SysEx (0xF0), System Common (0xF1-0xF7) and Realtime
    // (0xF8-0xFF) bypass the filter so the Web Configurator and clock
    // messages keep working.
    //
    // When the filter blocks a message we still allow MIDI Thru below
    // (soft-thru semantics — chained devices see everything).
    // ───────────────────────────────────────────────
    const bool isChannelVoice = (type >= 0x8 && type <= 0xE);
    const bool synthAccepts   = !isChannelVoice
                              || m_pSynth->ShouldAcceptChannel(channel);

    if (synthAccepts)
    {
        // Per-device message-type filters.  These ALSO suppress MIDI Thru
        // when blocked (matches the previous behaviour: if a device is
        // configured not to accept e.g. notes, those notes are dropped
        // entirely, not forwarded).
        bool typeAccepted = true;
        switch (type)
        {
            case 0x8: // Note Off
            case 0x9: // Note On
                typeAccepted = m_routing.acceptNotes;
                break;
            case 0xB: // CC
                typeAccepted = m_routing.acceptCC;
                break;
            case 0xD: // Channel Aftertouch
                typeAccepted = m_routing.acceptAftertouch;
                break;
            case 0xE: // Pitchbend
                typeAccepted = m_routing.acceptPitchbend;
                break;
            case 0xF:
                if (status == 0xF0)        typeAccepted = m_routing.acceptSysEx;
                else if (status >= 0xF8)   typeAccepted = m_routing.acceptRealtime;
                break;
        }
        if (!typeAccepted)
            return;

        // ───────────────────────────────────────────────
        // PROCESS MESSAGE
        // ───────────────────────────────────────────────
        switch (type)
        {
            case 0x8:
                if (len >= 3)
                    HandleNoteOff(channel, msg[1], msg[2]);
                break;

            case 0x9:
                if (len >= 3)
                    HandleNoteOn(channel, msg[1], msg[2]);
                break;

            case 0xB:
                if (len >= 3)
                    HandleControlChange(channel, msg[1], msg[2]);
                break;

            case 0xF:
                if (status == 0xF0)
                    HandleSysEx(msg, len);
                break;
        }
    }

    // ───────────────────────────────────────────────
    // MIDI THRU
    // Soft-thru semantics: when the global channel filter blocks a
    // channel-voice message, we still forward it to the chain so other
    // devices downstream see the original stream.
    // ───────────────────────────────────────────────
    if (m_pConfig->GetMIDIThruEnabled())
    {
        // SysEx is already forwarded in HandleSysExFromDevice().
        if (!(type == 0xF && status == 0xF0))
            m_pSynth->ForwardMIDI(msg, len, this);
    }
}

// ───────────────────────────────────────────────
// MESSAGE HANDLERS
// ───────────────────────────────────────────────

void CVelvetMIDIDevice::HandleNoteOn(u8 ch, u8 note, u8 vel)
{
    if (vel == 0)
        m_pSynth->NoteOff(note);
    else
        m_pSynth->NoteOn(note, vel);
}

void CVelvetMIDIDevice::HandleNoteOff(u8 ch, u8 note, u8 vel)
{
    m_pSynth->NoteOff(note);
}

void CVelvetMIDIDevice::HandleControlChange(u8 ch, u8 cc, u8 val)
{
    switch (cc)
    {
        case 1:  // Mod Wheel
            m_pSynth->SetParameter(kParamPMD, val / 127.0f);
            break;

        case 7:  // Volume
            m_pSynth->SetParameter(kParamMasterGain, val / 127.0f);
            break;

        case 10: // Pan
            m_pSynth->SendMidiCmd(0xB0 | ch, 10, val);
            break;

        case 64: // Sustain
            m_pSynth->SendMidiCmd(0xB0 | ch, 64, val);
            break;

        default:
            // Forward every other CC to the synth engine.  COPMEmu::sendMidiCmd
            // dispatches to vkpiano (notes, sustain, …) and additionally
            // calls handleMidiCC(), which routes CC 56–67 / 69–93 through
            // _ccToParamMap to the engine + FX parameters.  Without this
            // line the Web Configurator's knob/switch CCs (56–93) reach
            // the synth but never affect the audio path.
            m_pSynth->SendMidiCmd(0xB0 | ch, cc, val);
            break;
    }
}

void CVelvetMIDIDevice::HandleSysEx(const u8* msg, size_t len)
{
    // Master Volume SysEx
    if (len == 8 &&
        msg[0] == 0xF0 &&
        msg[1] == 0x7F &&
        msg[2] == 0x7F &&
        msg[3] == 0x04 &&
        msg[4] == 0x01 &&
        msg[7] == 0xF7)
    {
        int vol14 = (msg[5] & 0x7F) | ((msg[6] & 0x7F) << 7);
        float norm = vol14 / 16383.0f;
        m_pSynth->SetMasterVolume(norm);
    }

    // Forward all SysEx frames to the synth engine.
    m_pSynth->HandleSysExFromDevice(msg, len, this);
}
