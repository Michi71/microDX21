#ifndef _input_cst816dtouch_h
#define _input_cst816dtouch_h

#include <circle/input/touchscreen.h>
#include <circle/i2cmaster.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/types.h>

class CCST816DTouchScreen
{
public:
	static const u8 DEFAULT_RESET_GPIO     = 17;
	static const u8 DEFAULT_I2C_ADDRESS    = 0x15;
	static const unsigned DEFAULT_IRQ_GPIO = 4;

	CCST816DTouchScreen (CI2CMaster *pI2CMaster,
	                     CGPIOManager *pGPIOManager,
	                     unsigned nResetPin 	= DEFAULT_RESET_GPIO,
	                     unsigned nIRQGpio 		= DEFAULT_IRQ_GPIO,
	                     unsigned nWidth   		= 240,
	                     unsigned nHeight  		= 320,
	                     u8       nI2CAddress 	= DEFAULT_I2C_ADDRESS,
	                     unsigned nClockSpeed 	= 400000);

	~CCST816DTouchScreen (void);

	void SetRotation (unsigned nDegrees);
	unsigned GetRotation (void) const { return m_nRotation; }

	boolean Initialize (void);

	// This is the Circle touchscreen interface device ("touch1")
	CTouchScreenDevice *GetDevice (void) { return m_pDevice; }

	unsigned lastx = 0;
	unsigned lasty = 0;

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