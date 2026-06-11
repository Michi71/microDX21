# -*- coding: utf-8 -*-
"""Generate doc/microdx21-manual.pdf — English user manual for the
microDX21 single-encoder UI.

Mirrors doc/dx21-man.pdf's structure (5 modes + boot + MIDI + data
chart) but maps the original DX21 front-panel buttons onto the
KY-040 rotary encoder gestures defined in src/display/dx21_input.cpp.
"""

import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib.enums import TA_LEFT, TA_CENTER, TA_JUSTIFY
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, PageBreak, Table, TableStyle,
    PageTemplate, Frame,
)

OUT = "doc/microdx21-manual.pdf"

# ---------------------------------------------------------------------------
# Page templates
# ---------------------------------------------------------------------------

PAGE_W, PAGE_H = A4

def cover_page(canv, doc):
    canv.saveState()
    canv.setFont("Helvetica-Bold", 28)
    canv.setFillColor(colors.HexColor("#1a1a1a"))
    canv.drawCentredString(PAGE_W / 2, PAGE_H - 60 * mm, "microDX21")
    canv.setFont("Helvetica", 14)
    canv.drawCentredString(PAGE_W / 2, PAGE_H - 72 * mm,
                           "Yamaha DX21 Emulator")
    canv.setFont("Helvetica-Bold", 18)
    canv.drawCentredString(PAGE_W / 2, PAGE_H - 95 * mm,
                           "OWNER'S MANUAL")
    canv.setFont("Helvetica", 11)
    canv.drawCentredString(PAGE_W / 2, PAGE_H - 108 * mm,
                           "Raspberry Pi 3 / 4 / 5  \u00b7  Bare-Metal  \u00b7  "
                           "KY-040 Rotary Encoder")
    canv.setFont("Helvetica", 9)
    canv.setFillColor(colors.grey)
    canv.drawCentredString(PAGE_W / 2, 30 * mm,
                           "This manual describes how to operate microDX21 "
                           "with the rotary encoder and the 128x32 OLED display.")
    canv.drawCentredString(PAGE_W / 2, 22 * mm,
                           "Functions mirror the original Yamaha DX21 "
                           "Owner's Manual; strings are extracted from the "
                           "disassembled ROM V1.5 firmware.")
    canv.restoreState()


def inner_page(canv, doc):
    canv.saveState()
    canv.setFont("Helvetica", 8)
    canv.setFillColor(colors.grey)
    canv.drawString(20 * mm, 12 * mm,
                    "microDX21  \u00b7  Owner's Manual")
    canv.drawRightString(PAGE_W - 20 * mm, 12 * mm,
                         "Page %d" % doc.page)
    canv.setStrokeColor(colors.lightgrey)
    canv.line(20 * mm, 17 * mm, PAGE_W - 20 * mm, 17 * mm)
    canv.restoreState()


# ---------------------------------------------------------------------------
# Styles
# ---------------------------------------------------------------------------

styles = getSampleStyleSheet()

H1 = ParagraphStyle("H1", parent=styles["Heading1"],
                    fontName="Helvetica-Bold", fontSize=18, leading=22,
                    spaceBefore=4, spaceAfter=10, textColor=colors.HexColor("#1a1a1a"))
H2 = ParagraphStyle("H2", parent=styles["Heading2"],
                    fontName="Helvetica-Bold", fontSize=13, leading=16,
                    spaceBefore=14, spaceAfter=6, textColor=colors.HexColor("#1a1a1a"))
H3 = ParagraphStyle("H3", parent=styles["Heading3"],
                    fontName="Helvetica-Bold", fontSize=11, leading=14,
                    spaceBefore=8, spaceAfter=4, textColor=colors.HexColor("#333333"))
P  = ParagraphStyle("P", parent=styles["BodyText"],
                    fontName="Helvetica", fontSize=10, leading=13.5,
                    alignment=TA_JUSTIFY, spaceAfter=6)
Li = ParagraphStyle("Li", parent=styles["BodyText"],
                    fontName="Helvetica", fontSize=10, leading=13,
                    leftIndent=14, bulletIndent=2, spaceAfter=2)
Mono = ParagraphStyle("Mono", parent=styles["BodyText"],
                      fontName="Courier", fontSize=9, leading=12,
                      leftIndent=8, spaceAfter=4)
CalloutStyle = ParagraphStyle("Callout", parent=styles["BodyText"],
                              fontName="Helvetica-Oblique", fontSize=10,
                              leading=13, leftIndent=10, rightIndent=10,
                              textColor=colors.HexColor("#444444"),
                              borderColor=colors.HexColor("#bbbbbb"),
                              borderWidth=0.5, borderPadding=6, spaceAfter=10)
Small = ParagraphStyle("Small", parent=styles["BodyText"],
                       fontName="Helvetica", fontSize=8.5, leading=11,
                       textColor=colors.HexColor("#555555"))
TOCEntry = ParagraphStyle("TOC", parent=styles["BodyText"],
                          fontName="Helvetica", fontSize=11, leading=18,
                          leftIndent=4)

def Callout(t): return Paragraph(t, CalloutStyle)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def heading(t, level=1):
    return Paragraph(t, {1: H1, 2: H2, 3: H3}[level])

def para(t): return Paragraph(t, P)

def bullets(items):
    out = []
    for it in items:
        if isinstance(it, tuple):
            label, body = it
            txt = "<b>%s</b>  %s" % (label, body)
        else:
            txt = it
        out.append(Paragraph("\u2022 " + txt, Li))
    return out

