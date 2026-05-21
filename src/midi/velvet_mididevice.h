//
// velvet_mididevice.h
//

#ifndef _VELVET_MIDIDEVICE_H
#define _VELVET_MIDIDEVICE_H

#include <circle/types.h>
#include <string>

class CConfig;
class CUserInterface;
class CVelvetKeys;

class CVelvetMIDIDevice
{
public:
    enum TChannel
    {
        Channels = 16,
        Omni     = 16,
        Disabled = 17
    };

    struct MidiRoutingConfig
    {
        bool enableDevice = true;

        bool acceptNotes = true;
        bool acceptCC = true;
        bool acceptPitchbend = true;
        bool acceptAftertouch = true;
        bool acceptSysEx = true;
        bool acceptRealtime = true;

        u8 channel = Omni;   // 0–15 or Omni
    };

public:
    CVelvetMIDIDevice(CVelvetKeys* pSynth,
                      CConfig*     pConfig);
    virtual ~CVelvetMIDIDevice();

    void SetDeviceName(const std::string& name);
    const std::string& GetDeviceName() const;

    void HandleMIDI(const u8* msg, size_t len, unsigned cable = 0);

    virtual void Send(const u8* msg, size_t len, unsigned cable = 0) {}

    void SetChannel(u8 channel);
    u8   GetChannel() const;

    MidiRoutingConfig& Routing() { return m_routing; }

protected:
    void HandleNoteOn(u8 ch, u8 note, u8 vel);
    void HandleNoteOff(u8 ch, u8 note, u8 vel);
    void HandleControlChange(u8 ch, u8 cc, u8 val);
    void HandleSysEx(const u8* msg, size_t len);

protected:
    std::string m_deviceName;    

private:
    CVelvetKeys*    m_pSynth;
    CConfig*        m_pConfig;

    u8              m_channel;      // device-level channel filter

    MidiRoutingConfig m_routing;

};

#endif
