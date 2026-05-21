# AGENTS.md — microDX21 Architecture & Guidelines

## On Session Start
Ich arbeite an einem Emulator eines Yamaha DX21 Synthesizers. Beachte strikt die Echtzeit-Garantien. Später wird das System auf einem Raspberry Pi 3/4/5 mit Circle Bare Metal laufen, Develop läuft auf macos (M4 Max).

## Projekt-Identität
- **Was**: FM Synthesizer aif Basis von Nuked-OPP
- **Basis**: C++
- **Host**: macOS (M4 Max) zum cross compilen für circle.

## Architektur-Anker (WICHTIG)
1. **Audio-Pfad**: `Synth Engine -> Audio Output`.
2. **Echtzeit-Garantie**: Audio-Code muss deterministisch sein.
   - KEINE `std::vector` Resizing oder `std::string` Operationen im Audio-Callback.
   - KEIN `new`/`delete` nach der Initialisierung.
   - Nutze `volatile` für Flags zwischen Main-Loop und IRQ.
3. **Multi-Platform**: Code muss später auch auf Raspberry Pi laufen. Beachte CPU-Unterschiede bei Optimierungen.

## Code-Konventionen
- **C++ Standard**: Modernes C++ (C++17/20).
- **Naming**: 
  - Klassen: `C` Prefix (z.B. `CSampleEngine`, `CEffectChain`).
  - Member: `m_` Prefix (z.B. `m_pSoundDevice`).
- **Memory**: Samples liegen oft im RAM; achte auf effiziente Pointer-Arithmetik und Memory Alignment.

## Bekannte Komponenten & Integrationen
- **Backends**: Eigenes Synth Adapter.
- **MIDI**: Unterstützung für USB, Serial (GPIO).

## Build-System
- **Primärer Build-Befehl**: make im build/ordner

## Spezifische Instruktionen für Qwen/Codex
- Stelle klärende Fragen, bis du dir zu 95 % sicher ist, dass du deine Aufgabe erfolgreich abschließen kannst.
- Wenn du DSP-Code vorschlägst: Optimiere auf Floating-Point (VFP).

## Imported Claude Cowork project instructions