def table(rows, col_widths=None, header=True, font_size=9):
    style = [
        ("FONT", (0, 0), (-1, -1), "Helvetica", font_size),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("BOX", (0, 0), (-1, -1), 0.4, colors.HexColor("#888888")),
        ("INNERGRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#bbbbbb")),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]
    if header:
        style += [
            ("FONT", (0, 0), (-1, 0), "Helvetica-Bold", font_size),
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e6e6e6")),
        ]
    t = Table(rows, colWidths=col_widths)
    t.setStyle(TableStyle(style))
    return t

def ascii_box(title, lines):
    content = "  " + title + "\n"
    for ln in lines:
        content += "  " + ln + "\n"
    return Paragraph(content.replace(" ", "&nbsp;")
                                  .replace("<", "&lt;")
                                  .replace(">", "&gt;")
                                  .replace("\n", "<br/>"),
                     Mono)

# ---------------------------------------------------------------------------
# Document
# ---------------------------------------------------------------------------

doc = SimpleDocTemplate(
    OUT,
    pagesize=A4,
    leftMargin=20 * mm, rightMargin=20 * mm,
    topMargin=20 * mm, bottomMargin=22 * mm,
    title="microDX21 Owner's Manual",
    author="microDX21",
)

story = []

# COVER
story.append(Spacer(1, 100 * mm))
story.append(PageBreak())

# TABLE OF CONTENTS
story.append(heading("Table of Contents", 1))
toc = [
    ("1.  Operating the Rotary Encoder", "5"),
    ("    1.1  Gesture Reference", "5"),
    ("    1.2  Browse vs. Edit Mode", "6"),
    ("    1.3  Status-Line Messages", "7"),
    ("2.  PLAY Mode \u2014 Performing", "9"),
    ("    2.1  SINGLE / DUAL / SPLIT", "9"),
    ("    2.2  Voice Selection 1..128", "10"),
    ("3.  EDIT Mode \u2014 Voice Programming", "11"),
    ("    3.1  EDIT Parameters (Table)", "11"),
    ("    3.2  Operator Selection (OP1..OP4)", "13"),
    ("    3.3  EG Copy (Utility)", "14"),
    ("4.  PERFORMANCE Mode \u2014 32 Performance Memories", "15"),
    ("    4.1  Selecting a Performance", "15"),
    ("    4.2  Saving a Performance", "16"),
    ("5.  FUNCTION Mode \u2014 Configuration", "17"),
    ("    5.1  Tuning (Master Tune, Dual Detune)", "17"),
    ("    5.2  MIDI Functions", "18"),
    ("    5.3  Performance and Foot-Switch Functions", "19"),
    ("    5.4  Voice-Name Editor", "20"),
    ("6.  MEMORY Mode \u2014 SD Card / Performance Store", "21"),
    ("    6.1  The Seven Actions", "21"),
    ("    6.2  Worked Examples", "22"),
    ("7.  COMPARE \u2014 Original vs. Edit", "23"),
    ("8.  MEMORY PROTECT", "24"),
    ("9.  MIDI Data Format (Summary)", "25"),
    ("10. Factory Defaults and Bill of Materials", "27"),
    ("Appendix A: DX21 Button \u2192 Encoder Gesture Mapping", "29"),
    ("Appendix B: Display Layout Reference", "30"),
]
for label, page in toc:
    dot = "." * max(2, 80 - len(label) - len(page))
    story.append(Paragraph("%s&nbsp;%s&nbsp;&nbsp;<font color='#888888'>%s</font>" %
                           (label, dot, page), TOCEntry))
story.append(PageBreak())

