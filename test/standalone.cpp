#include "SDL.h"
#include <portmidi.h>
#include <termios.h>
#include <unistd.h>
#include <cmath>
#include <thread>
#include <atomic>
#include <iostream>
#include "opmemu.h"
#include "io/std_filesystem.h"

#define SAMPLE_RATE  48000
#define AUDIO_CHANNELS 2

static int audio_buffer_size;
static int audio_page_size;

static SDL_AudioDeviceID sdl_audio;

COPMEmu* opmemu = nullptr;
float sampleBufferL[512];
float sampleBufferR[512];

std::atomic<char> lastKey = 0;

char getch() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return c;
}

void inputThreadFunc() {
    while (true) {
        char c = getch();
        lastKey = c;
        if (c == 'q') break;
    }
}

inline int16_t floatToInt16(float val) {
    return static_cast<int16_t>(std::fmax(-32767.0f, std::fmin(32767.0f, val * 32767.0f)));
}

void audio_callback(void * /*userdata*/, Uint8 *stream, int len) {
    int numSamples = len / 4;
    opmemu->processBlock(sampleBufferL, sampleBufferR, numSamples);
    int16_t* out = (int16_t*)stream;
    for (int i = 0; i < numSamples; ++i) {
        out[i * 2]     = floatToInt16(sampleBufferL[i]);
        out[i * 2 + 1] = floatToInt16(sampleBufferR[i]);
    }
}

static const char *audio_format_to_str(int format) {
  switch (format) {
  case AUDIO_S8:    return "S8";
  case AUDIO_U8:    return "U8";
  case AUDIO_S16MSB:return "S16MSB";
  case AUDIO_S16LSB:return "S16LSB";
  case AUDIO_U16MSB:return "U16MSB";
  case AUDIO_U16LSB:return "U16LSB";
  case AUDIO_S32MSB:return "S32MSB";
  case AUDIO_S32LSB:return "S32LSB";
  case AUDIO_F32MSB:return "F32MSB";
  case AUDIO_F32LSB:return "F32LSB";
  }
  return "UNK";
}

int MCU_OpenAudio(int deviceIndex, int pageSize, int pageNum) {
  SDL_AudioSpec spec = {};
  SDL_AudioSpec spec_actual = {};
  audio_page_size = (pageSize / 2) * 2;
  audio_buffer_size = audio_page_size * pageNum;
  spec.format = AUDIO_S16SYS;
  spec.freq = SAMPLE_RATE;
  spec.channels = 2;
  spec.callback = audio_callback;
  spec.samples = audio_page_size / 4;
  int num = SDL_GetNumAudioDevices(0);
  if (num == 0) { printf("No audio output device found.\n"); return 0; }
  if (deviceIndex < -1 || deviceIndex >= num) {
    printf("Out of range audio device index. Default selected.\n");
    deviceIndex = -1;
  }
  const char *audioDevicename = deviceIndex == -1 ? "Default device" : SDL_GetAudioDeviceName(deviceIndex, 0);
  sdl_audio = SDL_OpenAudioDevice(deviceIndex == -1 ? NULL : audioDevicename, 0, &spec, &spec_actual, 0);
  if (!sdl_audio) return 0;
  printf("Audio device: %s\n", audioDevicename);
  printf("Audio Actual: F=%s, C=%d, R=%d, B=%d\n",
         audio_format_to_str(spec_actual.format), spec_actual.channels, spec_actual.freq, spec_actual.samples);
  fflush(stdout);
  SDL_PauseAudioDevice(sdl_audio, 0);
  return 1;
}

void MCU_CloseAudio(void) { SDL_CloseAudio(); }

static PmStream *midiInStream;

int MIDI_Init() {
  Pm_Initialize();
  int in_id = Pm_CreateVirtualInput("OPMEmu", NULL, NULL);
  Pm_OpenInput(&midiInStream, in_id, NULL, 0, NULL, NULL);
  Pm_SetFilter(midiInStream, PM_FILT_ACTIVE | PM_FILT_CLOCK | PM_FILT_SYSEX);
  PmEvent receiveBuffer[1];
  while (Pm_Poll(midiInStream)) Pm_Read(midiInStream, receiveBuffer, 1);
  return 1;
}

void MIDI_Quit() { Pm_Terminate(); }

