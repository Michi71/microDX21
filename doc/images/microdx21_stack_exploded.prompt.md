# microDX21 stack — exploded-view image prompt

This file is the **complete prompt and render recipe** for generating a
LEGO-instruction-style "exploded view" image of the microDX21 hardware stack
with `image_gen` (or the bundled `image_gen.py` CLI fallback).

If a future user wants a real rendered PNG (not the SVG schematic in
`microdx21_stack_exploded.svg`), they can run:

```bash
export OPENAI_API_KEY=sk-proj-...        # or any working OpenAI key
python "$HOME/.codex/skills/.system/imagegen/scripts/image_gen.py" generate \
  --prompt-file doc/images/microdx21_stack_exploded.prompt.md \
  --size 1536x1024 \
  --quality high \
  --out doc/images/microdx21_stack_exploded.png
```

The CLI will treat this file as a prompt (its body becomes the prompt text),
so what is in this file is exactly what the model will see — keep the body
below as the canonical spec.

---

## Asset type
infographic-diagram, LEGO-instruction style, exploded isometric view.

## Primary request
Exploded isometric "LEGO-style" assembly diagram of a microDX21 synth stack
for a Raspberry Pi Zero 2 WH. Vertical stack, top to bottom, with clear
vertical alignment and short motion-guide arrows connecting each layer.

## Scene / backdrop
Clean white technical-drawing background with a faint neutral grid; flat
colors, no photorealism, no gradients on parts.

## Stack from TOP to BOTTOM
Each layer is a separate flat rectangular board drawn in slight isometric
perspective and labelled on the right.

1. TOP — Waveshare 2.23" OLED HAT (SSD1305, 128x32, SPI/I2C).
   Small rectangular OLED display module with the visible 128x32 screen on
   top, a thin blue PCB below it, and a 2x20 GPIO female socket on the
   underside. Label: "Display: 128x32 OLED (SSD1305/SH1106)".

2. SECOND — Adafruit Perma-Proto Bonnet Mini (ADA3203).
   Green prototyping PCB with two rows of through-hole pads, labelled
   "GND", "3V3", "GPIO17/ENC", "GPIO27/ENC_BTN", "I2S BCLK/LRC/DATA",
   a 2x20 female header on top and 2x20 male header on the bottom, and three
   mounted components: a KY-040 rotary encoder, two 3.5 mm jack footprints
   labelled "AUDIO OUT L/R" and "MIDI IN/OUT". Label: "Proto Bonnet:
   encoder + jacks + wiring".

3. THIRD — WM8960 Hi-Fi Sound Card HAT (I2S/I2C).
   Red PCB with a centered WM8960 QFN chip outline, two 3.5 mm line-out
   jacks on the front edge labelled "L" and "R", a small speaker terminal
   block labelled "SPK+/SPK-", a pin header labelled "I2S / I2C", and a
   2x20 GPIO header. Label: "DAC: WM8960 (I2S, headphone + line out)".

4. BOTTOM — Raspberry Pi Zero 2 WH.
   Standard Pi Zero layout: small green PCB, micro-USB power on the left
   edge, micro-HDMI on the left edge, microSD slot on the underside (visible
   as a cutout), a 2x20 male GPIO header rising from the top of the board,
   the BCM2710A1 SoC + shielding can, and the "Raspberry Pi Zero 2 WH"
   silkscreen text. Label: "Raspberry Pi Zero 2 WH (BCM2710A1, 1 GHz, 512 MB)".

## Side annotations
- LEFT: four short curved arrows pointing downward labelled "1", "2", "3", "4"
  indicating the assembly order top-to-bottom.
- RIGHT: four short vertical alignment arrows showing the GPIO 2x20 pin
  headers passing through all four boards.

## Style / medium
Clean flat vector-style isometric technical illustration, like a LEGO
building instruction step, with crisp black outlines, flat color fills
(Pi green, OLED black, proto green, WM8960 red, encoder grey), subtle
drop shadow under each board, generous spacing between exploded layers,
no photorealism.

## Composition / framing
Portrait A3/tabloid-style layout, 4:5 ratio, the stack centered
horizontally, all four boards fully visible and not overlapping, labels
readable, generous margins.

## Text (verbatim, in this exact order on the right side of each board, in a clean sans-serif)
- "1. OLED HAT — SSD1305 128x32 (SPI/I2C)"
- "2. Proto Bonnet — KY-040 encoder, 2x 3.5 mm jacks"
- "3. WM8960 HAT — I2S DAC, headphone + line out"
- "4. Raspberry Pi Zero 2 WH — BCM2710A1, 512 MB"

## Constraints
- no logos or trademarks
- no watermark
- no photorealism
- clean line work
- readable labels
- no extra text besides the four labels above and the four step numbers 1-4
- use the exact wording above

## Avoid
gradients, photoreal textures, drop shadows on text, mirrored or
upside-down boards, overlapping boards, hand-drawn sketch style.

## Render parameters
- model: gpt-image-2 (CLI default; `gpt-image-1.5` is also accepted)
- size: 1536x1024 (landscape A3) or 1024x1536 (portrait)
- quality: high
- output format: png
- output path: doc/images/microdx21_stack_exploded.png
