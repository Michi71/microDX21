//
/// \file epd2in13v4display.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2021-2024  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _display_epd2in13v4display_h
#define _display_epd2in13v4display_h

#include <circle/display.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/types.h>

// Physical panel dimensions
#define EPD_2IN13_V4_WIDTH   122u
#define EPD_2IN13_V4_HEIGHT  250u

/// \brief Driver for Waveshare 2.13" e-Paper V4 (122x250, 1bpp) display
///
/// LVGL renders in RGB565; this driver converts each pixel to 1-bit via a
/// luma threshold and maintains a full-panel shadow buffer.  Partial refresh
/// is used for every SetArea() call after the initial base-image write.
class CEpd2in13V4Display : public CDisplay
{
public:
	enum TRotate : unsigned
	{
		Rotate0   = 0,
		Rotate90  = 90,
		Rotate180 = 180,
		Rotate270 = 270
	};
	/// \param pSPIMaster  Pointer to the SPI master
	/// \param nDCPin      GPIO number of the DC (data/command) pin
	/// \param nResetPin   GPIO number of the reset pin
	/// \param nBusyPin    GPIO number of the busy pin (input)
	/// \param nChipSelect SPI chip-select index (0 or 1)
	/// \param CPOL        SPI clock polarity (default 0)
	/// \param CPHA        SPI clock phase    (default 0)
	/// \param nClockSpeed SPI clock in Hz    (default 4 MHz)
	/// \note GPIO numbers are SoC BCM numbers, not header positions.
	CEpd2in13V4Display (CSPIMaster *pSPIMaster,
	                    unsigned nDCPin,
	                    unsigned nResetPin,
	                    unsigned nBusyPin,
	                    unsigned nChipSelect = 0,
	                    unsigned CPOL        = 0,
	                    unsigned CPHA        = 0,
	                    unsigned nClockSpeed = 4000000);

	~CEpd2in13V4Display (void);

	/// \return Display width in pixels
	unsigned GetWidth  (void) const override
	{
		return (m_nRotate == Rotate90 || m_nRotate == Rotate270)
		       ? EPD_2IN13_V4_HEIGHT
		       : EPD_2IN13_V4_WIDTH;
	}
	/// \return Display height in pixels
	unsigned GetHeight (void) const override
	{
		return (m_nRotate == Rotate90 || m_nRotate == Rotate270)
		       ? EPD_2IN13_V4_WIDTH
		       : EPD_2IN13_V4_HEIGHT;
	}
	/// \return Bits per pixel presented to LVGL (16 = RGB565)
	unsigned GetDepth  (void) const { return 16; }

	/// \brief Initialize hardware and load initial white base image
	/// \param nRotate  Image rotation
	/// \return TRUE on success
	boolean Initialize (unsigned nRotate = Rotate0);

	/// \brief No-op (EPD has no backlight)
	void On  (void) {}
	/// \brief Enter deep-sleep mode
	void Off (void) { Sleep (); }

	/// \brief Fill whole display
	/// \param bBlack TRUE → fill black, FALSE → fill white (default)
	void Clear (boolean bBlack = FALSE);

	/// \brief Set single pixel (updates shadow buffer, does NOT refresh)
	void SetPixel (unsigned nPosX, unsigned nPosY, TRawColor nColor);

	/// \brief Flush a rectangular region (LVGL RGB565 → 1bpp, partial refresh)
	/// \param rArea    Rectangle to update (zero-based display coordinates)
	/// \param pPixels  Row-major RGB565 pixel data for the rectangle
	/// \param pRoutine Optional completion callback (called after EPD is ready)
	/// \param pParam   User parameter forwarded to pRoutine
	void SetArea (const TArea &rArea, const void *pPixels,
	              TAreaCompletionRoutine *pRoutine = nullptr,
	              void *pParam = nullptr);

	/// \brief Re-send the current shadow buffer as a full refresh (base image)
	/// Call periodically to reset the EPD base and reduce ghosting.
	void RefreshBase (void);

	/// \brief Enter deep-sleep mode (panel power-down)
	void Sleep (void);

private:
	// ── EPD low-level protocol (Circle SPI, no DEV_Config dependency) ──────

	void EpdReset            (void);
	void EpdWaitBusy         (void);
	void EpdSendCommand      (u8 cmd);
	void EpdSendData         (u8 data);
	void EpdSendDataBuf      (const u8 *pData, size_t nLen);
	void EpdSetWindows       (u16 xStart, u16 yStart, u16 xEnd, u16 yEnd);
	void EpdSetCursor        (u16 x, u16 y);
	void EpdTurnOnDisplay    (void);
	void EpdTurnOnDisplayFast(void);
	void EpdTurnOnDisplayPartialWait (void);
	void EpdInitFull         (void);
	void EpdInitFast 		 (void);
	void EpdInitPart         (void);
	void EpdDisplayBase      (const u8 *pImage);
	void EpdDisplayBaseFast  (const u8 *pImage);
	void EpdDisplayPartialWait (const u8 *pImage);

	bool MapToPhysical(unsigned x, unsigned y, unsigned &outX, unsigned &outY) const;

	// ── SPI helper ──────────────────────────────────────────────────────────
	void SpiWrite (const void *pData, size_t nLen);

	// ── RGB565 → 1bpp helpers ───────────────────────────────────────────────
	static u8 Rgb565ToLuma (u16 nPixel);
	void UpdateFrameBuffer (const TArea &rArea, const u16 *pPixels);

private:
	CSPIMaster *m_pSPIMaster;
	unsigned    m_nChipSelect;
	unsigned    m_CPOL;
	unsigned    m_CPHA;
	unsigned    m_nClockSpeed;

	CGPIOPin    m_DCPin;
	CGPIOPin    m_ResetPin;
	CGPIOPin    m_BusyPin;

	unsigned 	m_nRotate = Rotate0;

	// Shadow frame buffer: WIDTH_BYTES columns × HEIGHT rows, 1bpp
	// Bit 7 (MSB) of each byte = leftmost pixel; 1 = white, 0 = black.
	static const unsigned WIDTH_BYTES = (EPD_2IN13_V4_WIDTH + 7) / 8; // 16
	u8 m_FrameBuffer[WIDTH_BYTES * EPD_2IN13_V4_HEIGHT];               // 4000 B

	boolean m_bBaseImageValid;
};

#endif