# =====================================================================
# 1. Operating the Rotary Encoder
# =====================================================================
story.append(heading("1.  Operating the Rotary Encoder", 1))
story.append(para(
    "Unlike the original, the microDX21 has only a single input "
    "device: a KY-040 rotary encoder with push-button. Every DX21 "
    "front-panel control (PLAY/EDIT/FUNCTION/PERFORMANCE/COMPARE/STORE/"
    "Cassette/Memory Protect, \u2026) is folded onto that one knob. The "
    "binding follows the six modes of the original firmware, whose "
    "mode\u2192display tables are extracted verbatim in "
    "<font face='Courier'>src/opm/dx21_ui_strings.h</font>."
))
story.append(heading("1.1  Gesture Reference", 2))
story.append(table(
    [
        ["Gesture", "Effect"],
        ["Rotate CW / CCW",          "Value +1 / -1 per detent"],
        ["Single click",             "Next mode: PLAY \u2192 EDIT \u2192 PERFORM \u2192 FUNCTION \u2192 MEMORY \u2192 PLAY"],
        ["Double click",             "COMPARE: audition the original / buffer voice (PLAY/EDIT/PERFORMANCE)"],
        ["Triple click",             "In EDIT mode: cycle the operator OP1 \u2192 OP2 \u2192 OP3 \u2192 OP4"],
        ["Hold 1 s (first tick)",    "Toggle between Browse and Edit"],
        ["Hold 2 s (second tick)",   "Toggle MEMORY PROTECT"],
    ],
    col_widths=[55 * mm, None],
))
story.append(Spacer(1, 4 * mm))
story.append(ascii_box(
    "Display (128 x 32 OLED, pages 0..3):",
    [
        "+------------------------------------+",
        "| MODE TITLE                 16/36   |  Page 0",
        "|   42          7-segment value      |  Page 1",
        "| PARAMETER NAME                     |  Page 2",
        "| STATUS / HINT                      |  Page 3",
        "+------------------------------------+",
    ]
))
story.append(Spacer(1, 3 * mm))
story.append(heading("1.2  Browse vs. Edit Mode", 2))
story.append(para(
    "When you enter a new mode the display starts automatically in "
    "<b>Browse mode</b> (status line shows <font face='Courier'>BROWSE</font>): "
    "rotation navigates the parameter list without touching the synth. "
    "The first hold (1 s) switches to <b>Edit</b> (status "
    "<font face='Courier'>EDIT</font>): rotation now changes the value "
    "of the currently selected parameter."
))
story.append(para(
    "The split is necessary because a single encoder must handle both "
    "list navigation and value editing. The mode list is one-dimensional: "
    "<b>EDIT</b> and <b>FUNCTION</b> prefer value-editing; "
    "<b>PLAY</b>, <b>PERFORMANCE</b> and <b>MEMORY</b> work in browse "
    "(list selection)."
))
story.append(Callout(
    "Note: in PLAY and PERFORMANCE the encoder is always live \u2014 "
    "rotation changes the current program instantly. In MEMORY rotation "
    "moves through the action / picker list. Hold reaches only the "
    "Memory-Protect toggle here."
))
story.append(heading("1.3  Status-Line Messages", 2))
story.append(para(
    "The status line (page 3) shows transient feedback. Important "
    "messages:"
))
story.append(table(
    [
        ["Message", "Meaning"],
        ["BROWSE / EDIT", "Mode status after a 1 s hold toggle"],
        ["MEMORY PROTECTED/UNPROTEC.", "Hold-2 s toggle of Memory Protect"],
        ["COMPARE:ORIGINAL", "Double-click engaged COMPARE"],
        ["NO EDITS YET", "Double-click refused \u2014 the voice has not been edited yet"],
        ["EDIT OP1..OP4", "Triple-click switched the target operator"],
        ["FROM OPx>OPy", "EG Copy: rotation is picking the source operator"],
        ["EG OPx>OPy OK", "EG Copy executed"],
        ["SRC=DST OPx", "EG Copy refused: source and destination are the same"],
        ["BULK TRANSMITTED / BULK FAILED", "SysEx bulk dump sent / failed"],
        ["NAME EDIT/STORED/REJECTED", "Voice-name editor active / committed / refused"],
        ["MIDI BUFFER FULL/MIDI CSUM ERROR", "MIDI receive-buffer error"],
    ],
    col_widths=[60 * mm, None],
))
story.append(PageBreak())

# =====================================================================
# 2. PLAY
# =====================================================================
story.append(heading("2.  PLAY Mode \u2014 Performing", 1))
story.append(para(
    "<b>PLAY</b> corresponds to the SINGLE / DUAL / SPLIT block of the "
    "original manual. The display shows the number and name of the "
    "current voice and the active play-mode label "
    "(<font face='Courier'>SINGLE</font>, <font face='Courier'>DUAL</font> "
    "or <font face='Courier'>SPLIT</font>)."
))
story.append(ascii_box(
    "PLAY view:",
    [
        "+------------------------------+",
        "|  <042>                       |  Voice number",
        "|  STRINGS                     |  Voice name (inverted under COMPARE)",
        "|  A1-A8                       |  Bank (1 of 4, derived from voice number)",
        "|  PLAY  SINGLE                |  Mode label",
        "+------------------------------+",
    ]
))
story.append(heading("2.1  SINGLE / DUAL / SPLIT", 2))
story.append(para(
    "The play mode is changed in FUNCTION mode (entries <b>Poly Mode</b> "
    "/ <b>Mono Mode</b>, or the voice-mode parameter directly). In "
    "<b>DUAL</b> the two halves can carry independent voices; <b>SPLIT</b> "
    "divides the keyboard at the split point into a left and right side. "
    "In SPLIT the Mono/Poly assignment is taken from each side's patch "
    "(VCED byte 63), so layouts are 4+4, 7+1, 1+7 or 1+1."
))
story.append(heading("2.2  Voice Selection 1..128", 2))
story.append(para(
    "Rotation picks the next / previous voice. Voices 1..128 are "
    "organised as 16 groups of 8 (Group 1..16, 8 voices each). The "
    "current bank (A1-A8, A9-A16, B1-A8, B9-B16) is shown on page 2. "
    "The RAM range (voices 1..32) holds your own patches; voices 33..128 "
    "are the DX21 factory ROM sounds."
))
story.append(Callout(
    "Tip: once the MIDI Sy Info flag is set (FUNCTION #5), every voice "
    "change additionally sends a 1-voice VCED dump "
    "(F0 43 2n 03 .. F7) out of MIDI OUT."
))
story.append(PageBreak())

