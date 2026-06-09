//
// epd2in13v4display.cpp
//
// Driver for Waveshare 2.13" e-Paper V4 (SSD1680 controller, 122x250 1bpp).
// Designed for bare-metal Raspberry Pi using the Circle framework.
// LVGL renders in RGB565; this driver converts to 1-bit via luma threshold
// and performs partial refresh on every SetArea() call.
//
// Partial-refresh strategy:
//   Initialize() → full init → white base image written to both BW RAM
//   (0x24) and Red RAM (0x26) → full display update.
//   SetArea()    → update shadow buffer (1bpp) → partial update (0x24 only)
//                  using TurnOnDisplay_Partial with busy-wait.
//   RefreshBase() → re-send shadow buffer as base image (full update) to
//                   reset ghosting after many partial updates.
//
// Copyright (C) 2021-2024  R. Stange <rsta2@o2online.de>  (Circle framework)
// EPD protocol derived from Waveshare sample code (MIT licence).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#include "epd2in13v4display.h"
#include <circle/timer.h>
#include <assert.h>

// ──────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ──────────────────────────────────────────────────────────────────────────────

CEpd2in13V4Display::CEpd2in13V4Display (CSPIMaster *pSPIMaster,
                                        unsigned nDCPin,
                                        unsigned nResetPin,
                                        unsigned nBusyPin,
                                        unsigned nChipSelect,
                                        unsigned CPOL,
                                        unsigned CPHA,
                                        unsigned nClockSpeed)
:	CDisplay (RGB565),
	m_pSPIMaster (pSPIMaster),
	m_nChipSelect (nChipSelect),
	m_CPOL (CPOL),
	m_CPHA (CPHA),
	m_nClockSpeed (nClockSpeed),
	m_DCPin    (nDCPin,    GPIOModeOutput),
	m_ResetPin (nResetPin, GPIOModeOutput),
	m_BusyPin  (nBusyPin,  GPIOModeInput),
	m_bBaseImageValid (FALSE)
{
	assert (pSPIMaster != 0);

	// Pre-fill shadow buffer with white (all bits 1)
	for (unsigned i = 0; i < sizeof (m_FrameBuffer); i++)
	{
		m_FrameBuffer[i] = 0xFF;
	}
}

