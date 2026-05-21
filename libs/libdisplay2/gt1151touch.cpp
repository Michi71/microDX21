#include "gt1151touch.h"
#include <circle/timer.h>
#include <assert.h>
#include <circle/actled.h>

// GT1151 register map
#define GT1151_REG_PRODUCT_ID 0x8140u
#define GT1151_REG_STATUS     0x814Eu
#define GT1151_REG_POINTS     0x8150u

// Status register bit masks
#define GT1151_STATUS_BUF_VALID  0x80u
#define GT1151_STATUS_NPOINTS    0x0Fu

CGT1151TouchScreen::CGT1151TouchScreen (CI2CMaster *pI2CMaster,
                                        CGPIOManager *pGPIOManager,
                                        unsigned nResetPin,
                                        unsigned nIRQGpio,
                                        unsigned nWidth,
                                        unsigned nHeight,
                                        u8       nI2CAddress,
                                        unsigned nClockSpeed)
:	m_pI2CMaster (pI2CMaster),
	m_nWidth (nWidth),
	m_nHeight (nHeight),
	m_nI2CAddress (nI2CAddress),
	m_nClockSpeed (nClockSpeed),
	m_IRQPin (nIRQGpio, GPIOModeInputPullUp, pGPIOManager),
	m_bInterruptConnected (FALSE),
	m_ResetPin (nResetPin, GPIOModeOutput),
	m_nRotation (0),
	m_pDevice (nullptr),
	m_bActive (FALSE),
	m_nLastTicks (0),
	m_bFingerDown (FALSE)
{
	assert (pI2CMaster != 0);
	assert (pGPIOManager != 0);
}

CGT1151TouchScreen::~CGT1151TouchScreen (void)
{
	if (m_bInterruptConnected)
	{
		m_IRQPin.DisableInterrupt ();
		m_IRQPin.DisconnectInterrupt ();
	}

	delete m_pDevice;
	m_pDevice = nullptr;
}

void CGT1151TouchScreen::Reset (void)
{
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (100);
	m_ResetPin.Write (LOW);
	CTimer::SimpleMsDelay (100);
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (100);
}

void CGT1151TouchScreen::SetRotation (unsigned nDegrees)
{
	assert (nDegrees < 360 && nDegrees % 90 == 0);
	m_nRotation = nDegrees;
}

boolean CGT1151TouchScreen::Initialize (void)
{
	assert (m_pI2CMaster != 0);

	Reset ();

	// Verify I2C comm
	u8 productId[4] = {0};
	if (!ReadRegs (GT1151_REG_PRODUCT_ID, productId, sizeof (productId)))
	{
		return FALSE;
	}

	// Clear pending data
	WriteReg (GT1151_REG_STATUS, 0x00);

	// IRQ (active-low): falling edge
	m_IRQPin.ConnectInterrupt (GPIOInterruptHandler, this);
	m_IRQPin.EnableInterrupt (GPIOInterruptOnFallingEdge);
	m_bInterruptConnected = TRUE;

	// Create Circle touchscreen device; Update() will be called via device Update()
	assert (!m_pDevice);
	m_pDevice = new CTouchScreenDevice (UpdateStub, this);
	assert (m_pDevice);

	return TRUE;
}