# =====================================================================
# 3. EDIT
# =====================================================================
story.append(heading("3.  EDIT Mode \u2014 Voice Programming", 1))
story.append(para(
    "<b>EDIT</b> exposes all 36 DX21 voice parameters. The order matches "
    "the list in <font face='Courier'>dat_F610</font> of the original "
    "firmware. Page 1 shows the current value as a 3-digit 7-segment "
    "number (0..255, interpreted as 0..127 / 0..99 / 0..7 depending on "
    "the parameter)."
))
story.append(heading("3.1  EDIT Parameters (Table)", 2))
story.append(table(
    [
        ["#", "Name", "Range", "Effect"],
        ["1",  "ALG",                "0..7",        "Algorithm"],
        ["2",  "ALGORITHM SELECT",   "0..7",        "Algorithm (long form)"],
        ["3",  "FEEDBACK",           "0..7",        "Feedback level"],
        ["4",  "LFO WAVE:",          "0..3",        "LFO waveform (TRIANGLE / SAW UP / SQUARE / S/HOLD)"],
        ["5",  "LFO SPEED",          "0..99",       "LFO speed"],
        ["6",  "LFO DELAY",          "0..99",       "LFO onset delay"],
        ["7",  "LFO PMD",            "0..99",       "LFO \u2192 pitch mod depth"],
        ["8",  "LFO AMD",            "0..99",       "LFO \u2192 amp mod depth"],
        ["9",  "P MOD SENS.",        "0..7",        "Pitch-mod sensitivity"],
        ["10", "A MOD SENS.",        "0..3",        "Amp-mod sensitivity"],
        ["11", "E BIAS SENS.",       "0..7",        "EG-bias sensitivity (OP)"],
        ["12", "KEY VELOCITY",       "0..7",        "Velocity sensitivity (OP)"],
        ["13", "FREQUENCY =",        "0..63",       "Coarse ratio (OP)"],
        ["14", "DETUNE    =",        "-7..+7",      "Detune in cents (OP)"],
        ["15", "EG  AR",             "0..31",       "Attack rate (OP)"],
        ["16", "EG  D1R",            "0..31",       "Decay-1 rate (OP)"],
        ["17", "EG  D1L",            "0..15",       "Decay-1 level (OP)"],
        ["18", "EG  D2R",            "0..31",       "Decay-2 rate (OP)"],
        ["19", "EG  RR",             "0..15",       "Release rate (OP)"],
        ["20", "OUTPUT LEVEL",       "0..99",       "Operator level (OP)"],
        ["21", "RATE SCALE",         "0..3",        "Rate scaling (OP)"],
        ["22", "LEVEL SCALE",        "0..99",       "Level scaling (OP)"],
        ["23", "PEG RATE 1",         "0..99",       "Pitch-EG rate 1"],
        ["24", "PEG LEVEL 1",        "0..99",       "Pitch-EG level 1"],
        ["25", "PEG RATE 2",         "0..99",       "Pitch-EG rate 2"],
        ["26", "PEG LEVEL 2",        "0..99",       "Pitch-EG level 2"],
        ["27", "PEG RATE 3",         "0..99",       "Pitch-EG rate 3"],
        ["28", "PEG LEVEL 3",        "0..99",       "Pitch-EG level 3"],
        ["29", "SAW UP",             "\u2014",       "Waveform alias"],
        ["30", "SQUARE",             "\u2014",       "Waveform alias"],
        ["31", "TRIANGL",            "\u2014",       "Waveform alias"],
        ["32", "S/HOLD",             "\u2014",       "Waveform alias"],
        ["33", "EG Copy",            "Utility",     "EG Copy: rotation picks the source OP"],
        ["34", "from OP",            "Utility",     "EG Copy: current source-OP selection"],
        ["35", "LFO SYNC :",         "ON/OFF",      "LFO key sync"],
        ["36", "Memory Protected",   "ON/OFF",      "Memory Protect (see Ch. 8)"],
    ],
    col_widths=[8 * mm, 38 * mm, 22 * mm, None],
    font_size=8.5,
))
story.append(heading("3.2  Operator Selection (OP1..OP4)", 2))
story.append(para(
    "Parameters 11..22 and 23..27 (per-operator: EG, OUT, FREQ, DET, "
    "RS, LS, EBS, KVS) act on the currently selected operator. In EDIT "
    "mode the selection is reached with a <b>triple click</b>: "
    "OP1 \u2192 OP2 \u2192 OP3 \u2192 OP4 \u2192 OP1. The current selection is shown in "
    "the title as <font face='Courier'>OPn</font> and acknowledged on "
    "the status line whenever it changes."
))
story.append(heading("3.3  EG Copy (Utility)", 2))
story.append(para(
    "EG Copy duplicates the five EG parameters (AR, D1R, D1L, D2R, RR) "
    "from a source operator into the destination operator chosen via "
    "triple-click. Procedure:"
))
story += bullets([
    "Triple-click in EDIT mode \u2192 pick the destination OP (status: <font face='Courier'>EDIT OP1..4</font>).",
    "Navigate to entry <b>EG Copy</b> or <b>from OP</b>.",
    "Rotate \u2192 set the source OP (status: <font face='Courier'>FROM OPx>OPy</font>).",
    "Single click \u2192 run the copy. On success the status line shows <font face='Courier'>EG OPx>OPy OK</font>; otherwise <font face='Courier'>SRC=DST OPx</font> or <font face='Courier'>EG COPY FAILED</font>.",
])
story.append(Callout(
    "Source and destination must differ. With COMPARE active the "
    "snapshot is renewed automatically; the Memory-Protect gate applies "
    "here as well \u2014 if it is ON, the copy is refused."
))
story.append(PageBreak())