void MIDI_Update() {
  PmEvent event;
  uint8_t data[3];
  while (Pm_Read(midiInStream, &event, 1)) {
    data[0] = Pm_MessageStatus(event.message);
    data[1] = Pm_MessageData1(event.message);
    data[2] = Pm_MessageData2(event.message);
    opmemu->processMidi(&data[0], 3);
  }
}

static const char* playModeName(COPMEmu::PlayMode mode) {
    switch (mode) {
        case COPMEmu::Single: return "SINGLE";
        case COPMEmu::Dual:   return "DUAL";
        case COPMEmu::Split:  return "SPLIT";
    }
    return "?";
}

static const char* pbModeName(int mode) {
    switch (mode) { case 0: return "ALL"; case 1: return "LOW"; case 2: return "HIGH"; case 3: return "K-ON"; }
    return "?";
}

static const char* portaModeName(int mode) {
    switch (mode) { case 0: return "OFF"; case 1: return "FULL"; case 2: return "FINGERED"; }
    return "?";
}

void printStatus() {
    printf("\n=== DX21 Emulator Status ===\n");
    printf("Mode: %s | Mono: %s\n", playModeName(opmemu->getPlayMode()), opmemu->isMono() ? "ON" : "OFF");
    printf("Patch A: %d (%s)\n", opmemu->getPatchA(), opmemu->getProgramName(opmemu->getPatchA()));
    printf("Patch B: %d (%s)\n", opmemu->getPatchB(), opmemu->getProgramName(opmemu->getPatchB()));
    printf("Split: %d | Balance: %d | MasterTune: %d\n",
           opmemu->getSplitPoint(), opmemu->getBalance(), opmemu->getMasterTune());
    printf("PB Mode: %s | PB Range: %d | Porta: %s (rate=%d)\n",
           pbModeName(opmemu->getPBMode()), opmemu->getPitchBendRange(),
           portaModeName(opmemu->getPortamentoMode()), opmemu->getPortamentoRate());
    printf("Ensemble: %s\n", opmemu->getEnsembleOn() ? "ON" : "OFF");
    printf("==========================\n\n");
}