/*boolean CGT1151TouchScreen::Read (int *pX, int *pY, boolean *pTouched)
{
	assert (pX != 0);
	assert (pY != 0);
	assert (pTouched != 0);

	*pTouched = FALSE;
	*pX = 0;
	*pY = 0;

	// Read a small block starting at 0x814E:
	// [0]=status (always 0 on your board)
	// [2]=event/trackid (0x80 when idle/released)
	// [4]=X low byte (0..255)
	// [6]=Y low byte (0..255)
	u8 b[10] = {0};
	if (!ReadRegs (GT1151_REG_STATUS, b, sizeof b))   // GT1151_REG_STATUS == 0x814E
	{
		return FALSE;
	}

	// Release / no touch (empirically from your dump)
	if (b[2] == 0x80)
	{
		*pTouched = FALSE;
		WriteReg (GT1151_REG_STATUS, 0x00);
		return TRUE;
	}

	unsigned rawX = (unsigned) b[4];   // 0..255
	unsigned rawY = (unsigned) b[6];   // 0..255

	// If axes feel mirrored, flip here (we can adjust after your next test):
	// rawX = 255u - rawX;
	// rawY = 255u - rawY;

	// Scale 0..255 to configured physical size
	u16 physX = (u16) ((rawX * (m_nWidth  - 1)) / 255u);
	u16 physY = (u16) ((rawY * (m_nHeight - 1)) / 255u);

	unsigned x = 0, y = 0;
	ApplyRotation (physX, physY, x, y);

	*pX = (int) x;
	*pY = (int) y;
	*pTouched = TRUE;

	WriteReg (GT1151_REG_STATUS, 0x00);
	return TRUE;
} */

boolean CGT1151TouchScreen::Read (int *pX, int *pY, boolean *pTouched)
{
	assert (pX != 0);
	assert (pY != 0);
	assert (pTouched != 0);

	*pTouched = FALSE;
	*pX = 0;
	*pY = 0;

	u8 b[10] = {0};
	if (!ReadRegs (GT1151_REG_STATUS, b, sizeof b)) 
	{
		return FALSE;
	}

	// Release / no touch (empirically from your dump)
	if (b[2] == 0x80)
	{
		*pTouched = FALSE;
		WriteReg (GT1151_REG_STATUS, 0x00);
		return TRUE;
	}

	u16 physX = ((u16)b[3] << 8) + b[2];
	u16 physY = ((u16)b[5] << 8) + b[4];

	unsigned x = 0, y = 0;

	ApplyRotation (physX, physY, x, y);

	*pX = (int) x;
	*pY = (int) y;
	*pTouched = TRUE;

	WriteReg (GT1151_REG_STATUS, 0x00);
	return TRUE;
}

void CGT1151TouchScreen::GPIOInterruptHandler (void *pParam)
{
	CGT1151TouchScreen *pThis = static_cast<CGT1151TouchScreen *> (pParam);
	assert (pThis != 0);

	pThis->m_bActive = TRUE;
}

void CGT1151TouchScreen::UpdateStub (void *pParam)
{
	CGT1151TouchScreen *pThis = static_cast<CGT1151TouchScreen *> (pParam);
	assert (pThis != 0);

	pThis->Update ();
}

static inline unsigned ClampU(unsigned v, unsigned maxIncl)
{
	return v > maxIncl ? maxIncl : v;
}

void CGT1151TouchScreen::ApplyRotation (u16 physX, u16 physY,
                                        unsigned &logicalX, unsigned &logicalY) const
{
	unsigned x = physX;
	unsigned y = physY;

	switch (m_nRotation)
	{
	default:
	case 0:
		// adjust if needed later
		break;

	case 90:
		x = (m_nHeight - 1) - physY;
		y = physX;
		break;

	case 180:
		x = (m_nWidth - 1) - physX;
		y = (m_nHeight - 1) - physY;
		break;

	case 270:
		x = physY;
		y = (m_nWidth - 1) - physX;
		break;
	}

	logicalX = x;
	logicalY = y;
}