# =====================================================================
# 4. PERFORMANCE
# =====================================================================
story.append(heading("4.  PERFORMANCE Mode \u2014 32 Performance Memories", 1))
story.append(para(
    "<b>PERFORMANCE</b> addresses the 32 performance memories (slot 1..32). "
    "Each performance stores: play mode (SINGLE/DUAL/SPLIT), voice A, "
    "voice B, split point, balance, pitch-bend range and mode, portamento "
    "mode and rate, mod-wheel and breath-controller sensitivities, chorus, "
    "transpose, key shift and a 10-character name."
))
story.append(ascii_box(
    "PERFORMANCE view:",
    [
        "+------------------------------+",
        "| PERFORM SI  3/32             |  Mode tag, slot",
        "|  CHOIR                       |  Performance name (or BUFF under COMPARE)",
        "| A:005  B:008                 |  Programmed voices",
        "| EDIT=APPLY                   |  Hint",
        "+------------------------------+",
    ]
))
story.append(heading("4.1  Selecting a Performance", 2))
story.append(para(
    "Rotation in Browse mode steps through slot 1..32. The status line "
    "shows <font face='Courier'>PERF APPLIED</font> if a valid slot was "
    "loaded, otherwise <font face='Courier'>EMPTY PERF</font>. In "
    "COMPARE mode the slot name is replaced by <font face='Courier'>"
    "BUFF</font> (see Ch. 7)."
))
story.append(heading("4.2  Saving a Performance", 2))
story.append(para(
    "Saving happens through the <b>MEMORY</b> mode (action <b>PERF</b>, "
    "see Ch. 6). The selected performance is held in the edit buffer "
    "(<i>ApplyPerformance</i>) until you explicitly store it."
))
story.append(PageBreak())

# =====================================================================
# 5. FUNCTION
# =====================================================================
story.append(heading("5.  FUNCTION Mode \u2014 Configuration", 1))
story.append(para(
    "FUNCTION corresponds to the FUNCTION block of the original manual. "
    "All 46 entries from <font face='Courier'>dat_FBDA</font> are "
    "preserved in the same order, so the assignment to keys, actions "
    "and values stays identical."
))
story.append(heading("5.1  Tuning (Master Tune, Dual Detune)", 2))
story.append(table(
    [
        ["#", "Name", "Values", "Effect"],
        ["1",  "Master Tune =",      "-64..+63",   "Tuning in cents; A3 = 440 Hz at 0"],
        ["2",  "Dual Detune",        "0..99",      "Detune of the B side in DUAL mode"],
    ],
    col_widths=[8 * mm, 38 * mm, 22 * mm, None],
))
story.append(heading("5.2  MIDI Functions", 2))
story.append(table(
    [
        ["#", "Name", "Values", "Effect"],
        ["3",  "Midi Switch :",   "ON / OFF",   "MIDI receive master switch"],
        ["4",  "Midi Ch Info:",   "ON / OFF",   "Channel information (CC/PC)"],
        ["5",  "Midi Sy Info:",   "ON / OFF",   "SysEx information (real-time param change, bulk)"],
        ["6",  "Midi Recv Ch =",  "1..16",      "Receive channel"],
        ["7",  "Midi Omni :",     "ON / OFF",   "OMNI mode"],
        ["40", "Midi Trns Ch =",  "0..15",      "Transmit channel (0 = off)"],
        ["41", "Midi Transmit ?", "Action",     "Trigger a bulk dump over MIDI OUT"],
    ],
    col_widths=[8 * mm, 38 * mm, 22 * mm, None],
))
story.append(heading("5.3  Performance and Foot-Switch Functions", 2))
story.append(table(
    [
        ["#", "Name", "Values", "Effect"],
        ["8",  "Recall Edit ?",     "Action",   "Edit Recall: bring back the last edited voice"],
        ["9",  "Are You Sure ?",    "Action",   "Confirmation dialog"],
        ["10", "Init. Voice ?",     "Action",   "Reset all voice parameters to their initialised values"],
        ["24", "Poly Mode",         "\u2014",    "Switch play mode SINGLE / DUAL / SPLIT"],
        ["25", "Mono Mode",         "ON/OFF",   "Mono operation (SINGLE only)"],
        ["26", "P Bend Range",      "0..12",    "Pitch-bend range in semitones"],
        ["27", "Fingered Porta",    "ON/OFF",   "Fingered portamento"],
        ["28", "Full Time Porta",   "ON/OFF",   "Full-time portamento"],
        ["29", "Porta Time",        "0..99",    "Portamento rate"],
        ["30", "Foot Volume",       "0..99",    "CC#7 volume range"],
        ["31", "Foot Sustain:",     "ON/OFF",   "CC#64 sustain-pedal enable"],
        ["32", "Foot Porta  :",     "ON/OFF",   "CC#65 portamento-foot-switch enable"],
        ["33", "MW Pitch",          "0..99",    "Mod wheel \u2192 pitch sensitivity"],
        ["34", "MW Amplitude",      "0..99",    "Mod wheel \u2192 amp sensitivity"],
        ["35", "BC Pitch",          "0..99",    "Breath controller \u2192 pitch sensitivity"],
        ["36", "BC Amplitude",      "0..99",    "Breath controller \u2192 amp sensitivity"],
        ["37", "BC Pitch Bias",     "0..99",    "Breath controller \u2192 pitch bias"],
        ["38", "BC EG Bias",        "0..99",    "Breath controller \u2192 EG bias"],
        ["39", "Middle C =",        "-24..+24", "Key offset"],
        ["42", "Chorus :",          "ON/OFF",   "3-line ensemble / chorus"],
        ["43", "Bend Mode =",       "0..3",     "Pitch-bend mode (All / Low / High / K-on)"],
        ["44", "Key Shift =",       "-24..+24", "Transpose in semitones"],
        ["45", "Name :",            "Editor",   "Voice-name editor (see 5.4)"],
    ],
    col_widths=[8 * mm, 38 * mm, 22 * mm, None],
    font_size=8.5,
))
story.append(heading("5.4  Voice-Name Editor", 2))
story.append(para(
    "Entry 45 (<font face='Courier'>Name :</font>) opens a 10-character "
    "editor. Rotation changes the character at the cursor position "
    "(character set: <font face='Courier'>A\u2013Z, a\u2013z, 0\u20139, "
    "space, '-', '.', '/'</font>); a short click advances the cursor. "
    "After the 10th position the name is committed. The status line "
    "acknowledges with <font face='Courier'>NAME STORED</font>, or "
    "\u2014 if Memory Protect is ON \u2014 <font face='Courier'>NAME REJECTED</font>."
))
story.append(PageBreak())