CEpd2in13V4Display::~CEpd2in13V4Display (void)
{
	Sleep ();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────
boolean CEpd2in13V4Display::Initialize (unsigned nRotate)
{
	m_nRotate = nRotate;
	// optional: ungültige Werte abfangen
	if (!(m_nRotate == Rotate0 || m_nRotate == Rotate90 ||
	      m_nRotate == Rotate180 || m_nRotate == Rotate270))
	{
		m_nRotate = Rotate0;
	}

	EpdInitFast();
	EpdDisplayBaseFast(m_FrameBuffer);
	m_bBaseImageValid = TRUE;
	return TRUE;
}

bool CEpd2in13V4Display::MapToPhysical(unsigned x, unsigned y,
                                       unsigned &outX, unsigned &outY) const
{
	const unsigned logicalW = GetWidth();
	const unsigned logicalH = GetHeight();

	if (x >= logicalW || y >= logicalH)
		return false;

	switch (m_nRotate)
	{
	case Rotate0:
		outX = x;
		outY = y;
		return true;

	case Rotate90:
		// logical: 250x122 -> physical: 122x250
		outX = (EPD_2IN13_V4_WIDTH  - 1) - y;
		outY = x;
		return true;

	case Rotate180:
		outX = (EPD_2IN13_V4_WIDTH  - 1) - x;
		outY = (EPD_2IN13_V4_HEIGHT - 1) - y;
		return true;

	case Rotate270:
		outX = y;
		outY = (EPD_2IN13_V4_HEIGHT - 1) - x;
		return true;

	default:
		return false;
	}
}

void CEpd2in13V4Display::Clear (boolean bBlack)
{
	u8 nFill = bBlack ? 0x00u : 0xFFu;
	for (unsigned i = 0; i < sizeof (m_FrameBuffer); i++)
	{
		m_FrameBuffer[i] = nFill;
	}

	EpdInitFast ();
	EpdDisplayBaseFast (m_FrameBuffer);
	m_bBaseImageValid = TRUE;
}

void CEpd2in13V4Display::SetPixel (unsigned nPosX, unsigned nPosY, TRawColor nColor)
{
	unsigned px, py;
	if (!MapToPhysical(nPosX, nPosY, px, py))
		return;

	u8 luma = Rgb565ToLuma ((u16) nColor);
	unsigned idx = py * WIDTH_BYTES + px / 8u;
	unsigned bit = 7u - (px % 8u);

	if (luma >= 128u) m_FrameBuffer[idx] |=  (u8)(1u << bit);
	else              m_FrameBuffer[idx] &= ~(u8)(1u << bit);
}

void CEpd2in13V4Display::SetArea (const TArea &rArea, const void *pPixels,
                                  TAreaCompletionRoutine *pRoutine, void *pParam)
{
	assert (pPixels != 0);

	// Convert the flushed RGB565 region into the 1bpp shadow buffer.
	UpdateFrameBuffer (rArea, (const u16 *) pPixels);

	if (!m_bBaseImageValid)
	{
		// First call: need a full refresh first to establish the base image.
		EpdInitFast ();
		EpdDisplayBaseFast (m_FrameBuffer);
		m_bBaseImageValid = TRUE;
	}
	else
	{
		// Subsequent calls: partial refresh only.
		EpdDisplayPartialWait (m_FrameBuffer);
	}

	if (pRoutine)
	{
		(*pRoutine) (pParam);
	}
}

void CEpd2in13V4Display::RefreshBase (void)
{
	EpdInitFast ();
	EpdDisplayBaseFast (m_FrameBuffer);
	m_bBaseImageValid = TRUE;
}

void CEpd2in13V4Display::Sleep (void)
{
	EpdSendCommand (0x10); // Deep Sleep Mode
	EpdSendData    (0x01);
	CTimer::SimpleMsDelay (100);
}

// ──────────────────────────────────────────────────────────────────────────────
// EPD low-level protocol
// ──────────────────────────────────────────────────────────────────────────────

void CEpd2in13V4Display::EpdReset (void)
{
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (20);
	m_ResetPin.Write (LOW);
	CTimer::SimpleMsDelay (2);
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (20);
}

void CEpd2in13V4Display::EpdWaitBusy (void)
{
	// HIGH = busy, LOW = ready
	while (m_BusyPin.Read () == HIGH)
	{
		CTimer::SimpleMsDelay (10);
	}
	CTimer::SimpleMsDelay (10);
}

void CEpd2in13V4Display::EpdSendCommand (u8 cmd)
{
	m_DCPin.Write (LOW);
	SpiWrite (&cmd, 1);
}

void CEpd2in13V4Display::EpdSendData (u8 data)
{
	m_DCPin.Write (HIGH);
	SpiWrite (&data, 1);
}

void CEpd2in13V4Display::EpdSendDataBuf (const u8 *pData, size_t nLen)
{
	m_DCPin.Write (HIGH);

	// BCM2835 SPI has a per-transfer size limit; chunk as needed.
	const size_t MaxChunk = 0xFFFC;
	while (nLen > 0)
	{
		size_t nChunk = (nLen > MaxChunk) ? MaxChunk : nLen;
		SpiWrite (pData, nChunk);
		pData += nChunk;
		nLen  -= nChunk;
	}
}

void CEpd2in13V4Display::EpdSetWindows (u16 xStart, u16 yStart,
                                        u16 xEnd,   u16 yEnd)
{
	EpdSendCommand (0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
	EpdSendData ((u8) ((xStart >> 3) & 0xFF));
	EpdSendData ((u8) ((xEnd   >> 3) & 0xFF));

	EpdSendCommand (0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
	EpdSendData ((u8)  (yStart       & 0xFF));
	EpdSendData ((u8) ((yStart >> 8) & 0xFF));
	EpdSendData ((u8)  (yEnd         & 0xFF));
	EpdSendData ((u8) ((yEnd   >> 8) & 0xFF));
}

void CEpd2in13V4Display::EpdSetCursor (u16 x, u16 y)
{
	EpdSendCommand (0x4E); // SET_RAM_X_ADDRESS_COUNTER
	EpdSendData ((u8) (x & 0xFF));

	EpdSendCommand (0x4F); // SET_RAM_Y_ADDRESS_COUNTER
	EpdSendData ((u8)  (y       & 0xFF));
	EpdSendData ((u8) ((y >> 8) & 0xFF));
}

void CEpd2in13V4Display::EpdTurnOnDisplay (void)
{
	EpdSendCommand (0x22); // Display Update Control
	EpdSendData    (0xF7);
	EpdSendCommand (0x20); // Activate Display Update Sequence
	EpdWaitBusy ();
}

void CEpd2in13V4Display::EpdTurnOnDisplayFast (void)
{
	EpdSendCommand (0x22); // Display Update Control
	EpdSendData    (0xc7); // fast:0x0c, quality:0x0f, 0xcf
	EpdSendCommand (0x20); // Activate Display Update Sequence
	EpdWaitBusy ();
}

void CEpd2in13V4Display::EpdTurnOnDisplayPartialWait (void)
{
	EpdSendCommand (0x22); // Display Update Control (partial waveform)
	EpdSendData    (0xFF);
	EpdSendCommand (0x20); // Activate Display Update Sequence
	EpdWaitBusy ();
}

void CEpd2in13V4Display::EpdInitFull (void)
{
	EpdReset ();
	EpdWaitBusy ();

	EpdSendCommand (0x12); // Software Reset
	EpdWaitBusy ();

	EpdSendCommand (0x01); // Driver Output Control
	EpdSendData (0xF9);
	EpdSendData (0x00);
	EpdSendData (0x00);

	EpdSendCommand (0x11); // Data Entry Mode: X-inc, Y-inc
	EpdSendData (0x03);

	EpdSetWindows (0, 0, EPD_2IN13_V4_WIDTH - 1, EPD_2IN13_V4_HEIGHT - 1);
	EpdSetCursor  (0, 0);

	EpdSendCommand (0x3C); // Border Waveform
	EpdSendData (0x05);

	EpdSendCommand (0x21); // Display Update Control
	EpdSendData (0x00);
	EpdSendData (0x80);

	EpdSendCommand (0x18); // Read built-in temperature sensor
	EpdSendData (0x80);

	EpdWaitBusy ();
}

void CEpd2in13V4Display::EpdInitFast (void)
{
	EpdReset();
	EpdWaitBusy(); 

	EpdSendCommand(0x12);  //SWRESET
	EpdWaitBusy();   

	EpdSendCommand(0x18); //Read built-in temperature sensor
	EpdSendData(0x80);

	EpdSendCommand(0x11); //data entry mode       
	EpdSendData(0x03);

	EpdSetWindows (0, 0, EPD_2IN13_V4_WIDTH - 1, EPD_2IN13_V4_HEIGHT - 1);
	EpdSetCursor  (0, 0);
		
	EpdSendCommand(0x22); // Load temperature value
	EpdSendData(0xB1);		
	EpdSendCommand(0x20);	
	EpdWaitBusy();   

	EpdSendCommand(0x1A); // Write to temperature register
	EpdSendData(0x64);		
	EpdSendData(0x00);	
					
	EpdSendCommand(0x22); // Load temperature value
	EpdSendData(0x91);		
	EpdSendCommand(0x20);	
	EpdWaitBusy();   
}

void CEpd2in13V4Display::EpdInitPart (void)
{
	// Short reset for partial mode (no full SW reset)
	m_ResetPin.Write (LOW);
	CTimer::SimpleMsDelay (1);
	m_ResetPin.Write (HIGH);

	EpdSendCommand (0x3C); // Border Waveform (partial)
	EpdSendData (0x80);

	EpdSendCommand (0x01); // Driver Output Control
	EpdSendData (0xF9);
	EpdSendData (0x00);
	EpdSendData (0x00);

	EpdSendCommand (0x11); // Data Entry Mode: X-inc, Y-inc
	EpdSendData (0x03);

	EpdSetWindows (0, 0, EPD_2IN13_V4_WIDTH - 1, EPD_2IN13_V4_HEIGHT - 1);
	EpdSetCursor  (0, 0);
}

void CEpd2in13V4Display::EpdDisplayBase (const u8 *pImage)
{
	// Write the same image to both BW RAM (0x24) and Red/Previous RAM (0x26).
	// This is required so partial updates can compare old vs. new correctly.
	EpdSendCommand (0x24); // Write BW RAM
	EpdSendDataBuf (pImage, WIDTH_BYTES * EPD_2IN13_V4_HEIGHT);

	EpdSendCommand (0x26); // Write Red/Previous RAM
	EpdSendDataBuf (pImage, WIDTH_BYTES * EPD_2IN13_V4_HEIGHT);

	EpdTurnOnDisplay ();
}

void CEpd2in13V4Display::EpdDisplayBaseFast (const u8 *pImage)
{
	// Write the same image to both BW RAM (0x24) and Red/Previous RAM (0x26).
	// This is required so partial updates can compare old vs. new correctly.
	EpdSendCommand (0x24); // Write BW RAM
	EpdSendDataBuf (pImage, WIDTH_BYTES * EPD_2IN13_V4_HEIGHT);

	EpdSendCommand (0x26); // Write Red/Previous RAM
	EpdSendDataBuf (pImage, WIDTH_BYTES * EPD_2IN13_V4_HEIGHT);

	EpdTurnOnDisplayFast ();
}

void CEpd2in13V4Display::EpdDisplayPartialWait (const u8 *pImage)
{
	EpdInitPart ();

	EpdSendCommand (0x24); // Write new image to BW RAM
	EpdSendDataBuf (pImage, WIDTH_BYTES * EPD_2IN13_V4_HEIGHT);

	EpdTurnOnDisplayPartialWait ();
}

// ──────────────────────────────────────────────────────────────────────────────
// SPI helper
// ──────────────────────────────────────────────────────────────────────────────

void CEpd2in13V4Display::SpiWrite (const void *pData, size_t nLen)
{
	m_pSPIMaster->SetClock (m_nClockSpeed);
	m_pSPIMaster->SetMode  (m_CPOL, m_CPHA);

#ifndef NDEBUG
	int nResult =
#endif
		m_pSPIMaster->Write (m_nChipSelect, pData, nLen);
	assert (nResult == (int) nLen);
}

// ──────────────────────────────────────────────────────────────────────────────
// RGB565 → 1bpp helpers
// ──────────────────────────────────────────────────────────────────────────────

// BT.601 luma from a native RGB565 word.
// Returns 0–255; caller compares against threshold 128.
u8 CEpd2in13V4Display::Rgb565ToLuma (u16 nPixel)
{
	// Unpack RGB565: R=bits[15:11], G=bits[10:5], B=bits[4:0]
	u8 r5 = (u8) ((nPixel >> 11) & 0x1Fu);
	u8 g6 = (u8) ((nPixel >>  5) & 0x3Fu);
	u8 b5 = (u8)  (nPixel        & 0x1Fu);

	// Expand to 8-bit
	u8 r8 = (u8) ((r5 << 3) | (r5 >> 2));
	u8 g8 = (u8) ((g6 << 2) | (g6 >> 4));
	u8 b8 = (u8) ((b5 << 3) | (b5 >> 2));

	// BT.601 luma: Y = 0.299R + 0.587G + 0.114B  (integer: /256)
	return (u8) ((r8 * 77u + g8 * 150u + b8 * 29u) >> 8);
}

void CEpd2in13V4Display::UpdateFrameBuffer (const TArea &rArea, const u16 *pPixels)
{
	int nSrcWidth  = (int)(rArea.x2 - rArea.x1 + 1);
	int nSrcHeight = (int)(rArea.y2 - rArea.y1 + 1);

	for (int y = 0; y < nSrcHeight; y++)
	{
		for (int x = 0; x < nSrcWidth; x++)
		{
			unsigned lx = (unsigned)rArea.x1 + (unsigned)x; // logical x
			unsigned ly = (unsigned)rArea.y1 + (unsigned)y; // logical y

			unsigned px, py;
			if (!MapToPhysical(lx, ly, px, py))
				continue;

			u16 nPixel = pPixels[y * nSrcWidth + x];
			u8  nLuma  = Rgb565ToLuma(nPixel);

			unsigned nIdx = py * WIDTH_BYTES + px / 8u;
			unsigned nBit = 7u - (px % 8u);

			if (nLuma >= 128u) m_FrameBuffer[nIdx] |=  (u8)(1u << nBit);
			else               m_FrameBuffer[nIdx] &= ~(u8)(1u << nBit);
		}
	}
}