void CGT1151TouchScreen::Update (void)
{
	// If we are driven by IRQ, m_bActive gets set in GPIOInterruptHandler().
	// While the finger is down, keep reading even if no more IRQs arrive.
	if (!m_bActive && !m_bFingerDown)
	{
		return;
	}

	// Rate limit (avoid hammering I2C / LVGL)
	unsigned nTicks = CTimer::GetClockTicks ();
	if (nTicks - m_nLastTicks < ThresholdMicros * (CLOCKHZ / 1000000))
	{
		return;
	}
	m_nLastTicks = nTicks;

	u8 b[10] = {0};
	if (!ReadRegs (GT1151_REG_STATUS, b, sizeof b))
	{
		// If we fail while finger was down, do a safe release so LVGL does not "stick".
		if (m_bFingerDown)
		{
			m_pDevice->ReportHandler (TouchScreenEventFingerUp, 0, 0, 0);
			m_bFingerDown = FALSE;
		}

		// Go idle until next IRQ
		m_bActive = FALSE;
		return;
	}

	// Release / no touch (as you determined)
	if (b[2] == 0x80)
	{
		if (m_bFingerDown)
		{
			m_pDevice->ReportHandler (TouchScreenEventFingerUp, 0, 0, 0);
			m_bFingerDown = FALSE;
		}

		WriteReg (GT1151_REG_STATUS, 0x00);

		// No finger: wait for next IRQ
		m_bActive = FALSE;
		return;
	}

	// Same decoding as your Read()
	u16 physX = ((u16) b[3] << 8) + b[2];
	u16 physY = ((u16) b[5] << 8) + b[4];

	unsigned x = 0, y = 0;
	ApplyRotation (physX, physY, x, y);

	if (!m_bFingerDown)
	{
		m_pDevice->ReportHandler (TouchScreenEventFingerDown, 0, x, y);
		m_bFingerDown = TRUE;
	}
	else
	{
		m_pDevice->ReportHandler (TouchScreenEventFingerMove, 0, x, y);
	}

	WriteReg (GT1151_REG_STATUS, 0x00);

	// Stay active while finger is down. Some controllers only fire IRQ on touch-down.
	m_bActive = TRUE;
}

/*boolean CGT1151TouchScreen::ReadRegs (u16 nRegAddr, u8 *pData, unsigned nLength)
{
	if (m_nClockSpeed)
	{
		m_pI2CMaster->SetClock (m_nClockSpeed);
	}

	// 16-bit reg address, MSB first
	u8 aAddr[2] = { (u8) (nRegAddr >> 8), (u8) (nRegAddr & 0xFF) };

	// Use repeated-start transaction (more compatible with some GT11xx firmwares)
	int nRead = m_pI2CMaster->WriteReadRepeatedStart (m_nI2CAddress,
	                                                 aAddr, sizeof(aAddr),
	                                                 pData, nLength);
	return nRead == (int) nLength;
}*/

boolean CGT1151TouchScreen::ReadRegs (u16 nRegAddr, u8 *pData, unsigned nLength)
{
    if (m_nClockSpeed)
    {
        m_pI2CMaster->SetClock (m_nClockSpeed);
    }

    u8 aAddr[2] = { (u8) (nRegAddr >> 8), (u8) (nRegAddr & 0xFF) };

    if (m_pI2CMaster->Write (m_nI2CAddress, aAddr, sizeof(aAddr)) != (int) sizeof(aAddr))
    {
        return FALSE;
    }

    int nRead = m_pI2CMaster->Read (m_nI2CAddress, pData, nLength);
    return nRead == (int) nLength;
}

boolean CGT1151TouchScreen::WriteRegs (u16 nRegAddr, const u8 *pData, unsigned nLength)
{
	assert (nLength <= 32);

	u8 aBuf[2 + 32];
	aBuf[0] = (u8) (nRegAddr >> 8);
	aBuf[1] = (u8) (nRegAddr & 0xFF);
	for (unsigned i = 0; i < nLength; i++)
	{
		aBuf[2 + i] = pData[i];
	}

	if (m_nClockSpeed)
	{
		m_pI2CMaster->SetClock (m_nClockSpeed);
	}

	return m_pI2CMaster->Write (m_nI2CAddress, aBuf, 2 + nLength) == (int) (2 + nLength);
}

boolean CGT1151TouchScreen::WriteReg (u16 nRegAddr, u8 nValue)
{
	return WriteRegs (nRegAddr, &nValue, 1);
}