# =====================================================================
# 6. MEMORY
# =====================================================================
story.append(heading("6.  MEMORY Mode \u2014 SD Card / Performance Store", 1))
story.append(para(
    "<b>MEMORY</b> is a five-stage state machine that maps the original "
    "Cassette / Bank functions onto the SD card. There are seven "
    "actions; each is operated by an action picker (stage 0), a YES/NO "
    "confirmation (stage 1), up to two value pickers (stage 2/4) and a "
    "result display (stage 3)."
))
story.append(ascii_box(
    "MEMORY view:",
    [
        "+------------------------------+",
        "| MEMORY  ACT 1                |  Stage 0: pick action",
        "|  SAVE                        |  Action name (large)",
        "| Save to SD    ?              |  Hint text",
        "| (Status / empty)             |",
        "+------------------------------+",
    ]
))
story.append(heading("6.1  The Seven Actions", 2))
story.append(table(
    [
        ["#", "Action", "Picker 1", "Picker 2", "Effect"],
        ["1", "SAVE",  "Group 1..16", "\u2014", "32 RAM voices \u2192 SD:/MICRODX21/BANK_NN/"],
        ["2", "LOAD",  "Group 1..16", "\u2014", "SD bank \u2192 32 RAM voices"],
        ["3", "VRFY",  "Group 1..16", "\u2014", "Verify SD bank vs. RAM"],
        ["4", "STOR",  "Slot 1..32",  "\u2014", "Store the edit voice into a RAM slot"],
        ["5", "ROMG",  "Group 1..16", "Bank 1..4", "ROM group (8 voices) \u2192 RAM bank"],
        ["6", "ROMV",  "Voice 1..128", "Slot 1..32", "One ROM voice \u2192 edit buffer \u2192 RAM"],
        ["7", "PERF",  "Slot 1..32",  "\u2014", "Save the current performance"],
    ],
    col_widths=[10 * mm, 16 * mm, 30 * mm, 18 * mm, None],
    font_size=8.5,
))
story.append(heading("6.2  Worked Examples", 2))
story.append(para("<b>Example A: save the current 32 RAM voices to SD</b>"))
story += bullets([
    "Rotate \u2192 pick <b>SAVE</b> (no. 1).",
    "Click \u2192 YES/NO. Rotate to YES.",
    "Click \u2192 Group picker 1..16; rotate to choose (e.g. 1).",
    "Click \u2192 execution. The display briefly shows <font face='Courier'>OK</font> or <font face='Courier'>SAVE FAILED</font>, then returns to stage 0.",
])
story.append(para("<b>Example B: load a ROM group into a RAM bank</b>"))
story += bullets([
    "Rotate \u2192 pick <b>ROMG</b> (no. 5).",
    "Click \u2192 YES.",
    "Click \u2192 Group picker (ROM group 1..16). Rotate, click.",
    "Click \u2192 Slot picker (RAM bank 1..4 = A1-A8 / A9-A16 / B1-A8 / B9-B16). Rotate, click.",
    "Click \u2192 execution. Memory Protect must be OFF.",
])
story.append(Callout(
    "Before any write action (STORE, ROMG, ROMV, PERF) Memory Protect "
    "must be OFF. When the write-protect is active the call is aborted "
    "and the status line shows <font face='Courier'>MEMORY PROTECTED</font> "
    "(Ch. 8)."
))
story.append(PageBreak())

# =====================================================================
# 7. COMPARE
# =====================================================================
story.append(heading("7.  COMPARE \u2014 Original vs. Edit", 1))
story.append(para(
    "While editing, the original (or the voice in the edit buffer) can "
    "be auditioned directly without losing the in-progress edits. "
    "<b>Double-click</b> in PLAY, EDIT or PERFORMANCE toggles COMPARE."
))
story.append(para("In COMPARE state:"))
story += bullets([
    "In PLAY the voice name is drawn inverted;",
    "In EDIT and PERFORMANCE the voice / performance slot is replaced by <font face='Courier'>BUFF</font>;",
    "The title row additionally shows the COMPARE marker;",
    "The engine switches back to the snapshot taken before the edit; <font face='Courier'>setCompare(true)</font> writes a snapshot as soon as the first edit change arrives.",
])
story.append(Callout(
    "Refused with status <font face='Courier'>NO EDITS YET</font> if no "
    "edit change has happened yet. The original manual also specifies "
    "this rule."
))
story.append(PageBreak())

# =====================================================================
# 8. MEMORY PROTECT
# =====================================================================
story.append(heading("8.  MEMORY PROTECT", 1))
story.append(para(
    "Memory Protect prevents accidental overwriting of the 32 RAM "
    "voices and the 32 performance memories. A 2\u00d72 block in the top-"
    "right corner of the title row is shown whenever Protect is active."
))
story.append(para("To toggle:"))
story += bullets([
    "Hold 2 s (second hold tick) \u2014 mode-independent.",
    "Status line: <font face='Courier'>MEMORY PROTECTED</font> / <font face='Courier'>MEMORY UNPROTECTED</font>.",
    "As an alternative, EDIT entry 36 also toggles Protect from the list display.",
])
story.append(para(
    "Write paths that respect Memory Protect: <b>STORE Voice</b> "
    "(MEMORY #4), <b>ROMG / ROMV</b> (#5/#6), <b>PERF</b> (#7), "
    "<b>set voice name</b> (FUNCTION #45), and all 32-voice bulk dumps "
    "received via SysEx."
))
story.append(PageBreak())

