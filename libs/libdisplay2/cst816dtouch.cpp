#include "cst816dtouch.h"
#include <circle/timer.h>
#include <assert.h>
#include <circle/actled.h>

#define CST816D_ID_REG 0xA7
#define CST816D_TOUCH_NUM_REG 0X02
#define CST816D_TOUCH_XH_REG  0x03
#define CST816D_TOUCH_XL_REG  0x04
#define CST816D_TOUCH_YH_REG  0x05
#define CST816D_TOUCH_YL_REG  0x06

CCST816DTouchScreen::CCST816DTouchScreen (CI2CMaster *pI2CMaster,
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

CCST816DTouchScreen::~CCST816DTouchScreen (void)
{
	if (m_bInterruptConnected)
	{
		m_IRQPin.DisableInterrupt ();
		m_IRQPin.DisconnectInterrupt ();
	}

	delete m_pDevice;
	m_pDevice = nullptr;
}

void CCST816DTouchScreen::Reset (void)
{
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (100);
	m_ResetPin.Write (LOW);
	CTimer::SimpleMsDelay (100);
	m_ResetPin.Write (HIGH);
	CTimer::SimpleMsDelay (100);
}

void CCST816DTouchScreen::SetRotation (unsigned nDegrees)
{
	assert (nDegrees < 360 && nDegrees % 90 == 0);
	m_nRotation = nDegrees;
}

boolean CCST816DTouchScreen::Initialize (void)
{
	assert (m_pI2CMaster != 0);

	Reset ();

	// Verify I2C comm
	u8 productId = 0;
	if (!ReadRegs (CST816D_ID_REG, &productId, 1))
	{
		return FALSE;
	}

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

void CCST816DTouchScreen::GPIOInterruptHandler (void *pParam)
{
	CCST816DTouchScreen *pThis = static_cast<CCST816DTouchScreen *> (pParam);
	assert (pThis != 0);

	pThis->m_bActive = TRUE;
}

void CCST816DTouchScreen::UpdateStub (void *pParam)
{
	CCST816DTouchScreen *pThis = static_cast<CCST816DTouchScreen *> (pParam);
	assert (pThis != 0);

	pThis->Update ();
}

void CCST816DTouchScreen::ApplyRotation (u16 physX, u16 physY,
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

void CCST816DTouchScreen::Update (void)
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

	u8 b[5] = {0};
	if (!ReadRegs (CST816D_TOUCH_NUM_REG, b, sizeof b))
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

	// Release / no touch
	if (b[0] == 0)
	{
		if (m_bFingerDown)
		{
			m_pDevice->ReportHandler (TouchScreenEventFingerUp, 0, 0, 0);
			m_bFingerDown = FALSE;
		}

		// No finger: wait for next IRQ
		m_bActive = FALSE;
		return;
	}

	// Same decoding as your Read()
	u16 physX = (((u16)b[1] & 0x0f) << 8) + b[2];
	u16 physY = (((u16)b[3] & 0x0f) << 8) + b[4];

	unsigned x = physX, y = physY;
	//ApplyRotation (physX, physY, x, y);

	if ((m_nWidth == 320) && (m_nHeight == 240) && (m_nRotation == 0))
	{
		x = physY;
		y = (m_nHeight - 1) - physX;
	}

	lastx = x;
	lasty = y;

	if (!m_bFingerDown)
	{
		m_pDevice->ReportHandler (TouchScreenEventFingerDown, 0, x, y);
		m_bFingerDown = TRUE;
	}
	else
	{
		m_pDevice->ReportHandler (TouchScreenEventFingerMove, 0, x, y);
	}

	// Stay active while finger is down. Some controllers only fire IRQ on touch-down.
	m_bActive = TRUE;
}

/*boolean CCST816DTouchScreen::ReadRegs (u16 nRegAddr, u8 *pData, unsigned nLength)
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

boolean CCST816DTouchScreen::ReadRegs (u16 nRegAddr, u8 *pData, unsigned nLength)
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

boolean CCST816DTouchScreen::WriteRegs (u16 nRegAddr, const u8 *pData, unsigned nLength)
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

boolean CCST816DTouchScreen::WriteReg (u16 nRegAddr, u8 nValue)
{
	return WriteRegs (nRegAddr, &nValue, 1);
}