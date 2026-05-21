#ifndef _input_gt1151touch_h
#define _input_gt1151touch_h

#include <circle/input/touchscreen.h>
#include <circle/i2cmaster.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/types.h>

class CGT1151TouchScreen
{
public:
	static const u8 DEFAULT_I2C_ADDRESS    = 0x14;
	static const unsigned DEFAULT_IRQ_GPIO = 27;	// Header pin 13

	CGT1151TouchScreen (CI2CMaster *pI2CMaster,
	                    CGPIOManager *pGPIOManager,
	                    unsigned nResetPin 		= 22,
	                    unsigned nIRQGpio 		= DEFAULT_IRQ_GPIO,
	                    unsigned nWidth   		= 122,
	                    unsigned nHeight  		= 250,
	                    u8       nI2CAddress 	= DEFAULT_I2C_ADDRESS,
	                    unsigned nClockSpeed 	= 400000);

	~CGT1151TouchScreen (void);

	void SetRotation (unsigned nDegrees);
	unsigned GetRotation (void) const { return m_nRotation; }

	boolean Initialize (void);

	// Polling API for debug / LVGL direct read style
	// Returns TRUE if I2C transfer succeeded. *pTouched tells state.
	boolean Read (int *pX, int *pY, boolean *pTouched);

	// This is the Circle touchscreen interface device ("touch1")
	CTouchScreenDevice *GetDevice (void) { return m_pDevice; }

	boolean DebugRead (u16 nRegAddr, u8 *pData, unsigned nLength) { return ReadRegs(nRegAddr, pData, nLength); }

private:
	void Reset (void);
	void Update (void);
	static void UpdateStub (void *pParam);
	static void GPIOInterruptHandler (void *pParam);

	boolean ReadRegs  (u16 nRegAddr, u8 *pData, unsigned nLength);
	boolean WriteRegs (u16 nRegAddr, const u8 *pData, unsigned nLength);
	boolean WriteReg  (u16 nRegAddr, u8 nValue);

	void ApplyRotation (u16 physX, u16 physY, unsigned &logicalX, unsigned &logicalY) const;

private:
	CI2CMaster *m_pI2CMaster;
	unsigned    m_nWidth;
	unsigned    m_nHeight;
	u8          m_nI2CAddress;
	unsigned    m_nClockSpeed;

	CGPIOPin    m_IRQPin;
	boolean     m_bInterruptConnected;

	CGPIOPin    m_ResetPin;

	unsigned    m_nRotation;

	CTouchScreenDevice *m_pDevice;

	volatile boolean m_bActive;
	unsigned m_nLastTicks;
	boolean m_bFingerDown;

private:
	static const unsigned ThresholdMicros = 12000;
};

#endif