# =====================================================================
# 9. MIDI
# =====================================================================
story.append(heading("9.  MIDI Data Format (Summary)", 1))
story.append(para(
    "microDX21 is hardware-DX21-compatible at the SysEx level. The "
    "following formats are supported:"
))
story.append(table(
    [
        ["Type", "Format", "Hex"],
        ["Real-time parameter change", "F0 43 1n 12 pp vv F7", "0x10 + n"],
        ["1-voice VCED dump",          "F0 43 2n 03 ... F7",     "f=3"],
        ["32-voice VMEM bulk",         "F0 43 2n 04 ... F7",     "f=4"],
        ["Dump request",               "F0 43 2n ff F7",         "f=3/4"],
    ],
    col_widths=[60 * mm, 55 * mm, 25 * mm],
))
story.append(Spacer(1, 2 * mm))
story.append(para(
    "<b>Real-time parameter change</b> sends VCED parameter numbers "
    "0..92 (operators + global voice parameters) plus parameter 93 "
    "(operator-enable bit field). The panel-edit routine uses the same "
    "numbering; incoming MIDI edits never echo back (<i>applySysexParam</i> "
    "sets <i>transmit=false</i>)."
))
story.append(para(
    "When selecting a voice in PLAY SINGLE the matching 1-voice VCED "
    "dump is additionally sent out of MIDI OUT (gated on "
    "<font face='Courier'>FUNCTION #5 Midi Sy Info</font>)."
))
story.append(para(
    "The <b>32-voice bulk</b> sequence sends 4096 data bytes (32 \u00d7 128, "
    "73 bytes of packed VMEM + 55 zero padding per voice) with a "
    "two's-complement checksum. On receive the legacy format f=0x09 "
    "(76-byte VCED, simple sum) is also accepted for maximum "
    "interoperability with older editors."
))
story.append(para(
    "On receive the <b>edit-slot</b> voice "
    "(<font face='Courier'>m_sysexEditVoice</font>) is the destination. "
    "It can be set by MIDI Program Change (voice 1..32 = RAM slot; "
    "voices from the ROM are read-only until they are copied into the "
    "edit slot via the MEMORY action ROMV)."
))
story.append(PageBreak())

# =====================================================================
# 10. Factory defaults and BoM
# =====================================================================
story.append(heading("10.  Factory Defaults and Bill of Materials", 1))
story.append(heading("10.1  Factory Defaults", 2))
story.append(table(
    [
        ["Parameter", "Value"],
        ["Play mode", "SINGLE"],
        ["Poly / Mono", "POLY"],
        ["Master Tune", "0"],
        ["MIDI Switch", "ON"],
        ["Receive channel", "1"],
        ["Transmit channel", "1"],
        ["Sys Info", "ON"],
        ["CH Info", "ON"],
        ["Memory Protect", "ON (turn OFF for a load, then back ON)"],
        ["Chorus / Ensemble", "ON"],
        ["Pitch Bend Range", "2"],
        ["Foot Volume / Sustain / Porta", "OFF"],
    ],
    col_widths=[60 * mm, None],
))
story.append(heading("10.2  Bill of Materials and Prerequisites", 2))
story += bullets([
    "Raspberry Pi 3, 4 or 5 (BCM2837/2711/2712) \u2014 no Linux, Circle bare-metal build.",
    "I2S DAC PCM5102A (or on-board PWM) \u2014 configured in <font face='Courier'>config/microdx21.ini</font>.",
    "128 \u00d7 32 OLED: SSD1305 / SH1106, SPI or I2C, DC pin 24, reset 25, SPI0 8 MHz (default).",
    "KY-040 rotary encoder \u2014 CLK / DT / SW on GPIO 10 / 9 / 11 (default, editable in <font face='Courier'>microdx21.ini</font>).",
    "USB-MIDI host (DIN adapter) or \u2014 on Pi 4 / 5 \u2014 the RP2350 Comms Pi over UART GPIO 14 / 15.",
    "microSD card with boot firmware (<font face='Courier'>kernel8.img</font> for Pi 3, <font face='Courier'>kernel8-rpi4.img</font> for Pi 4, <font face='Courier'>kernel_2712.img</font> for Pi 5) and <font face='Courier'>config/microdx21.ini</font>.",
])
story.append(heading("10.3  Gesture Cheat-Sheet", 2))
story.append(table(
    [
        ["Gesture", "Effect (across all modes)"],
        ["Rotate",         "Value \u00b11 / list navigation / picker change"],
        ["Single click",   "Next mode (PLAY \u2192 EDIT \u2192 PERFORM \u2192 FUNCTION \u2192 MEMORY \u2192 PLAY)"],
        ["Double click",   "COMPARE on / off (PLAY / EDIT / PERFORM)"],
        ["Triple click",   "OP1\u2192OP2\u2192OP3\u2192OP4 (EDIT only)"],
        ["Hold 1 s",       "Browse \u2194 Edit (only meaningful in EDIT / FUNCTION)"],
        ["Hold 2 s",       "Toggle MEMORY PROTECT"],
    ],
    col_widths=[40 * mm, None],
))
story.append(PageBreak())