int main() {
  StdFileSystem fs;
  opmemu = new COPMEmu(&fs);
  opmemu->Initialize();
  opmemu->initRamFromRom();

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
    fprintf(stderr, "FATAL: SDL init failed: %s\n", SDL_GetError());
    return 2;
  }
  if (!MCU_OpenAudio(-1, 512, 32)) {
    fprintf(stderr, "FATAL: Audio open failed\n");
    return 2;
  }
  if (!MIDI_Init()) {
    fprintf(stderr, "WARNING: MIDI init failed\n");
  }

  std::thread inputThread(inputThreadFunc);

  printf("DX21 OPM Emulator — Interactive Test\n");
  printf("MIDI: NoteOn/Off, CC#1=Mod, CC#2=Breath, CC#64=Sustain, PitchBend\n");
  printf("Keys: s/d/p=Mode  m=Mono  +/-=PatchA  a/b=PatchB  [/]=Split  \u003c/\u003e=Balance\n");
  printf("      l/h/k/0=PBMode  r=PBRange  o=PortaMode  O/P=PortaRate  t/T=Tune  e=Ensemble\n");
  printf("      S=saveRam  L=loadRam  P=savePerf  p=loadPerf  0-9=applyPerf\n");
  printf("      ?=Status  q=Quit\n\n");

  bool quit_requested = false;
  bool ensemble = false;
  int currentPatchA = 0;
  int currentPatchB = 0;
  int splitPoint = 60;
  int balance = 50;

  opmemu->setCurrentProgram(currentPatchA);
  opmemu->setPatchA(currentPatchA);
  opmemu->setPatchB(currentPatchB);
  opmemu->setSplitPoint(splitPoint);
  opmemu->setBalance(balance);
  printStatus();

  while (!quit_requested) {
    MIDI_Update();
    SDL_Event sdl_event;
    while (SDL_PollEvent(&sdl_event)) {
      if (sdl_event.type == SDL_QUIT) quit_requested = true;
    }

    char c = lastKey.exchange(0);
    if (c == 0) { SDL_Delay(1); continue; }

    SDL_LockAudioDevice(sdl_audio);
    switch (c) {
      case 's': opmemu->setPlayMode(COPMEmu::Single); printf("Mode: SINGLE\n"); break;
      case 'd': opmemu->setPlayMode(COPMEmu::Dual);   printf("Mode: DUAL\n"); break;
      case 'p': opmemu->setPlayMode(COPMEmu::Split);  printf("Mode: SPLIT\n"); break;
      case 'm': opmemu->setMono(!opmemu->isMono()); printf("Mono: %s\n", opmemu->isMono() ? "ON" : "OFF"); break;
      case '+': currentPatchA = (currentPatchA + 1) % opmemu->getNumPrograms(); opmemu->setPatchA(currentPatchA); printf("Patch A: %d - %s\n", currentPatchA, opmemu->getProgramName(currentPatchA)); break;
      case '-': currentPatchA = currentPatchA - 1; if (currentPatchA < 0) currentPatchA = opmemu->getNumPrograms() - 1; opmemu->setPatchA(currentPatchA); printf("Patch A: %d - %s\n", currentPatchA, opmemu->getProgramName(currentPatchA)); break;
      case 'a': currentPatchB = (currentPatchB + 1) % opmemu->getNumPrograms(); opmemu->setPatchB(currentPatchB); printf("Patch B: %d - %s\n", currentPatchB, opmemu->getProgramName(currentPatchB)); break;
      case 'b': currentPatchB = currentPatchB - 1; if (currentPatchB < 0) currentPatchB = opmemu->getNumPrograms() - 1; opmemu->setPatchB(currentPatchB); printf("Patch B: %d - %s\n", currentPatchB, opmemu->getProgramName(currentPatchB)); break;
      case '[': splitPoint = std::max(0, splitPoint - 1); opmemu->setSplitPoint(splitPoint); printf("Split: %d\n", splitPoint); break;
      case ']': splitPoint = std::min(127, splitPoint + 1); opmemu->setSplitPoint(splitPoint); printf("Split: %d\n", splitPoint); break;
      case ',':
      case '<': balance = std::max(0, balance - 1); opmemu->setBalance(balance); printf("Balance: %d\n", balance); break;
      case '.':
      case '>': balance = std::min(99, balance + 1); opmemu->setBalance(balance); printf("Balance: %d\n", balance); break;
      case 'l': opmemu->setPBMode(1); printf("PB Mode: LOW\n"); break;
      case 'h': opmemu->setPBMode(2); printf("PB Mode: HIGH\n"); break;
      case 'k': opmemu->setPBMode(3); printf("PB Mode: K-ON\n"); break;
      case '0': opmemu->setPBMode(0); printf("PB Mode: ALL\n"); break;
      case 'r': { int r = opmemu->getPitchBendRange(); r = (r + 1) % 13; opmemu->setPitchBendRange(r); printf("PB Range: %d\n", r); break; }
      case 'o': { int mode = opmemu->getPortamentoMode(); mode = (mode + 1) % 3; opmemu->setPortamentoMode(mode); printf("Portamento: %s\n", portaModeName(mode)); break; }
      case 'O': { int rate = opmemu->getPortamentoRate(); rate = std::min(99, rate + 5); opmemu->setPortamentoRate(rate); printf("Porta Rate: %d\n", rate); break; }
      case 'P': { int rate = opmemu->getPortamentoRate(); rate = std::max(0, rate - 5); opmemu->setPortamentoRate(rate); printf("Porta Rate: %d\n", rate); break; }
      case 't': { int t = opmemu->getMasterTune(); t = std::max(-64, t - 1); opmemu->setMasterTune(t); printf("Tune: %d\n", t); break; }
      case 'T': { int t = opmemu->getMasterTune(); t = std::min(63, t + 1); opmemu->setMasterTune(t); printf("Tune: %d\n", t); break; }
      case 'e': opmemu->setEnsembleOn(!ensemble); ensemble = opmemu->getEnsembleOn(); printf("Ensemble: %s\n", ensemble ? "ON" : "OFF"); break;
      case '?':
      case '/': printStatus(); break;
      case 'q': quit_requested = true; break;
    }
    SDL_UnlockAudioDevice(sdl_audio);
  }

  inputThread.join();
  MCU_CloseAudio();
  MIDI_Quit();
  SDL_Quit();
  delete opmemu;
  return 0;
}