# =====================================================================
# Appendix A: DX21 button mapping
# =====================================================================
story.append(heading("Appendix A: DX21 Button \u2192 Encoder Gesture Mapping", 1))
story.append(para(
    "The original DX21 has more than 30 buttons (PLAY SINGLE / PLAY "
    "DUAL / PLAY SPLIT / EDIT / COMPARE / FUNCTION / 16 voice selectors "
    "A1..A8 + B1..B8 / DATA ENTRY / -1 / +1 / STORE / PB MODE SET / "
    "KEY SHIFT / CHARACTER). The table below shows how every one of "
    "them maps onto the single encoder."
))
story.append(table(
    [
        ["DX21 control", "Encoder gesture"],
        ["PLAY SINGLE / DUAL / SPLIT",
         "FUNCTION mode: entry 24 / 25 (Poly / Mono mode & voice-mode parameter)"],
        ["EDIT",
         "Single click cycles into EDIT mode"],
        ["EDIT / COMPARE (second press)",
         "Double click \u2014 COMPARE"],
        ["FUNCTION",
         "Single click cycles into FUNCTION mode"],
        ["VOICE 1..32 (A1..A8 / B1..B8)",
         "PLAY mode: rotation selects 1..128 (all 16 groups \u00d7 8)"],
        ["VOICE 1..16 (PERFORMANCE)",
         "PERFORMANCE mode: rotation selects slot 1..32"],
        ["DATA ENTRY dial",
         "Rotation in EDIT / FUNCTION (Edit mode)"],
        ["DATA ENTRY +1 / -1",
         "Rotation (one detent per direction)"],
        ["STORE",
         "MEMORY mode: action STOR, then slot picker, click"],
        ["MEMORY PROTECT",
         "Hold 2 s \u2014 status line acknowledges"],
        ["PB MODE / KEY SHIFT",
         "FUNCTION entry 43 (Bend Mode) and 44 (Key Shift)"],
        ["CHARACTER (voice name)",
         "FUNCTION #45 opens the name editor; rotation changes the character, click advances the cursor"],
        ["Cassette SAVE / LOAD / VERIFY",
         "MEMORY mode, action SAVE / LOAD / VRFY"],
    ],
    col_widths=[55 * mm, None],
))
story.append(PageBreak())

# =====================================================================
# Appendix B: Display layout reference
# =====================================================================
story.append(heading("Appendix B: Display Layout Reference", 1))
story.append(para("<b>PLAY</b>"))
story.append(ascii_box(
    "Page 0: '  <NNN>'   voice number",
    ["Page 1: voice name (inverted under COMPARE)",
     "Page 2: bank (A1-A8 / A9-A16 / B1-A8 / B9-B16)",
     "Page 3: 'PLAY  MODE_LABEL' or status",
     "Top-right pixels: COMPARE marker (4x2) + MEMORY PROTECT (2x2)"]))
story.append(Spacer(1, 3 * mm))
story.append(para("<b>EDIT</b>"))
story.append(ascii_box(
    "Page 0: 'EDIT  OPn N/36' or 'EDIT  N/36'",
    ["Page 1: 3-digit 7-segment value (0..255)",
     "Page 2: EDIT_PARAM_NAMES[N]",
     "Page 3: status / hint / override"]))
story.append(Spacer(1, 3 * mm))
story.append(para("<b>PERFORMANCE</b>"))
story.append(ascii_box(
    "Page 0: 'PERFORM  SI  N/32'  (mode tag: SI / DU / SP)",
    ["Page 1: performance name (or BUFF under COMPARE)",
     "Page 2: 'A:NNN  B:NNN'  (or '-- EMPTY --')",
     "Page 3: status / hint"]))
story.append(Spacer(1, 3 * mm))
story.append(para("<b>FUNCTION</b>"))
story.append(ascii_box(
    "Page 0: 'FUNCTION  N/46'",
    ["Page 1: function name (7-seg, 16 px high)",
     "Page 2: '=NN' / 'ON' / 'OFF' / name editor / '=n/a'",
     "Page 3: status / hint"]))
story.append(Spacer(1, 3 * mm))
story.append(para("<b>MEMORY</b>"))
story.append(ascii_box(
    "Page 0: 'MEMORY  ACT N' / 'YES/NO' / 'PICK NNN' / 'DEST NNN' / 'DONE'",
    ["Page 1: SAVE / LOAD / VRFY / STOR / ROMG / ROMV / PERF / YES/NO / digits / OK / FAILED",
     "Page 2: hint text (per action)",
     "Page 3: status / hint"]))
story.append(Spacer(1, 6 * mm))
story.append(Paragraph(
    "(c) microDX21 project. This document is not an original Yamaha "
    "publication. Where applicable it references the <i>Yamaha DX21 "
    "Owner's Manual</i> (OMD-127-1). The on-screen strings are "
    "extracted from the disassembled original firmware V1.5 "
    "(<font face='Courier'>src/opm/firmware/dx21_rom_v1_5.asm</font>).",
    Small))

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

page_templates = [
    PageTemplate(id="cover", frames=[Frame(0, 0, PAGE_W, PAGE_H, id="cover")],
                 onPage=cover_page),
    PageTemplate(id="inner", frames=[Frame(20*mm, 22*mm,
                                            PAGE_W - 40*mm,
                                            PAGE_H - 42*mm, id="inner")],
                 onPage=inner_page),
]
doc.pageTemplates = page_templates
doc.build(story)
print("Wrote", OUT)
