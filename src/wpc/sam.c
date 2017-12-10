//#define INCLUDE_STERN_SAM
#ifdef INCLUDE_STERN_SAM

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "vpintf.h"
#include "cpu/at91/at91.h"
#include "sndbrd.h"

//#include "defs.h"

// Defines
#define SAM_DISPLAYSMOOTH 4

#define SAM1_USE_REAL_FREQ	1							// Use real cpu frequency //!! CSI,IJ4 needs this, but FG timers are off/too slow

#if SAM1_USE_REAL_FREQ
	#define SAM1_ARMCPU_FREQ	40000000				// 40 MHZ - Marked on the schematic
	#define SAM1_ZC_FREQ	104	//104 for 60hz //105 for 50hz // This produces a 60Hz zero cross detection for Family Guy RoHs detection
    #define SAM_IRQFREQ 4039 //4040 //!! experimental 4039 found by destruk
#else
    #define SAM1_ARMCPU_FREQ  55000000					// 55 MHZ - DMD Animations run closer to accurate
    #define SAM1_ZC_FREQ    146 //!! has changed since ALU changes in ARM7core //143 for 60hz //144 for 50hz // This produces a 60Hz zero cross detection for Family Guy RoHs detection
    #define SAM_IRQFREQ 4040
#endif

#define WAVE_OUT_RATE 24242
#define BUFFSIZE 262144

#define SAM_CPU	0
#define SAM_ROMBANK0 1

#define SAM_NOMINI	 0x00
#define SAM_MINIDMD  0x01
#define SAM_NOMINI2  0x02
#define SAM_MINIDMD3 0x04
#define SAM_NOMINI3	 0x08
#define SAM_NOMINI4	 0x10
#define SAM_NOMINI5	 0x20

#define SAM_0COL 0x00
#define SAM_2COL 0x02
#define SAM_3COL 0x03
#define SAM_8COL 0x08

// Variables
struct {
	int vblankCount;
	UINT32 solenoids;
	UINT32 solenoids2;
	int diagnosticLed;
	int col;
	int zc;
	data32_t ram1;
	data32_t ram2;
	INT16 samplebuf[BUFFSIZE];
	INT16 lastsamp;
	char unused2[2];
	int sampout;
	int sampnum;
	int msu[6];
	int powerswitch;
	INT16 bank;
	char minidata[226];
} samlocals;

static data32_t *sam_reset_ram;
static data32_t *sam_page0_ram;
static data32_t *sam_cpu;

static int sam_bank[47];

static int sam_stream = 0;

//SAM Input Ports
#define SAM_COMPORTS \
  PORT_START /* 0 */ \
    COREPORT_BITDEF(  0x0010, IPT_TILT,           KEYCODE_INSERT)  \
    COREPORT_BIT   (  0x0020, "Slam Tilt",        KEYCODE_HOME)  \
    COREPORT_BIT   (  0x0040, "Ticket Notch",     KEYCODE_K)  \
	COREPORT_BIT   (  0x0080, "Dedicated Sw#20",  KEYCODE_L) \
    COREPORT_BIT   (  0x0100, "Back",             KEYCODE_7) \
    COREPORT_BIT   (  0x0200, "Minus",            KEYCODE_8) \
    COREPORT_BIT   (  0x0400, "Plus",             KEYCODE_9) \
    COREPORT_BIT   (  0x0800, "Select",           KEYCODE_0) \
    COREPORT_BIT   (  0x8000, "Start Button",     KEYCODE_1) \
    COREPORT_BIT   (  0x4000, "Tournament Start", KEYCODE_2) \
    COREPORT_BITDEF(  0x0001, IPT_COIN1,          KEYCODE_3)  \
    COREPORT_BITDEF(  0x0002, IPT_COIN2,          KEYCODE_4)  \
    COREPORT_BITDEF(  0x0004, IPT_COIN3,          KEYCODE_5)  \
    COREPORT_BITDEF(  0x0008, IPT_COIN4,          KEYCODE_6)  \
    COREPORT_BITTOG(  0x1000, "Coin Door",        KEYCODE_END) \
  PORT_START /* 1 */ \
    COREPORT_DIPNAME( 0x001f, 0x0000, "Country") \
      COREPORT_DIPSET(0x0000, "USA" ) \
      COREPORT_DIPSET(0x000d, "Australia" ) \
      COREPORT_DIPSET(0x0001, "Austria" ) \
      COREPORT_DIPSET(0x0002, "Belgium" ) \
      COREPORT_DIPSET(0x0003, "Canada 1" ) \
      COREPORT_DIPSET(0x001a, "Canada 2" ) \
      COREPORT_DIPSET(0x0013, "Chuck E. Cheese" ) \
      COREPORT_DIPSET(0x0016, "Croatia" ) \
      COREPORT_DIPSET(0x0009, "Denmark" ) \
      COREPORT_DIPSET(0x0005, "Finland" ) \
      COREPORT_DIPSET(0x0006, "France" ) \
      COREPORT_DIPSET(0x0007, "Germany" ) \
      COREPORT_DIPSET(0x000f, "Greece" ) \
      COREPORT_DIPSET(0x0008, "Italy" ) \
      COREPORT_DIPSET(0x001b, "Lithuania" ) \
      COREPORT_DIPSET(0x0015, "Japan" ) \
      COREPORT_DIPSET(0x0017, "Middle East" ) \
      COREPORT_DIPSET(0x0004, "Netherlands" ) \
      COREPORT_DIPSET(0x0010, "New Zealand" ) \
      COREPORT_DIPSET(0x000a, "Norway" ) \
      COREPORT_DIPSET(0x0011, "Portugal" ) \
      COREPORT_DIPSET(0x0019, "Russia" ) \
      COREPORT_DIPSET(0x0014, "South Africa" ) \
      COREPORT_DIPSET(0x0012, "Spain" ) \
      COREPORT_DIPSET(0x000b, "Sweden" ) \
      COREPORT_DIPSET(0x000c, "Switzerland" ) \
      COREPORT_DIPSET(0x0018, "Taiwan" ) \
      COREPORT_DIPSET(0x000e, "U.K." ) \
      COREPORT_DIPSET(0x001c, "Unknown (00011100)" ) \
      COREPORT_DIPSET(0x001d, "Unknown (00011101)" ) \
      COREPORT_DIPSET(0x001e, "Unknown (00011110)" ) \
      COREPORT_DIPSET(0x001f, "Unknown (00011111)" ) \
	COREPORT_DIPNAME( 0x0020, 0x0000, "Dip #6") \
      COREPORT_DIPSET(0x0000, "0" ) \
      COREPORT_DIPSET(0x0020, "1" ) \
	COREPORT_DIPNAME( 0x0040, 0x0000, "Dip #7") \
      COREPORT_DIPSET(0x0000, "0" ) \
      COREPORT_DIPSET(0x0040, "1" ) \
	COREPORT_DIPNAME( 0x0080, 0x0080, "Dip #8") \
      COREPORT_DIPSET(0x0000, "1" ) \
      COREPORT_DIPSET(0x0080, "0" )

#define SAM_INPUT_PORTS_START(name,balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls) \
    SAM_COMPORTS

#define SAM_INPUT_PORTS_END INPUT_PORTS_END
#define SAM_COMINPORT       CORE_COREINPORT

//Sound Interface
const struct sndbrdIntf samIntf = {
	"SAM1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, SNDBRD_NODATASYNC
};

static void sam_sh_update(int num, INT16 *buffer, int length)
{
	int ii;

	ii = 0;
	if ( length > 0 )
	{
		while ( samlocals.sampout != samlocals.sampnum && samlocals.sampnum >= 64 )
		{
			buffer[ii] = samlocals.samplebuf[samlocals.sampout];
			samlocals.sampout = (samlocals.sampout + 1) % BUFFSIZE;
			samlocals.lastsamp = buffer[ii++];
			if ( ii >= length )
				return;
		}
		for ( ; ii < length; ++ii )
			buffer[ii] = samlocals.lastsamp;
	}
}

int sam_sh_start(const struct MachineSound *msound)
{
	char stream_name[40];

	sprintf(stream_name, "%s", "SAM1");
	sam_stream = stream_init(stream_name, 100, WAVE_OUT_RATE*2, 0, sam_sh_update);
	return sam_stream < 0;
}

void sam_sh_stop(void)
{
}

static struct CustomSound_interface samCustInt =
{
	sam_sh_start,
	sam_sh_stop,
	0,
};

//Memory Reads
//Complete - Needs cleanup
static READ32_HANDLER(samswitch_r)
{
	data32_t data; // eax@1
	int v5; // edi@1
	int ii; // esi@1
	int v7; // esi@6

	data = 0;
	v5 = 0;
	ii = 0;
	while ( !((0xFF << (ii * 8)) & ~mem_mask) )
	{
		ii++;
		if ( ii >= 4 )
			goto LABEL_6;
	}
	v5 = ii;
LABEL_6:
	v7 = v5 + 4 * offset;
	if ( v7 >= 0 && v7 <= 6)
    {
		switch ( v7 )
		{
			case 0:
				data = (coreGlobals.swMatrix[2 * samlocals.col + 1] | coreGlobals.swMatrix[2 * samlocals.col + 2] << 8) ^ 0xFFFF;
				break;
			case 2:
				data = (coreGlobals.swMatrix[9] | ((core_swapNyb[coreGlobals.swMatrix[11] & 0xF] | (0x10 * core_swapNyb[coreGlobals.swMatrix[11] >> 4])) << 8)) ^ 0xFFFF;
				break;
			case 4:
				if ( ~mem_mask == 0xFF00 )
					data = (core_getDip(0) ^ 0xFF) << 8;
				else
					data = (coreGlobals.swMatrix[0] | (core_getDip(0) << 8)) ^ 0xFFFF;
				break;
			case 5:
				data = core_getDip(0) ^ 0xFF;
				break;
			default:
				logerror("error");
				break;
		}
	}
	if ( v7 == 0x80000 )
		data = samlocals.bank;
	return data << 8 * v5;
}

static READ32_HANDLER(samxilinx_r)
{
	data32_t data = 0;
	if(~mem_mask == 0xFF00)
		// was ((6 | zc) << 2) | u1...   bits:   0 1 1 zc u1 u1
		//                     works     bits:   0 0 0 zc u1 u1
		//                                                u1 = solenoids have power - 1-32
		//                                                   u1 = power switch
		data = (( /*0*/7 << 3 ) | (samlocals.zc << 2) | (samlocals.powerswitch != 0 ? 3 : 0)) << 8;
	else
		return data;
	return data;
}

static READ32_HANDLER(samcpu_r)
{
	if (offset == 2104)
		return 0;
	if (offset == 2105)
		return 4294967295;

	if ( offset < 0x4000 )
		return sam_cpu[offset];
	if ( offset != 0x4240 
		&& offset != 0x4414
		&& offset != 0x441a
		&& offset != 0x4420
		&& offset != 0x4426)
		return sam_cpu[offset];
	return sam_cpu[offset];
	switch( offset )
	{
		case 0x4414:
		case 0x441a:
		case 0x4420:
		case 0x4426:
			//sam_cpu[offset] = 0x00000001;
			return sam_cpu[offset];
		
		case 0x4021:
			//sam_cpu[offset] = 0x00000000;
			return sam_cpu[offset];

		case 0x4022:
			//sam_cpu[offset] = 0xFFFF0000;
			return sam_cpu[offset];

		case 0x4035:
			//sam_cpu[offset] = 0xFFFF0022;
			return sam_cpu[offset];
		//Language
		case 0x4036:
			//sam_cpu[offset] = 0xFFFE0100;
			return sam_cpu[offset];
		//Time?
		case 0x4037:
			//sam_cpu[offset] = 0x34352345;
			return sam_cpu[offset];

		//Key pressed
		case 0x4041:
		case 0x408d:
			logerror("Test");
			break;

		//Should be 01
		case 0x4240:
			break;

		case 0x4402:
			//sam_cpu[offset] = 0x00000001;
			break;

		default:
			//logerror("Test");
			break;
	}
	return sam_cpu[offset];
}

static MEMORY_READ32_START(sam_readmem)
	{ 0x00000000, 0x000FFFFF, MRA32_RAM },
	{ 0x00300000, 0x003FFFFF, MRA32_RAM },
	{ 0x01000000, 0x0107FFFF, MRA32_RAM },
	{ 0x01080000, 0x0109EFFF, MRA32_RAM },
	{ 0x0109F000, 0x010FFFFF, MRA32_RAM },
	{ 0x01100000, 0x011FFFFF, samswitch_r },
	{ 0x02000000, 0x020FFFFF, MRA32_RAM },
    { 0x02100000, 0x0211FFFF, MRA32_RAM }, //samcpu_r },
	{ 0x02400024, 0x02400027, samxilinx_r },
	{ 0x03000000, 0x030000FF, MRA32_RAM },
	{ 0x04000000, 0x047FFFFF, MRA32_RAM },  //U44?
	{ 0x04800000, 0x04FFFFFF, MRA32_BANK1 },  //U45?
	{ 0x05000000, 0x057FFFFF, MRA32_RAM },
	{ 0x05800000, 0x05FFFFFF, MRA32_BANK1 },
	{ 0x06000000, 0x067FFFFF, MRA32_RAM },
	{ 0x06800000, 0x06FFFFFF, MRA32_BANK1 },
	{ 0x07000000, 0x077FFFFF, MRA32_RAM },
	{ 0x07800000, 0x07FFFFFF, MRA32_BANK1 },
MEMORY_END

//Memory Writes
static data32_t prev_data;
static WRITE32_HANDLER(samxilinx_w)
{
	if(~mem_mask & 0xFFFF0000)
		data >>= 16;
	prev_data = data;
	samlocals.samplebuf[samlocals.sampnum] = data;
	samlocals.sampnum = (samlocals.sampnum + 1) % BUFFSIZE;
}

static WRITE32_HANDLER(samdmdram_w)
{
	int ii = 0;
	
	if ( offset == 2 )
	{
	    int result = data;
		samlocals.col = 0;
	    do
		{
			if ( data & (1 << ii) )
			samlocals.col += ii;
			result >>= 1;
			ii++;
		}
		while ( data >> ii );
	}
	else if ( offset == 8 )
	{
		if ( ~mem_mask & 0xFFFF0000 )
			samlocals.ram2 = data >> 16;
		else
			samlocals.ram1 = data;
    }
    else if ( offset != 0x2AAA0 )
        logerror("error");
}

static int sam_led(UINT32 bank)
{
	int result;
	int ii;

	result = 0;
	ii = 0;
	do
	{
		if ( bank & 1 )
			result += ii;
		bank >>= 1;
		ii++;
	}
	while ( bank );
	return result;
}

static int led[] =
	{ 5, 8, 7, 6, 9, 1, 2, 3, 4, 0, 10, 11, 12, 13};
static char byte_105E0AF8;
static int dword_105E0B00;
static int dword_105E0B04;
static char byte_105E0AF9;
static int dword_105E0AFC;
static int dword_105E0AF4;

static WRITE32_HANDLER(sambank_w)
{
	int v24 = 0;
	int v25 = 0;
	int ram;
	int bank;

	while ( !((0xFF << (v25 * 8)) & ~mem_mask) )
	{
		v25++;
		if ( v25 >= 4 )
			goto LABEL_6;
	}
	v24 = v25;
LABEL_6:
	ram = v24 + 4 * offset + 0x2400000;
	bank = data >> 8 * v24;
	if ( ram > 0x24000FF )
	{
		switch ( ram )
		{
			case 0x2500000:
				samlocals.diagnosticLed = (bank & 1) | samlocals.diagnosticLed & 2;
				break;
			case 0x2580000:
				//was bank & 0x3
				cpu_setbank(SAM_ROMBANK0, memory_region(154) + ((bank & 0xf) << 23) );
				samlocals.bank = (bank & 0xf);
				break;
			case 0x2F00000:
				if ( !bank )
					samlocals.diagnosticLed = samlocals.diagnosticLed & 1 | 2;
				break;
			default:
				logerror("error");
				break;
		}
	}
	else
	{
		switch ( ram - 0x2400020 )
		{
			int ii;
			case 0:
				sam_bank[0]++;
				if ( sam_bank[0] == 1 )
				{
					samlocals.solenoids &= 0xFFFF00FF;
					for(ii = 0; ii <= 7; ii++)
					{
						if ( bank & (1<<ii) )
							sam_bank[14 + ii] = 0x19;
						if( sam_bank[14 + ii] > 0)
						{
							sam_bank[14 + ii]--;
							samlocals.solenoids |= ((sam_bank[14 + ii] > 0) << (8 + ii));
						}
					}
					//samlocals.solenoids |= (bank << 8);
				}
				return;
			case 1:
				sam_bank[1]++;
				if ( sam_bank[1] == 1 )
				{
					samlocals.solenoids &= 0xFFFFFF00;
					for(ii = 0; ii <= 7; ii++)
					{
						if ( bank & (1<<ii) )
							sam_bank[6 + ii] = 0x19;
						if( sam_bank[6 + ii] > 0)
						{
							sam_bank[6 + ii]--;
							samlocals.solenoids |= ((sam_bank[6 + ii] > 0) << ii);
						}
					}
					//samlocals.solenoids |= bank;
				}
				return;
			case 2:
				sam_bank[2]++;
				if ( sam_bank[2] == 1 )
				{
					samlocals.solenoids &= 0xFF00FFFF;
					for(ii = 0; ii <= 7; ii++)
					{
						if ( bank & (1<<ii) )
							sam_bank[22 + ii] = 0x19;
						if( sam_bank[22 + ii] > 0)
						{
							sam_bank[22 + ii]--;
							samlocals.solenoids |= ((sam_bank[22 + ii] > 0) << (16 + ii));
						}
					}
					//samlocals.solenoids |= (bank << 16);
				}
				return;
			case 3:
				sam_bank[3]++;
				if ( sam_bank[3] == 1 )
				{
					samlocals.solenoids &= 0x00FFFFFF;
					for(ii = 0; ii <= 7; ii++)
					{
						if ( bank & (1<<ii) )
							sam_bank[30 + ii] = 0x19;
						if( sam_bank[30 + ii] > 0)
						{
							sam_bank[30 + ii]--;
							samlocals.solenoids |= ((sam_bank[30 + ii] > 0) << (24 + ii));
						}
					}
					//samlocals.solenoids |= (bank << 24);
				}
				return;
			case 6:
				sam_bank[4]++;
				if ( sam_bank[4] == 1 && bank >= 0)
				{
					samlocals.solenoids2 &= 0xFFFFF00F;
					for(ii = 0; ii <= 7; ii++)
					{
						if ( bank & (1<<ii) )
							sam_bank[38 + ii] = 0x19;
						if( sam_bank[38 + ii] > 0)
						{
							sam_bank[38 + ii]--;
							samlocals.solenoids2 |= ((sam_bank[38 + ii] > 0) << (4 + ii));
						}
					}
					//samlocals.solenoids2 |= (bank << 4);
				}
				if ( core_gameData->hw.gameSpecific1 & 1 )
				{
					if ( dword_105E0B00 == 1 )
					{
						dword_105E0B04 = led[sam_led(bank & 0x7F | ((byte_105E0AF9 & 0x7F) << 7))];
					}
					else
					{
						if ( dword_105E0B00 > 1 )
							samlocals.minidata[16 * dword_105E0B04 + dword_105E0B00] = bank & 0x7F;
						if ( dword_105E0B00 >= 17 )
						{
LABEL_157:
							if ( (~byte_105E0AF9 & bank) >= 0x80)
							{
								byte_105E0AF9 = bank;
								dword_105E0B00 = 0;
								return;
							}
							goto LABEL_176;
						}
					}
					dword_105E0B00++;
					goto LABEL_157;
				}
				if ( core_gameData->hw.gameSpecific1 & 2 )
				{
					int test;
					test = bank & ~byte_105E0AF9;
					if (!((test < 0x80) && (test >= 0)))
					{
						dword_105E0B00 = 0;
						byte_105E0AF9 = bank;
						dword_105E0B04 = sam_led(bank & 3);
						return;
					}
					if ( dword_105E0B00 < 3 )
					{
			            coreGlobals.tmpLampMatrix[dword_105E0B04 + dword_105E0B00 + 2 * dword_105E0B04 + 10] = bank & 0x7F;
						byte_105E0AF9 = bank;
			            coreGlobals.lampMatrix[dword_105E0B04 + dword_105E0B00 + 2 * dword_105E0B04 + 10] = bank & 0x7F;
						dword_105E0B00++;
			            return;
					}
				}
				if ( core_gameData->hw.gameSpecific1 & 4 )
				{
					int test;
					test = bank & ~byte_105E0AF9;
					if ( (test < 0x80) && (test >= 0) )
					{
						dword_105E0B00++;
					}
					else
					{
						dword_105E0AFC = dword_105E0AFC == 0;
						dword_105E0B00 = 0;
					}
					if ( dword_105E0AFC )
					{
						if ( dword_105E0B00 == 1 )
						{
							byte_105E0AF9 = bank;
							dword_105E0B04 = sam_led(bank & 0x1F);
							return;
						}
						if ( dword_105E0B00 > 1 && dword_105E0B00 < 7 )
						{
							byte_105E0AF9 = bank;
							samlocals.minidata[16 * dword_105E0B04 + dword_105E0B00] = bank & 0x7F;
							return;
						}
					}
					else
					{
						if ( dword_105E0B00 < 6 )
						{
							coreGlobals.tmpLampMatrix[dword_105E0B00 + 10] = bank & 0x7F;
							coreGlobals.lampMatrix[dword_105E0B00 + 10] = bank & 0x7F;
						}
					}
				}
LABEL_176:
				byte_105E0AF9 = bank;
				break;
			case 8:
				sam_bank[0] = 0;
				sam_bank[1] = 0;
				sam_bank[2] = 0;
				sam_bank[3] = 0;
				sam_bank[4] = 0;
				sam_bank[5] = 0;
				sam_bank[46]++;
				if ( sam_bank[46] == 2 )
					dword_105E0AF4 = sam_led(bank);
				return;
			case 10:
				sam_bank[46] = 0;
				sam_bank[5]++;
				if ( sam_bank[5] == 1 )
					coreGlobals.tmpLampMatrix[dword_105E0AF4] = core_revbyte(bank);
				return;
			case 11:
				if ( core_gameData->hw.gameSpecific1 & SAM_MINIDMD3 )
				{
					if ( (bank & ~byte_105E0AF8) & 8 )
						dword_105E0AFC = 0;
					else if ( (bank & ~byte_105E0AF8) & 0x10 )
						dword_105E0AFC = 1;
				}
				if ( core_gameData->hw.gameSpecific1 & SAM_NOMINI3 && ~bank & 0x08 )
					coreGlobals.lampMatrix[10] = coreGlobals.tmpLampMatrix[10] = core_revbyte(byte_105E0AF9);
				if ( core_gameData->hw.gameSpecific1 & SAM_NOMINI4 && ~bank & 0x10 )
					coreGlobals.lampMatrix[10] = coreGlobals.tmpLampMatrix[10] = core_revbyte(byte_105E0AF9);
				if ( core_gameData->hw.gameSpecific1 & SAM_NOMINI5 )
				{
					if ( ~bank & 0x08 )
						coreGlobals.lampMatrix[8] = coreGlobals.tmpLampMatrix[8] = core_revbyte(byte_105E0AF9);
					if ( ~bank & 0x10 )
						coreGlobals.lampMatrix[9] = coreGlobals.tmpLampMatrix[9] = core_revbyte(byte_105E0AF9);
					if ( ~bank & 0x20 )
						coreGlobals.lampMatrix[10] = coreGlobals.tmpLampMatrix[10] = core_revbyte(byte_105E0AF9);
					if ( ~bank & 0x40 )
						logerror("Test");
				}
		        if ( (~bank & 0x40) && (byte_105E0AF9 & 3) )
					logerror("error");
				byte_105E0AF8 = bank;
				return;
			 default:
				logerror("error");
		}
	}
}

static WRITE32_HANDLER(samcpu_w)
{
	switch( offset )
	{
		//clock??
		case 0x4021:
		case 0x4023:
		case 0x4025:
		case 0x4027:
		case 0x4029:
		case 0x402b:
		case 0x402d:
		case 0x402f:
		case 0x4031:
		case 0x4033:
			//sam_cpu[offset] = 0x00000000;
			//logerror("Test");
			return;
		//clock??
		case 0x4022:
		case 0x4024:
		case 0x4026:
		case 0x4028:
		case 0x402a:
		case 0x402c:
		case 0x402e:
		case 0x4030:
		case 0x4032:
		case 0x4034:
			//sam_cpu[offset] = 0xFFFF0000;
			//logerror("Test");
			return;

		case 0x4037:
			logerror("Test");
			return;

		case 0x4041:
		case 0x408d:
			logerror("Test");
			break;

		case 0x4672:
		case 0x4674:
			logerror("Test");
			break;

		case 0x4634:
		case 0x4636:
			logerror("Test");
			break;

		default:
			logerror("Test");
			break;
	}
	sam_cpu[offset] = (sam_cpu[offset] & mem_mask) | (data & ~mem_mask);
}

static MEMORY_WRITE32_START(sam_writemem)
	{ 0x00000000, 0x000FFFFF, MWA32_RAM, &sam_page0_ram},  // Boot RAM
	{ 0x00300000, 0x003FFFFF, MWA32_RAM, &sam_reset_ram},  // Swapped RAM
	{ 0x01000000, 0x0107FFFF, MWA32_RAM },
	{ 0x01080000, 0x0109EFFF, MWA32_RAM },
	{ 0x0109F000, 0x010FFFFF, samxilinx_w },
	{ 0x01100000, 0x01FFFFFF, samdmdram_w },
	{ 0x02100000, 0x0211FFFF, MWA32_RAM, &sam_cpu },
	{ 0x02400000, 0x02FFFFFF, sambank_w },
	{ 0x03000000, 0x030000FF, MWA32_RAM },
	{ 0x04000000, 0x047FFFFF, MWA32_RAM },
	{ 0x04800000, 0x04FFFFFF, MWA32_RAM },
	{ 0x05000000, 0x057FFFFF, MWA32_RAM },
	{ 0x05800000, 0x05FFFFFF, MWA32_RAM },
	{ 0x06000000, 0x067FFFFF, MWA32_RAM },
	{ 0x06800000, 0x06FFFFFF, MWA32_RAM },
	{ 0x07000000, 0x077FFFFF, MWA32_RAM },
	{ 0x07800000, 0x07FFFFFF, MWA32_RAM },
MEMORY_END

//Ports
static READ32_HANDLER(sam_port_r)
{
	data32_t data;
	data = 3072;
	return data;
}

static WRITE32_HANDLER(sam_port_w)
{
	double l_vol = 0.0;
	double r_vol = 0.0;

	if ( data & 0x10 )
	{
		if ( samlocals.msu[5] >= 0 )
		{
			samlocals.msu[4] |= ((data >> 5) & 1) << samlocals.msu[5];
			samlocals.msu[5]--;
		}
	}
	if ( data & 8 )
	{
		switch ( samlocals.msu[4] >> 8 )
		{
			case 0x10:
				samlocals.msu[0] = samlocals.msu[4];
				break;
			case 0x11:
				samlocals.msu[1] = samlocals.msu[4];
				break;
			case 0x12:
				samlocals.msu[2] = samlocals.msu[3] = (samlocals.msu[4] & 1);
				break;
		}
		if (samlocals.msu[2] == 0 )
			l_vol = (samlocals.msu[0] & 0x7F) * 0.78125;
		if (samlocals.msu[3] == 0 )
			r_vol = (samlocals.msu[0] & 0x7F) * 0.78125;
		mixer_set_stereo_volume(0, l_vol, r_vol);
		samlocals.msu[5] = 16;
		samlocals.msu[4] = 0;
	}
}

static PORT_READ32_START(sam_readport)
	{ 0x00, 0xFF, sam_port_r },
PORT_END

static PORT_WRITE32_START(sam_writeport)
	{ 0x00, 0xFF, sam_port_w },
PORT_END

//Machine
static MACHINE_INIT(sam) {
	at91_set_ram_pointers(sam_reset_ram, sam_page0_ram);
}

static MACHINE_RESET(sam) {
	memset(&samlocals, 0, sizeof(samlocals));
	samlocals.msu[5] = 16;
	samlocals.powerswitch = 1;
}

static SWITCH_UPDATE(sam) {
	if (inports) {
		// 1111      .... checking 0x000X and putting into column 9
		CORE_SETKEYSW(inports[SAM_COMINPORT], 0xF, 9);

		// 1111 1111 .... checking 0x0XX0 and putting those into column 0
		CORE_SETKEYSW(inports[SAM_COMINPORT]>>4, 0xFF, 0);

		// 1100 0000 .... checking 0x8000 and 0x4000 and setting switch column 2
		CORE_SETKEYSW(inports[SAM_COMINPORT]>>8, 0xC0, 2);

		// 0x1000(coin door) >> 12
		samlocals.powerswitch = ~(inports[SAM_COMINPORT]>>12) & 0x01;
	}
}

static INTERRUPT_GEN(sam_vblank) {
	samlocals.vblankCount += 1;
	memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, 40);
	if ((samlocals.vblankCount % SAM_DISPLAYSMOOTH) == 0) {
	    coreGlobals.diagnosticLed = samlocals.diagnosticLed;
		samlocals.diagnosticLed = 0;
	}
	coreGlobals.solenoids = samlocals.solenoids;
	coreGlobals.solenoids2 = samlocals.solenoids2;
	core_updateSw(TRUE);
}

static NVRAM_HANDLER(sam) {
	core_nvram(file, read_or_write, sam_cpu, 0x20000, 0xff);
}

static void sam_timer(int data)
{
	samlocals.zc = (samlocals.zc == 0);
}

static INTERRUPT_GEN(sam_irq)
{  
	cpu_set_irq_line(SAM_CPU, 0, PULSE_LINE);
}

static MACHINE_DRIVER_START(sam)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_SWITCH_UPDATE(sam)
	MDRV_CPU_ADD(AT91, SAM1_ARMCPU_FREQ)
	MDRV_CPU_MEMORY(sam_readmem, sam_writemem)
	MDRV_CPU_PORTS(sam_readport, sam_writeport)
	MDRV_CPU_VBLANK_INT(sam_vblank, 1)
	MDRV_CPU_PERIODIC_INT(sam_irq, SAM_IRQFREQ)
	MDRV_CORE_INIT_RESET_STOP(sam, sam, NULL)
	MDRV_DIPS(8)
	MDRV_NVRAM_HANDLER(sam)
	MDRV_TIMER_ADD(sam_timer, SAM1_ZC_FREQ)
	MDRV_SOUND_ADD(CUSTOM, samCustInt)
	MDRV_DIAGNOSTIC_LEDH(2)
MACHINE_DRIVER_END

#define INITGAME(name, gen, disp, lampcol, hw) \
	static core_tGameData name##GameData = { \
		gen, disp, {FLIP_SW(FLIP_L) | FLIP_SOL(FLIP_L), 0, lampcol, 0, 0, 0, hw}}; \
	static void init_##name(void) { core_gameData = &name##GameData; }

//Memory Regions
#define SAM_CPU1REGION	REGION_CPU1
#define SAM_CPU2REGION	REGION_CPU2
#define SAM_ROMREGION	REGION_USER1

#define SAM_ROMLOAD_BOOT(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x01FFFFFF, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x05000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x02000000, 0x00100000)

#define SAM_ROMLOAD(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x03FFFFFF, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x07000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000)

#define SAM_ROMLOAD64(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x04FFFFFF, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x07800000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000)

#define SAM_ROMLOAD128(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x077FFFFF, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x0A800000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x09000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x09800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x0A000000, 0x00800000)

#define SAM_ROMEND ROM_END

//Displays
static UINT8 hew[16] =
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
//	{ 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15};
PINMAME_VIDEO_UPDATE(samdmd_update) {

	UINT8 *RAM1;
	UINT8 *RAM2;

	UINT8 temp;

	int ii, jj;
	tDMDDot dotCol = {{0}};

	for( ii = 1; ii <= 32; ii++ )
	{
		UINT8 *line = &dotCol[ii][0];
		for( jj = 0; jj < 128; jj++ )
		{
			RAM1 = memory_region(129) + 0x1080000 + (samlocals.ram1 * 0x1000) + ((ii - 1)*128) + jj;
			RAM2 = memory_region(129) + 0x1080000 + (samlocals.ram2 * 0x1000) + ((ii - 1)*128) + jj;
			temp = *RAM1;
			if ((*RAM1 & 0xF0) == 0xF0)
				temp = *RAM2;
			*line = hew[temp];
			*line++;
		}
	}

	video_update_core_dmd(bitmap, cliprect, dotCol, layout);
	return 0;
}

PINMAME_VIDEO_UPDATE(samminidmd_update) {
	tDMDDot dotCol;
	int ii,kk,bits;
	int dmd_x = (layout->left-10)/7;
	int dmd_y = (layout->top-34)/9;

	for (ii = 0, bits = 0x40; ii < 7; ii++, bits >>= 1) 
		for (kk = 0; kk < 5; kk++)
			dotCol[ii+1][kk] = samlocals.minidata[(dmd_y * 0x50) + (kk << 4) + dmd_x + 2] & bits ? 3 : 0;

	for (ii = 0; ii < 5; ii++) {
		bits = 0;
		for (kk = 0; kk < 7; kk++)
			bits = (bits<<1) | (dotCol[kk+1][ii] ? 1 : 0);
		coreGlobals.drawSeg[5*dmd_x + 35*dmd_y + ii] = bits;
	}

	if (!pmoptions.dmd_only)
		video_update_core_dmd(bitmap, cliprect, dotCol, layout);
	return 0;
}

PINMAME_VIDEO_UPDATE(samminidmd2_update) {
	tDMDDot dotCol;
	int ii,jj,kk,bits;

	for (jj = 0; jj < 5; jj++)
		for (ii = 0, bits = 0x40; ii < 7; ii++, bits >>= 1) 
			for (kk = 0; kk < 5; kk++)
				dotCol[kk+1][ii+(jj*7)] = samlocals.minidata[(kk << 4) + jj + 2] & bits ? 3 : 0;

	for (ii = 0; ii < 35; ii++) {
		bits = 0;
		for (kk = 0; kk < 5; kk++)
			bits = (bits<<1) | (dotCol[kk+1][ii] ? 1 : 0);
		coreGlobals.drawSeg[ii] = bits;
	}

	if (!pmoptions.dmd_only)
		video_update_core_dmd(bitmap, cliprect, dotCol, layout);
	return 0;
}

static struct core_dispLayout sam_dmd128x32[] = {
	{0, 0, 32, 128, CORE_DMD|CORE_DMDNOAA, (void *)samdmd_update},
	{0}
};

static struct core_dispLayout sammini1_dmd128x32[] = {
	DISP_SEG_IMPORT(sam_dmd128x32),
	{34, 10, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 17, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 24, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 31, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 38, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 45, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{34, 52, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 10, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 17, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 24, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 31, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 38, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 45, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{43, 52, 7, 5, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd_update},
	{0}
};

static struct core_dispLayout sammini2_dmd128x32[] = {
	DISP_SEG_IMPORT(sam_dmd128x32),
	{34, 10, 5, 35, CORE_DMD|CORE_DMDNOAA, (void *)samminidmd2_update},
	{0}
};

//Games
//Boot Flash - complete
INITGAME(sam1_flashb, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD_BOOT(sam1_flashb_0102, "boot_102.bin", CRC(92c93cba) SHA1(aed7ba2f988df8c95e2ad08f70409152d5caa49a), 0x00100000)
SAM_ROMEND
SAM_ROMLOAD_BOOT(sam1_flashb_0106, "boot_106.bin", CRC(fe7bcece) SHA1(775590bbd52c24950db86cc231566ba3780030d8), 0x000e8ac8)
SAM_ROMEND
SAM_ROMLOAD_BOOT(sam1_flashb_0210, "boot_210.bin", CRC(0f3fd4a4) SHA1(115d0b73c40fcdb2d202a0a9065472d216ca89e0), 0x000f0304)
SAM_ROMEND
SAM_ROMLOAD_BOOT(sam1_flashb_0230, "boot_230.bin", CRC(a4258c49) SHA1(d865edf7d1c6d2c922980dd192222dc24bc092a0), 0x000f0624)
SAM_ROMEND
SAM_ROMLOAD_BOOT(sam1_flashb_0310, "boot_310.bin", CRC(de017f82) SHA1(e4a9a818fa3f1754374cd00b52b8a087d6c442a9), 0x00100000)
SAM_ROMEND

SAM_INPUT_PORTS_START(sam1_flashb, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(sam1_flashb, 0102, "S.A.M Boot Flash Update (V1.02)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(sam1_flashb, 0106, 0102, "S.A.M System Flash Boot (V1.06)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(sam1_flashb, 0210, 0102, "S.A.M System Flash Boot (V2.10)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sam1_flashb, 0230, 0102, "S.A.M System Flash Boot (V2.30)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sam1_flashb, 0310, 0102, "S.A.M System Flash Boot (V3.10)", 2008, "Stern", sam, 0)

//World Poker Tour - complete
INITGAME(wpt, GEN_SAM, sammini1_dmd128x32, SAM_2COL, SAM_MINIDMD);

SAM_ROMLOAD(wpt_103a, "wpt0103a.bin", CRC(cd5f80bc) SHA1(4aaab2bf6b744e1a3c3509dc9dd2416ff3320cdb), 0x019bb1dc)
SAM_ROMEND

SAM_ROMLOAD(wpt_105a, "wpt0105a.bin", CRC(51608819) SHA1(a14aa47bdbce1dc958504d866ac963b06cd93bef), 0x019bb198)
SAM_ROMEND

SAM_ROMLOAD(wpt_106a, "wpt0106a.bin", CRC(72fd2e58) SHA1(3e910b964d0dc67fd538c027b474b3587b216ce5), 0x019bb198)
SAM_ROMEND
SAM_ROMLOAD(wpt_106f, "wpt0106f.bin", CRC(efa3eeb9) SHA1(a5260511b6325917a9076bac6c92f1a8472142b8), 0x01aa3fdc)
SAM_ROMEND
SAM_ROMLOAD(wpt_106g, "wpt0106g.bin", CRC(9b486bc4) SHA1(c2c3c426201db99303131c5efb4275291ab721d7), 0x01a33de8)
SAM_ROMEND
SAM_ROMLOAD(wpt_106i, "wpt0106i.bin", CRC(177146f0) SHA1(279380fcc3924a8bb8e3002a66c317473d3fc773), 0x01b2c6ec)
SAM_ROMEND
SAM_ROMLOAD(wpt_106l, "wpt0106l.bin", CRC(e38034a1) SHA1(c391887a90f9387f86dc94e22bb7fca57c8e91be), 0x01c706d8)
SAM_ROMEND

SAM_ROMLOAD(wpt_108a, "wpt0108a.bin", CRC(bca1f1f7) SHA1(cba81c9645f91f4b0b62ec1eed514069248c19b7), 0x019bb198)
SAM_ROMEND
SAM_ROMLOAD(wpt_108f, "wpt0108f.bin", CRC(b1a8f235) SHA1(ea7b553f2340eb82c34f7e95f4dee6fdd3026f14), 0x01aa3fdc)
SAM_ROMEND
SAM_ROMLOAD(wpt_108g, "wpt0108g.bin", CRC(b77ccfae) SHA1(730de2c5e9fa85e25ce799577748c9cf7b83c5e0), 0x01a33de8)
SAM_ROMEND
SAM_ROMLOAD(wpt_108i, "wpt0108i.bin", CRC(748362f2) SHA1(174733a2d0f45c46dca8bc6d6bc35d39e36e465d), 0x01b2c6ec)
SAM_ROMEND
SAM_ROMLOAD(wpt_108l, "wpt0108l.bin", CRC(6440224a) SHA1(e1748f0204464d134c5f5083b5c12723186c0422), 0x01c706d8)
SAM_ROMEND

SAM_ROMLOAD(wpt_109a, "wpt0109a.bin", CRC(6702e90c) SHA1(5d208894ef293c8a7157ab27eac9a8bca012dc43), 0x019bb198)
SAM_ROMEND
SAM_ROMLOAD(wpt_109f, "wpt0109f.bin", CRC(44f64903) SHA1(f3bcb8acbc8a6cad6f8573f78de53ce8336e7879), 0x01aa3fdc)
SAM_ROMEND
SAM_ROMLOAD(wpt_109f2, "wpt0109f2.bin", CRC(656f3957) SHA1(8c68b00fe528f6467a9c34663bbaa9bc308fc971), 0x01aa3fdc)
SAM_ROMEND
SAM_ROMLOAD(wpt_109g, "wpt0109g.bin", CRC(0699b279) SHA1(e645361f02865aa5560a4bbae45e085df0c4ae22), 0x01a33de8)
SAM_ROMEND
SAM_ROMLOAD(wpt_109i, "wpt0109i.bin", CRC(87e5f39f) SHA1(9c79bb0f9ebb5f4f4b9ef959f56812a3fe2fda11), 0x01b2c6ec)
SAM_ROMEND
SAM_ROMLOAD(wpt_109l, "wpt0109l.bin", CRC(a724e6c4) SHA1(161c9e6319a305953ac169cdeceeca304ab582e6), 0x01c706d8)
SAM_ROMEND

SAM_ROMLOAD(wpt_111a, "wpt0111a.bin", CRC(423138a9) SHA1(8df7b9358cacb9399c7886b9905441dc727693a6), 0x019bb19c)
SAM_ROMEND
SAM_ROMLOAD(wpt_111af,"wpt0111af.bin", CRC(e3a53741) SHA1(395ffe5e25248504d61bb1c96b914e712e7360c3), 0x01a46cf0)
SAM_ROMEND
SAM_ROMLOAD(wpt_111ai, "wpt0111ai.bin", CRC(a1e819c5) SHA1(f4e2dc6473f31e7019495d0f37b9b60f2c252f70), 0x01a8c8b8)
SAM_ROMEND
SAM_ROMLOAD(wpt_111al, "wpt0111al.bin", CRC(fbe2e2cf) SHA1(ed837d6ecc1f312c84a2fd235ade86227c2df843), 0x01b2ebb0)
SAM_ROMEND
SAM_ROMLOAD(wpt_111f, "wpt0111f.bin", CRC(25573be5) SHA1(20a33f387fbf150adda835d2f91ec456077e4c41), 0x01aa3fe0)
SAM_ROMEND
SAM_ROMLOAD(wpt_111g, "wpt0111g.bin", CRC(96782b8e) SHA1(4b89f0d44894f0157397a65a93346e637d71c4f2), 0x01a33dec)
SAM_ROMEND
SAM_ROMLOAD(wpt_111gf, "wpt0111gf.bin", CRC(c1488680) SHA1(fc652273e55d32b0c6e8e12c8ece666edac42962), 0x01a74b80)
SAM_ROMEND
SAM_ROMLOAD(wpt_111i, "wpt0111i.bin", CRC(4d718e63) SHA1(3ae6cefd6f96a31634f1399d1ce5d2c60955a93c), 0x01b2c6f0)
SAM_ROMEND
SAM_ROMLOAD(wpt_111l, "wpt0111l.bin", CRC(61f4e257) SHA1(10b11e1340593c9555ff88b0ac971433583fbf13), 0x01c706dc)
SAM_ROMEND

SAM_ROMLOAD(wpt_112a, "wpt0112a.bin", CRC(b98b4bf9) SHA1(75257a2759978d5fc699f78e809543d1cc8c456b), 0x019bb1b0)
SAM_ROMEND
SAM_ROMLOAD(wpt_112af,"wpt0112af.bin", CRC(8fe1e3c8) SHA1(837bfc2cf7f4601f99d110428f5de5dd69d2186f), 0x01A46D04)
SAM_ROMEND
SAM_ROMLOAD(wpt_112ai, "wpt0112ai.bin", CRC(ac878dfb) SHA1(13db57c77f5d75e87b21d3cfd7aba5dcbcbef59b), 0x01A8C8CC)
SAM_ROMEND
SAM_ROMLOAD(wpt_112al, "wpt0112al.bin", CRC(2c0dc704) SHA1(d5735977463ee92d87aba3a41d368b92a76b2908), 0x011B2EBC4)
SAM_ROMEND
SAM_ROMLOAD(wpt_112f, "wpt0112f.bin", CRC(1f7e081c) SHA1(512d44353f619f974d98294c55378f5a1ab2d04b), 0x01AA3FF4)
SAM_ROMEND
SAM_ROMLOAD(wpt_112g, "wpt0112g.bin", CRC(2fbac57d) SHA1(fb19e7a4a5384fc8c91e166617dad29a47b2d8b0), 0x01A33E00)
SAM_ROMEND
SAM_ROMLOAD(wpt_112gf, "wpt0112gf.bin", CRC(a6b933b3) SHA1(72a36a427527c3c5cb455a74afbbb43f2bee6480), 0x01A74B94)
SAM_ROMEND
SAM_ROMLOAD(wpt_112i, "wpt0112i.bin", CRC(0ba02986) SHA1(db1cbe0611d40c334205d0a8b9f5c6147b259549), 0x01B2C704)
SAM_ROMEND
SAM_ROMLOAD(wpt_112l, "wpt0112l.bin", CRC(203c3a05) SHA1(6173f6a6110e2a226beb566371b2821b0a5b8609), 0x01C706F0)
SAM_ROMEND

SAM_ROMLOAD(wpt_140a, "wpt1400a.bin", CRC(4b287770) SHA1(e19b60a08de9067a2b4c4dd71783fc812b3c7648), 0x019BB1EC)
SAM_ROMEND
SAM_ROMLOAD(wpt_140af,"wpt1400af.bin", CRC(bed3e3f1) SHA1(43b9cd6deccc8e516e2f5e99295b751ccadbac29), 0x01A46D40)
SAM_ROMEND
SAM_ROMLOAD(wpt_140ai, "wpt1400ai.bin", CRC(12a62641) SHA1(680283a7493921904f7fe9fae10d965db839f986), 0x01A8C908)
SAM_ROMEND
SAM_ROMLOAD(wpt_140al, "wpt1400al.bin", CRC(2f03204b) SHA1(c7a0b645258dc1aca6a297641bc5cc10c255d5a7), 0x01B2EC00)
SAM_ROMEND
SAM_ROMLOAD(wpt_140f, "wpt1400f.bin", CRC(3c9ce123) SHA1(5e9f6c9e5d4cdba36b7eacc24b602ea4dde92514), 0x01AA4030)
SAM_ROMEND
SAM_ROMLOAD(wpt_140g, "wpt1400g.bin", CRC(5f8216da) SHA1(79b79acf7c457e6d70af458712bf946094d08d2a), 0x01A33E3C)
SAM_ROMEND
SAM_ROMLOAD(wpt_140gf, "wpt1400gf.bin", CRC(7be526fa) SHA1(a42e5c2c1fde9ab97d7dcfe64b8c0055372729f3), 0x01A74BD0)
SAM_ROMEND
SAM_ROMLOAD(wpt_140i, "wpt1400i.bin", CRC(9f19ed03) SHA1(4ef570be084b1e5196a19b7f516f621025c174bc), 0x01B2C740)
SAM_ROMEND
SAM_ROMLOAD(wpt_140l, "wpt1400l.bin", CRC(00eff09c) SHA1(847203d4d2ce8d11a5403374f2d5b6dda8458bc9), 0x01C7072C)
SAM_ROMEND

SAM_INPUT_PORTS_START(wpt, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(wpt, 103a, "World Poker Tour (V1.03)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 105a, 103a, "World Poker Tour (V1.05)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 106a, 103a, "World Poker Tour (V1.06)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 106f, 103a, "World Poker Tour (V1.06) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 106g, 103a, "World Poker Tour (V1.06) (German)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 106i, 103a, "World Poker Tour (V1.06) (Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 106l, 103a, "World Poker Tour (V1.06) (Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 108a, 103a, "World Poker Tour (V1.08)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 108f, 103a, "World Poker Tour (V1.08) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 108g, 103a, "World Poker Tour (V1.08) (German)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 108i, 103a, "World Poker Tour (V1.08) (Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 108l, 103a, "World Poker Tour (V1.08) (Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 109a, 103a, "World Poker Tour (V1.09)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 109f, 103a, "World Poker Tour (V1.09) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 109f2,103a, "World Poker Tour (V1.09-2) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 109g, 103a, "World Poker Tour (V1.09) (German)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 109i, 103a, "World Poker Tour (V1.09) (Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 109l, 103a, "World Poker Tour (V1.09) (Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 111a, 103a, "World Poker Tour (V1.11)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111af, 103a, "World Poker Tour (V1.11) (English, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111ai, 103a, "World Poker Tour (V1.11) (English, Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111al, 103a, "World Poker Tour (V1.11) (English, Spanish)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111f, 103a, "World Poker Tour (V1.11) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111g, 103a, "World Poker Tour (V1.11) (German)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111gf, 103a, "World Poker Tour (V1.11) (German, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111i, 103a, "World Poker Tour (V1.11) (Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 111l, 103a, "World Poker Tour (V1.11) (Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 112a, 103a, "World Poker Tour (V1.12)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112af, 103a, "World Poker Tour (V1.12) (English, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112ai, 103a, "World Poker Tour (V1.12) (English, Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112al, 103a, "World Poker Tour (V1.12) (English, Spanish)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112f, 103a, "World Poker Tour (V1.12) (French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112g, 103a, "World Poker Tour (V1.12) (German)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112gf, 103a, "World Poker Tour (V1.12) (German, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112i, 103a, "World Poker Tour (V1.12) (Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 112l, 103a, "World Poker Tour (V1.12) (Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(wpt, 140a, 103a, "World Poker Tour (V14.00)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140af, 103a, "World Poker Tour (V14.00) (English, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140ai, 103a, "World Poker Tour (V14.00) (English, Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140al, 103a, "World Poker Tour (V14.00) (English, Spanish)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140f, 103a, "World Poker Tour (V14.00) (French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140g, 103a, "World Poker Tour (V14.00) (German)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140gf, 103a, "World Poker Tour (V14.00) (German, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140i, 103a, "World Poker Tour (V14.00) (Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(wpt, 140l, 103a, "World Poker Tour (V14.00) (Spanish)", 2008, "Stern", sam, 0)

//Simpson's Kooky Carnival Redemption - good - complete
INITGAME(scarn9nj, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);
INITGAME(scarn103, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);
INITGAME(scarn, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);
INITGAME(scarn200, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(scarn9nj, "scarn09nj.bin", CRC(3a9142e0) SHA1(57d75763fb52c891d1bb16e85ae170c38e6dd818), 0x0053B7CC)
SAM_ROMEND
SAM_ROMLOAD(scarn103, "scarn103.bin", CRC(69f5bb8a) SHA1(436db9872d5809c7ed5fe607c4167cdc0e1b5294), 0x0053A860)
SAM_ROMEND
SAM_ROMLOAD(scarn, "scarn105.bin", CRC(a09ffa33) SHA1(fab75f338a5d6c82632cd0804ddac1ab78466636), 0x0053DD14)
SAM_ROMEND
SAM_ROMLOAD(scarn200, "scarn200.bin", CRC(f08a2cf0) SHA1(ae32da8b35006061d397832563b71976899625bb), 0x005479F8)
SAM_ROMEND

SAM_INPUT_PORTS_START(scarn9nj, 1) SAM_INPUT_PORTS_END
SAM_INPUT_PORTS_START(scarn103, 1) SAM_INPUT_PORTS_END
SAM_INPUT_PORTS_START(scarn, 1) SAM_INPUT_PORTS_END
SAM_INPUT_PORTS_START(scarn200, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEFNV(scarn9nj, "Simpson's Kooky Carnival Redemption (V0.9) (New Jersey)", 2006, "Stern", sam, 0)
CORE_CLONEDEFNV(scarn103, scarn9nj, "Simpson's Kooky Carnival Redemption (V1.03)", 2006, "Stern", sam, 0)
CORE_CLONEDEFNV(scarn, scarn9nj, "Simpson's Kooky Carnival Redemption (V1.05)", 2006, "Stern", sam, 0)
CORE_CLONEDEFNV(scarn200, scarn9nj, "Simpson's Kooky Carnival Redemption (V2.00)", 2006, "Stern", sam, 0)

//Family Guy - good - complete
INITGAME(fg, GEN_SAM, sam_dmd128x32, SAM_8COL, SAM_NOMINI2);

SAM_ROMLOAD(fg_300ai, "fg300ai.bin", CRC(e2cffa79) SHA1(59dff445118ed8a3a76b6e93950802d1fec87619), 0x01FC0290)
SAM_ROMEND

SAM_ROMLOAD(fg_400a, "fg400a.bin", CRC(af6c2dd4) SHA1(e3164e982c90a5300144e63e4a74dd225fe1b272), 0x013E789C)
SAM_ROMEND
SAM_ROMLOAD(fg_400ag, "fg400ag.bin", CRC(3b4ae199) SHA1(4ef674badce2c90334fa7a8b6b90c32dcabc2334), 0x01971684)
SAM_ROMEND

SAM_ROMLOAD(fg_700af, "fg700af.bin", CRC(bbeda480) SHA1(e312a3b24f1b69db9f88a5313db168d9f2a71450), 0x01A4D3D4)
SAM_ROMEND
SAM_ROMLOAD(fg_700al, "fg700al.bin", CRC(25288f43) SHA1(5a2ed2e0b264895938466ca1104ba4ed9be86b3a), 0x01BCE8F8)
SAM_ROMEND

SAM_ROMLOAD(fg_800al, "fg800al.bin", CRC(b74dc3bc) SHA1(b24bab06b9f451cf9f068c555d3f70ffdbf40da7), 0x01BC6CB4)
SAM_ROMEND

SAM_ROMLOAD(fg_1000af, "fg1000af.bin", CRC(27cabf5d) SHA1(dde359c1fed728c8f91901f5ce351b5adef399f3), 0x01CE5514)
SAM_ROMEND
SAM_ROMLOAD(fg_1000ag, "fg1000ag.bin", CRC(130e0bd6) SHA1(ced815270d419704d94d5acdc5335460a64484ae), 0x01C53678)
SAM_ROMEND
SAM_ROMLOAD(fg_1000ai, "fg1000ai.bin", CRC(2137e62a) SHA1(ac892d2536c5dde97194ffb69c74d0517000357a), 0x01D9F8B8)
SAM_ROMEND
SAM_ROMLOAD(fg_1000al, "fg1000al.bin", CRC(0f570f24) SHA1(8861bf3e6add7a5372d81199c135808d09b5e600), 0x01E5F448)
SAM_ROMEND

SAM_ROMLOAD(fg_1100af, "fg1100af.bin", CRC(31304627) SHA1(f36d6924f1f291f675f162ff056b6ea2f03f4351), 0x01CE5514)
SAM_ROMEND
SAM_ROMLOAD(fg_1100ag, "fg1100ag.bin", CRC(d2735578) SHA1(a38b8f690ffcdb96875d3c8293e6602d7142be11), 0x01C53678)
SAM_ROMEND
SAM_ROMLOAD(fg_1100ai, "fg1100ai.bin", CRC(4fa2c59e) SHA1(7fce5c1fd306eccc567ae7d155c782649c022074), 0x01D9F8B8)
SAM_ROMEND
SAM_ROMLOAD(fg_1100al, "fg1100al.bin", CRC(d9b724a8) SHA1(33ac12fd4bbed11e38ade68426547ed97612cbd3), 0x01E5F448)
SAM_ROMEND

SAM_ROMLOAD(fg_1200ai, "fg1200ai.bin", CRC(078b0c9a) SHA1(f1472d2c4a06d674bf652dd481cce5d6ca125e0c), 0x01D9F8B8)
SAM_ROMEND
SAM_ROMLOAD(fg_1200al, "fg1200al.bin", CRC(d10cff88) SHA1(e312a3b24f1b69db9f88a5313db168d9f2a71450), 0x01E5F448)
SAM_ROMEND
SAM_ROMLOAD(fg_1200af, "fg1200af.bin", CRC(ba6a3a2e) SHA1(78eb2e26abe00d7ce5fa998b6ec1381ac0f1db31), 0x01CE5514)
SAM_ROMEND
SAM_ROMLOAD(fg_1200ag, "fg1200ag.bin", CRC(d9734f94) SHA1(d56ddf5961e5ac4c3565f9d92d6fb7e0e0af4bcb), 0x01C53678)
SAM_ROMEND

SAM_INPUT_PORTS_START(fg, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(fg, 300ai, "Family Guy (V3.00) (English, Italian)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 400a, 300ai, "Family Guy (V4.00) (English)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 400ag, 300ai, "Family Guy (V4.00) (English, German)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 700af, 300ai, "Family Guy (V7.00) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 700al, 300ai, "Family Guy (V7.00) (English, Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 800al, 300ai, "Family Guy (V8.00) (English, Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 1000af, 300ai, "Family Guy (V10.00) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1000ag, 300ai, "Family Guy (V10.00) (English, German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1000ai, 300ai, "Family Guy (V10.00) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1000al, 300ai, "Family Guy (V10.00) (English, Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 1100af, 300ai, "Family Guy (V11.00) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1100ag, 300ai, "Family Guy (V11.00) (English, German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1100ai, 300ai, "Family Guy (V11.00) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1100al, 300ai, "Family Guy (V11.00) (English, Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(fg, 1200af, 300ai, "Family Guy (V12.00) (English, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1200ag, 300ai, "Family Guy (V12.00) (English, German)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1200ai, 300ai, "Family Guy (V12.00) (English, Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(fg, 1200al, 300ai, "Family Guy (V12.00) (English, Spanish)", 2008, "Stern", sam, 0)

//Pirates of the Caribbean - good - complete
INITGAME(potc, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(potc_110af, "potc110af.bin", CRC(9d87bb49) SHA1(9db04259a0b2733d6f5966a2f3e0fc1c7002cef1), 0x01AC6550)
SAM_ROMEND
SAM_ROMLOAD(potc_110ai, "potc110ai.bin", CRC(027916d9) SHA1(0ddc0fa86da55ea0494f2095c838b41b53f568de), 0x01B178E8)
SAM_ROMEND
SAM_ROMLOAD(potc_110gf, "potc110gf.bin", CRC(ce29b69c) SHA1(ecc9ad8f77ab30538536631d513d25654f5a2f3c), 0x01B60464)
SAM_ROMEND

SAM_ROMLOAD(potc_111as, "potc111as.bin", CRC(09903169) SHA1(e284b1dc2642337633867bac9739fdda692acb2f), 0x01C829B4)
SAM_ROMEND

SAM_ROMLOAD(potc_113af, "potc113af.bin", CRC(1c52b3f5) SHA1(2079f06f1f1514614fa7cb240559b4e72925c70c), 0x01AC6550)
SAM_ROMEND
SAM_ROMLOAD(potc_113ai, "potc113ai.bin", CRC(e8b487d1) SHA1(037435b40347a8e1197876fbf7a79e03befa11f4), 0x01B178E8)
SAM_ROMEND
SAM_ROMLOAD(potc_113as, "potc113as.bin", CRC(2c819a02) SHA1(98a79b50e6c80bd58b2571fefc2f5f61030bc25d), 0x01C829B4)
SAM_ROMEND
SAM_ROMLOAD(potc_113gf, "potc113gf.bin", CRC(a508a2f8) SHA1(45e46af267c7caec86e4c92526c4cda85a1bb168), 0x01B60464)
SAM_ROMEND

SAM_ROMLOAD(potc_115af, "potc115af.bin", CRC(008e93b2) SHA1(5a272670cb3e5e59071500124a0086ef86e2b528), 0x01AC6564)
SAM_ROMEND
SAM_ROMLOAD(potc_115ai, "potc115ai.bin", CRC(88b66285) SHA1(1d65e4f7a31e51167b91f82d96c3951442b16264), 0x01B178FC)
SAM_ROMEND
SAM_ROMLOAD(potc_115as, "potc115as.bin", CRC(9c107d0e) SHA1(5213246ee78c6cc082b9f895b1d1abfa52016ede), 0x01C829C8)
SAM_ROMEND
SAM_ROMLOAD(potc_115gf, "potc115gf.bin", CRC(09a8454c) SHA1(1af420b314d339231d3b7772ffa44175a01ebd30), 0x01B60478)
SAM_ROMEND

SAM_ROMLOAD(potc_300af, "potc300af.bin", CRC(b6fc0c4b) SHA1(5c0d6b46dd6c4f14e03298500558f376ee342de0), 0x01AD2B40)
SAM_ROMEND
SAM_ROMLOAD(potc_300ai, "potc300ai.bin", CRC(2d3eb95e) SHA1(fea9409ffea3554ff0ec1c9ef6642465ec4120e7), 0x01B213A8)
SAM_ROMEND
SAM_ROMLOAD(potc_300as, "potc300as.bin", CRC(e5e7049d) SHA1(570125f9eb6d7a04ba97890095c15769f0e0dbd6), 0x01C88124)
SAM_ROMEND
SAM_ROMLOAD(potc_300gf, "potc300gf.bin", CRC(52772953) SHA1(e820ca5f347ab637bee07a9d7426058b9fd6557c), 0x01B67104)
SAM_ROMEND

SAM_ROMLOAD(potc_400af, "potc400af.bin", CRC(03cfed21) SHA1(947fff6bf3ed69cb346ae9f159e378902901033f), 0x01AD2B40)
SAM_ROMEND
SAM_ROMLOAD(potc_400ai, "potc400ai.bin", CRC(5382440b) SHA1(01d8258b98e256fc54565afd9915fd5079201973), 0x01B213A8)
SAM_ROMEND
SAM_ROMLOAD(potc_400as, "potc400as.bin", CRC(f739474d) SHA1(43bf3fbd23498e2cbeac3d87f5da727e7c05eb86), 0x01C88124)
SAM_ROMEND
SAM_ROMLOAD(potc_400gf, "potc400gf.bin", CRC(778d02e7) SHA1(6524e56ebf6c5c0effc4cb0521e3a463540ceac4), 0x01B67104)
SAM_ROMEND

SAM_ROMLOAD(potc_600af, "potc600af.bin", CRC(39a51873) SHA1(9597d356a3283c5a4e488a399196a51bf5ed16ca), 0x01AD2B40)
SAM_ROMEND
SAM_ROMLOAD(potc_600ai, "potc600ai.bin", CRC(2d7aebae) SHA1(9e383507d225859b4df276b21525f500ba98d600), 0x01B24CC8)
SAM_ROMEND
SAM_ROMLOAD(potc_600as, "potc600as.bin", CRC(5d5e1aaa) SHA1(9c7a416ae6587a86c8d2c6350621f09580226971), 0x01C92990)
SAM_ROMEND
SAM_ROMLOAD(potc_600gf, "potc600gf.bin", CRC(44eb2610) SHA1(ec1e1f7f2cd135942531e0e3f540afadb5d2f527), 0x01B67104)
SAM_ROMEND

SAM_INPUT_PORTS_START(potc, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(potc, 110af, "Pirates of the Caribbean (V1.10) (English, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 110ai, 110af, "Pirates of the Caribbean (V1.10) (English, Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 110gf, 110af, "Pirates of the Caribbean (V1.10) (German, French)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(potc, 111as, 110af, "Pirates of the Caribbean (V1.11) (English, Spanish)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(potc, 113af, 110af, "Pirates of the Caribbean (V1.13) (English, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 113ai, 110af, "Pirates of the Caribbean (V1.13) (English, Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 113as, 110af, "Pirates of the Caribbean (V1.13) (English, Spanish)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 113gf, 110af, "Pirates of the Caribbean (V1.13) (German, French)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(potc, 115af, 110af, "Pirates of the Caribbean (V1.15) (English, French)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 115ai, 110af, "Pirates of the Caribbean (V1.15) (English, Italian)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 115as, 110af, "Pirates of the Caribbean (V1.15) (English, Spanish)", 2006, "Stern", sam, 0)
CORE_CLONEDEF(potc, 115gf, 110af, "Pirates of the Caribbean (V1.15) (German, French)", 2006, "Stern", sam, 0)

CORE_CLONEDEF(potc, 300af, 110af, "Pirates of the Caribbean (V3.00) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 300ai, 110af, "Pirates of the Caribbean (V3.00) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 300as, 110af, "Pirates of the Caribbean (V3.00) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 300gf, 110af, "Pirates of the Caribbean (V3.00) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(potc, 400af, 110af, "Pirates of the Caribbean (V4.00) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 400ai, 110af, "Pirates of the Caribbean (V4.00) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 400as, 110af, "Pirates of the Caribbean (V4.00) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(potc, 400gf, 110af, "Pirates of the Caribbean (V4.00) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(potc, 600af, 110af, "Pirates of the Caribbean (V6.0) (English, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(potc, 600ai, 110af, "Pirates of the Caribbean (V6.0) (English, Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(potc, 600as, 110af, "Pirates of the Caribbean (V6.0) (English, Spanish)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(potc, 600gf, 110af, "Pirates of the Caribbean (V6.0) (German, French)", 2008, "Stern", sam, 0)

//Spider-Man - good - complete
INITGAME(sman, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(sman_130af, "sman130af.bin", CRC(6aa6a03a) SHA1(f56442e84b8789f49127bf4ba97dd05c77ea7c36), 0x017916C8)
SAM_ROMEND
SAM_ROMLOAD(sman_130ai, "sman130ai.bin", CRC(92aab158) SHA1(51662102da54e7e7c0f63689fffbf70653ee8f11), 0x017B7960)
SAM_ROMEND
SAM_ROMLOAD(sman_130al, "sman130al.bin", CRC(33004e72) SHA1(3bc30200945d896aefbff51c7b427595885a23c4), 0x0180AAA0)
SAM_ROMEND
SAM_ROMLOAD(sman_130gf, "sman130gf.bin", CRC(2838d2f3) SHA1(2192f1fbc393c5e0dcd59198d098bb2531d8b6de), 0x017AEC84)
SAM_ROMEND

SAM_ROMLOAD(sman_140, "sman140a.bin", CRC(48c2565d) SHA1(78f5d3242cfaa85fa0fd3937b6042f067dff535b), 0x016CE3C0)
SAM_ROMEND
SAM_ROMLOAD(sman_140af, "sman140af.bin", CRC(d181fa71) SHA1(66af219d9266b6b24e6857ad1a6b4fe539058052), 0x01A50398)
SAM_ROMEND
SAM_ROMLOAD(sman_140ai, "sman140ai.bin", CRC(0de6937e) SHA1(f2e60b545ef278e1b7981bf0a3dc2c622205e8e1), 0x01A70F78)
SAM_ROMEND
SAM_ROMLOAD(sman_140al, "sman140al.bin", CRC(fd372e14) SHA1(70f3e4d210a4da4b6122089c477b5b3f51d3593f), 0x01ADC768)
SAM_ROMEND
SAM_ROMLOAD(sman_140gf, "sman140gf.bin", CRC(f1124c86) SHA1(755f15dd566f86695c7143512d81e16af71c8853), 0x01A70F78)
SAM_ROMEND

SAM_ROMLOAD(sman_142, "sman142a.bin", CRC(307b0163) SHA1(015c8c86763c645b43bd71a3cdb8975fcd36a99f), 0x016E8D60)
SAM_ROMEND

SAM_ROMLOAD(sman_160, "sman160a.bin", CRC(05425962) SHA1(a37f61239a7116e5c14a345c288f781fa6248cf8), 0x01725778)
SAM_ROMEND
SAM_ROMLOAD(sman_160af, "sman160af.bin", CRC(d0b552e9) SHA1(2550baba3c4be5308779d502a2d2d01e1c2539ef), 0x01B0121C)
SAM_ROMEND
SAM_ROMLOAD(sman_160ai, "sman160ai.bin", CRC(b776f59b) SHA1(62600474b8a5e1e2d40319817505c8b5fd3df2fa), 0x01B26D28)
SAM_ROMEND
SAM_ROMLOAD(sman_160al, "sman160al.bin", CRC(776937d9) SHA1(631cadd665f895feac90c3cbc14eb8e321d19b4e), 0x01BB15BC)
SAM_ROMEND
SAM_ROMLOAD(sman_160gf, "sman160gf.bin", CRC(1498f877) SHA1(e625a7e683035665a0a1a97e5de0947628c3f7ea), 0x01B24430)
SAM_ROMEND

SAM_ROMLOAD(sman_170, "sman170a.bin", CRC(45c9e5f5) SHA1(8af3215ecc247186c83e235c60c3a2990364baad), 0x01877484)
SAM_ROMEND
SAM_ROMLOAD(sman_170af, "sman170af.bin", CRC(b38f3948) SHA1(8daae4bc8b1eaca2bd43198365474f5da09b4788), 0x01C6F32C)
SAM_ROMEND
SAM_ROMLOAD(sman_170ai, "sman170ai.bin", CRC(ba176624) SHA1(56c847995b5a3e2286e231c1d69f82cf5492cd5d), 0x01C90F74)
SAM_ROMEND
SAM_ROMLOAD(sman_170al, "sman170al.bin", CRC(0455f3a9) SHA1(134ff31605798989b396220f8580d1c079678084), 0x01D24E70)
SAM_ROMEND
SAM_ROMLOAD(sman_170gf, "sman170gf.bin", CRC(152aa803) SHA1(e18f9dcc5380126262cf1e32e99b6cc2c4aa23cb), 0x01C99C74)
SAM_ROMEND

SAM_ROMLOAD(sman_190, "sman190a.bin", CRC(7822a6d1) SHA1(6a21dfc44e8fa5e138fe6474c467ef6d6544d78c), 0x01652310)
SAM_ROMEND
SAM_ROMLOAD(sman_190af, "sman190af.bin", CRC(dac27fde) SHA1(93a236afc4be6514a8fc57e45eb5698bd999eef6), 0x018B5C34)
SAM_ROMEND
SAM_ROMLOAD(sman_190ai, "sman190ai.bin", CRC(95c769ac) SHA1(e713677fea9e28b2438a30bf5d81448d3ca140e4), 0x018CD02C)
SAM_ROMEND
SAM_ROMLOAD(sman_190al, "sman190al.bin", CRC(4df8168c) SHA1(8ebfda5378037c231075017713515a3681a0e38c), 0x01925DD0)
SAM_ROMEND
SAM_ROMLOAD(sman_190gf, "sman190gf.bin", CRC(a4a874a4) SHA1(1e46720462f1279c417d955c500e829e878ce31f), 0x018CD02C)
SAM_ROMEND

SAM_ROMLOAD(sman_192, "sman192a.bin", CRC(a44054fa) SHA1(a0910693d13cc61dba7a2bbe9185a24b33ef20ec), 0x01920870)
SAM_ROMEND
SAM_ROMLOAD(sman_192af, "sman192af.bin", CRC(c9f8a7dd) SHA1(e63e98965d08b8a645c92fb34ce7fc6e1ad05ddc), 0x01B81624)
SAM_ROMEND
SAM_ROMLOAD(sman_192ai, "sman192ai.bin", CRC(f02acad4) SHA1(da103d5ddbcbdcc19cca6c17b557dcc71942970a), 0x01B99F88)
SAM_ROMEND
SAM_ROMLOAD(sman_192al, "sman192al.bin", CRC(501f9986) SHA1(d93f973f9eddfd85903544f0ce49c1bf17b36eb9), 0x01BF19A0)
SAM_ROMEND
SAM_ROMLOAD(sman_192gf, "sman192gf.bin", CRC(32597e1d) SHA1(47a28cdba11b32661dbae95e3be1a41fc475fa5e), 0x01B9A1B4)
SAM_ROMEND

SAM_ROMLOAD(sman_200, "sman200a.bin", CRC(3b13348c) SHA1(4b5c6445d7805c0a39054bd51522751030b73162), 0x0168E8A8)
SAM_ROMEND

SAM_ROMLOAD(sman_210, "sman210a.bin", CRC(f983df18) SHA1(a0d46e1a58f016102773861a4f1b026755f776c8), 0x0168e8a8)
SAM_ROMEND
SAM_ROMLOAD(sman_210af, "sman210af.bin", CRC(2e86ac24) SHA1(aa223db6289a876e77080e16f29cbfc62183fa67), 0x0192b160)
SAM_ROMEND
SAM_ROMLOAD(sman_210ai, "sman210ai.bin", CRC(aadd1ea7) SHA1(a41b0067f7490c6df5d85e80b208c9993f806366), 0x0193cc7c)
SAM_ROMEND
SAM_ROMLOAD(sman_210al, "sman210al.bin", CRC(8c441caa) SHA1(e40ac748284f65de5c444ac89d3b02dd987facd0), 0x019a5d5c)
SAM_ROMEND
SAM_ROMLOAD(sman_210gf, "sman210gf.bin", CRC(2995cb97) SHA1(0093d3f20aebbf6129854757cc10aff63fc18a4a), 0x01941c04)
SAM_ROMEND

SAM_ROMLOAD(sman_220, "sman220a.bin", CRC(44f31e8e) SHA1(4c07d01c95c5fab1955b11e4f7c65f369a91dfd7), 0x018775B8)
SAM_ROMEND

SAM_ROMLOAD(sman_230, "sman230a.bin", CRC(a86f1768) SHA1(72662dcf05717d3b2b335077ceddabe562738468), 0x018775B8)
SAM_ROMEND

SAM_ROMLOAD(sman_240, "sman240a.bin", CRC(dc5ee57e) SHA1(7453db81b161cdbf7be690da15ea8a78e4a4e57d), 0x018775B8)
SAM_ROMEND

SAM_ROMLOAD(sman_260, "sman260a.bin", CRC(acfc813e) SHA1(bcbb0ec2bbfc55b1256c83b0300c0c38d15a3db1), 0x018775E0)
SAM_ROMEND
SAM_ROMLOAD(sman_261, "sman261a.bin", CRC(9900cd4c) SHA1(1b95f957f8d709bba9fb3b7dcd4bca99176a010c), 0x01718F64)
SAM_ROMEND

SAM_INPUT_PORTS_START(sman, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(sman, 130af, "Spider-Man (V1.30) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 130ai, 130af, "Spider-Man (V1.30) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 130al, 130af, "Spider-Man (V1.30) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 130gf, 130af, "Spider-Man (V1.30) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 140, 130af, "Spider-Man (V1.40)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 140af, 130af, "Spider-Man (V1.40) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 140ai, 130af, "Spider-Man (V1.40) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 140al, 130af, "Spider-Man (V1.40) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 140gf, 130af, "Spider-Man (V1.40) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 142, 130af, "Spider-Man (V1.42) BETA", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 160, 130af, "Spider-Man (V1.60)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 160af, 130af, "Spider-Man (V1.60) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 160ai, 130af, "Spider-Man (V1.60) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 160al, 130af, "Spider-Man (V1.60) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 160gf, 130af, "Spider-Man (V1.60) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 170, 130af, "Spider-Man (V1.70)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 170af, 130af, "Spider-Man (V1.70) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 170ai, 130af, "Spider-Man (V1.70) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 170al, 130af, "Spider-Man (V1.70) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 170gf, 130af, "Spider-Man (V1.70) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 190, 130af, "Spider-Man (V1.90)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 190af, 130af, "Spider-Man (V1.90) (English, French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 190ai, 130af, "Spider-Man (V1.90) (English, Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 190al, 130af, "Spider-Man (V1.90) (English, Spanish)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(sman, 190gf, 130af, "Spider-Man (V1.90) (German, French)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(sman, 192, 130af, "Spider-Man (V1.92)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 192af, 130af, "Spider-Man (V1.92) (English, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 192ai, 130af, "Spider-Man (V1.92) (English, Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 192al, 130af, "Spider-Man (V1.92) (English, Spanish)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 192gf, 130af, "Spider-Man (V1.92) (German, French)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(sman, 200, 130af, "Spider-Man (V2.0)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(sman, 210, 130af, "Spider-Man (V2.1)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 210af, 130af, "Spider-Man (V2.1) (English, French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 210ai, 130af, "Spider-Man (V2.1) (English, Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 210al, 130af, "Spider-Man (V2.1) (English, Spanish)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(sman, 210gf, 130af, "Spider-Man (V2.1) (German, French)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(sman, 220, 130af, "Spider-Man (V2.2)", 2009, "Stern", sam, 0)

CORE_CLONEDEF(sman, 230, 130af, "Spider-Man (V2.3)", 2009, "Stern", sam, 0)

CORE_CLONEDEF(sman, 240, 130af, "Spider-Man (V2.4)", 2009, "Stern", sam, 0)

CORE_CLONEDEF(sman, 260, 130af, "Spider-Man (V2.6)", 2010, "Stern", sam, 0)

CORE_CLONEDEF(sman, 261, 130af, "Spider-Man (V2.61)", 2010, "Stern", sam, 0)

//Wheel Of Fortune - good - complete
INITGAME(wof, GEN_SAM, sammini2_dmd128x32, SAM_8COL, SAM_MINIDMD3);

SAM_ROMLOAD(wof_100, "wof0100a.bin", CRC(f3b80429) SHA1(ab1c9752ea74b5950b51aabc6dbca4f405705240), 0x01C7DF60)
SAM_ROMEND

SAM_ROMLOAD(wof_200, "wof0200a.bin", CRC(2e56b65f) SHA1(908662261548f4b80433d58359e9ff1013bf315b), 0x01C7DFD0)
SAM_ROMEND
SAM_ROMLOAD(wof_200f, "wof0200f.bin", CRC(d48d4885) SHA1(25cabea55f30d86b8d6398f94e1d180377c34de6), 0x01E76BA4)
SAM_ROMEND
SAM_ROMLOAD(wof_200g, "wof0200g.bin", CRC(81f61e6c) SHA1(395be7e0ccb9a806738fc6338b8e6dbea561986d), 0x01CDEC2C)
SAM_ROMEND
SAM_ROMLOAD(wof_200i, "wof0200i.bin", CRC(3e48eef7) SHA1(806a0313852405cd9913406201dd9e434b9b160a), 0x01D45EE8)
SAM_ROMEND

SAM_ROMLOAD(wof_300, "wof0300a.bin", CRC(7a8483b8) SHA1(e361eea5a01d6ba22782d34538edd05f3b068472), 0x01C7DFD0)
SAM_ROMEND
SAM_ROMLOAD(wof_300f, "wof0300f.bin", CRC(fd5c2bec) SHA1(77f6e4177df8a17f43198843f8a0a3cf5caf1704), 0x01E76BA4)
SAM_ROMEND
SAM_ROMLOAD(wof_300g, "wof0300g.bin", CRC(54b50069) SHA1(909b98a7f5fdfa0164c7dc52e9c830eecada2a64), 0x01CDEC2C)
SAM_ROMEND
SAM_ROMLOAD(wof_300i, "wof0300i.bin", CRC(7528800b) SHA1(d55024935861aa8895f9604e92f0d74cb2f3827d), 0x01D45EE8)
SAM_ROMEND
SAM_ROMLOAD(wof_300l, "wof0300l.bin", CRC(12e1b3a5) SHA1(6b62e40e7b124477dc8508e39722c3444d4b39a4), 0x01B080B0)
SAM_ROMEND

SAM_ROMLOAD(wof_400, "wof0400a.bin", CRC(974e6dd0) SHA1(ce4d7537e8f42ab6c3e84eac19688e2155115345), 0x01C7DFD0)
SAM_ROMEND
SAM_ROMLOAD(wof_400f, "wof0400f.bin", CRC(91a793c0) SHA1(6c390ab435dc20889bccfdd11bbfc411efd1e4f9), 0x01E76BA4)
SAM_ROMEND
SAM_ROMLOAD(wof_400g, "wof0400g.bin", CRC(ee97a6f3) SHA1(17a3093f7e5d052c23b669ee8717a21a80b61813), 0x01CDEC2C)
SAM_ROMEND
SAM_ROMLOAD(wof_400i, "wof0400i.bin", CRC(35053d2e) SHA1(3b8d176c7b34e7eaf20f9dcf27649841c5122609), 0x01D45EE8)
SAM_ROMEND

SAM_ROMLOAD(wof_401l, "wof0401l.bin", CRC(4db936f4) SHA1(4af1d4642529164cb5bc0b9adbc229b131098007), 0x01B080B0)
SAM_ROMEND

SAM_ROMLOAD(wof_500, "wof0500a.bin", CRC(6613e864) SHA1(b6e6dcfa782720e7d0ce36f8ea33a0d05763d6bd), 0x01C7DFD0)
SAM_ROMEND
SAM_ROMLOAD(wof_500f, "wof0500f.bin", CRC(3aef1035) SHA1(4fa0a40fea403beef0b3ce695ff52dec3d90f7bf), 0x01E76BA4)
SAM_ROMEND
SAM_ROMLOAD(wof_500g, "wof0500g.bin", CRC(658f8622) SHA1(31926717b5914f91b70eeba182eb219a4fd51299), 0x01CDEC2C)
SAM_ROMEND
SAM_ROMLOAD(wof_500i, "wof0500i.bin", CRC(27fb48bc) SHA1(9a9846c84a1fc543ec2236a28991d0cd70e86b52), 0x01D45EE8)
SAM_ROMEND
SAM_ROMLOAD(wof_500l, "wof0500l.bin", CRC(b8e09fcd) SHA1(522983ce75b24733a0827a2eeea3d44419c7998e), 0x01B080B0)
SAM_ROMEND

SAM_INPUT_PORTS_START(wof, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(wof, 100, "Wheel of Fortune (V1.0)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(wof, 200, 100, "Wheel of Fortune (V2.0)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 200f, 100, "Wheel of Fortune (V2.0) (French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 200g, 100, "Wheel of Fortune (V2.0) (German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 200i, 100, "Wheel of Fortune (V2.0) (Italian)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(wof, 300, 100, "Wheel of Fortune (V3.0)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 300f, 100, "Wheel of Fortune (V3.0) (French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 300g, 100, "Wheel of Fortune (V3.0) (German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 300i, 100, "Wheel of Fortune (V3.0) (Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 300l, 100, "Wheel of Fortune (V3.0) (Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(wof, 400, 100, "Wheel of Fortune (V4.0)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 400f, 100, "Wheel of Fortune (V4.0) (French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 400g, 100, "Wheel of Fortune (V4.0) (German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 400i, 100, "Wheel of Fortune (V4.0) (Italian)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(wof, 401l, 100, "Wheel of Fortune (V4.01) (Spanish)", 2007, "Stern", sam, 0)

CORE_CLONEDEF(wof, 500, 100, "Wheel of Fortune (V5.0)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 500f, 100, "Wheel of Fortune (V5.0) (French)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 500g, 100, "Wheel of Fortune (V5.0) (German)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 500i, 100, "Wheel of Fortune (V5.0) (Italian)", 2007, "Stern", sam, 0)
CORE_CLONEDEF(wof, 500l, 100, "Wheel of Fortune (V5.0) (Spanish)", 2007, "Stern", sam, 0)

//Shrek - good - complete
INITGAME(shr, GEN_SAM, sam_dmd128x32, SAM_8COL, SAM_NOMINI2);

SAM_ROMLOAD(shr_130, "shr130.bin", CRC(0c4efde5) SHA1(58e156a43fef983d48f6676e8d65fb30d45f8ec3), 0x01BB0824)
SAM_ROMEND
SAM_ROMLOAD(shr_141, "shr141.bin", CRC(f4f847ce) SHA1(d28f9186bb04036e9ff56d540e70a50f0816051b), 0x01C55290)
SAM_ROMEND

SAM_INPUT_PORTS_START(shr, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(shr, 130, "Shrek (V1.30)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(shr, 141, 130, "Shrek (V1.41)", 2008, "Stern", sam, 0)

//Indiana Jones - good - bugged - complete
INITGAME(ij4, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(ij4_113, "ij4_113.bin", CRC(aa2bdf3e) SHA1(71fd1c970fe589cec5124237684facaae92cbf09), 0x01C6D98C)
SAM_ROMEND
SAM_ROMLOAD(ij4_113f, "ij4_113f.bin", CRC(cb7b7c31) SHA1(3a2f718a9a533941c5476f8348dacf7e3523ddd0), 0x01C6D98C)
SAM_ROMEND
SAM_ROMLOAD(ij4_113g, "ij4_113g.bin", CRC(30a33bfd) SHA1(c37b6035c313cce85d325ab87039f5a872d28f5a), 0x01BFF3F4)
SAM_ROMEND
SAM_ROMLOAD(ij4_113i, "ij4_113i.bin", CRC(fcb37e0f) SHA1(7b23a56baa9985e2322aee954befa13dc2d55119), 0x01C81FA4)
SAM_ROMEND
SAM_ROMLOAD(ij4_113l, "ij4_113l.bin", CRC(e4ff8120) SHA1(f5537cf920633a621b4c7a740bfc07cefe3a99d0), 0x01D02988)
SAM_ROMEND

SAM_ROMLOAD(ij4_114, "ij4_114.bin", CRC(00e5b850) SHA1(3ad57120d11aff4ca8917dea28c2c26ae254e2b5), 0x01C6D9E4)
SAM_ROMEND
SAM_ROMLOAD(ij4_114f, "ij4_114f.bin", CRC(a7c2a5e4) SHA1(c0463b055096a3112a31680dc509f421c1a5c1cf), 0x01C6D9E4)
SAM_ROMEND
SAM_ROMLOAD(ij4_114g, "ij4_114g.bin", CRC(7176b0be) SHA1(505132887bca0fa9d6ca8597101357f26501a0ad), 0x01C34974)
SAM_ROMEND
SAM_ROMLOAD(ij4_114i, "ij4_114i.bin", CRC(dac0563e) SHA1(30dbaed1b1a180f7ca68a4caef469c2997bf0355), 0x01C875F8)
SAM_ROMEND
SAM_ROMLOAD(ij4_114l, "ij4_114l.bin", CRC(e9b3a81a) SHA1(574377e7a398083f3498d91640ad7dc5250acbd7), 0x01D0B290)
SAM_ROMEND

SAM_ROMLOAD(ij4_116, "ij4_116.bin", CRC(80293485) SHA1(043c857a8dfa79cb7ae876c55a10227bdff8e873), 0x01C6D9E4)
SAM_ROMEND
SAM_ROMLOAD(ij4_116f, "ij4_116f.bin", CRC(56821942) SHA1(484f4359b6d1ecb45c29bef7532a8136028504f4), 0x01C6D9E4)
SAM_ROMEND
SAM_ROMLOAD(ij4_116g, "ij4_116g.bin", CRC(2b7b81be) SHA1(a70ed07daec7f13165a0256bc011a72136e25210), 0x01C34974)
SAM_ROMEND
SAM_ROMLOAD(ij4_116i, "ij4_116i.bin", CRC(7b07c207) SHA1(67969e85cf96949f8b85d88acfb69be55f32ea52), 0x01C96B38)
SAM_ROMEND
SAM_ROMLOAD(ij4_116l, "ij4_116l.bin", CRC(833ae2fa) SHA1(cb931e473164ddfa2559f3a58f2fcac5d456dc96), 0x01D14FD8)
SAM_ROMEND

SAM_ROMLOAD(ij4_210, "ij4_210.bin", CRC(b96e6fd2) SHA1(f59cbdefc5ab6b21662981b3eb681fd8bd7ade54), 0x01C6D9E4)
SAM_ROMEND
SAM_ROMLOAD(ij4_210f, "ij4_210f.bin", CRC(d1d37248) SHA1(fd6819e0e86b83d658790ff30871596542f98c8e), 0x01C6D9E4)
SAM_ROMEND

SAM_INPUT_PORTS_START(ij4, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(ij4, 113, "Indiana Jones (V1.13)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 113f, 113, "Indiana Jones (V1.13) (French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 113g, 113, "Indiana Jones (V1.13) (German)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 113i, 113, "Indiana Jones (V1.13) (Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 113l, 113, "Indiana Jones (V1.13) (Spanish)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(ij4, 114, 113, "Indiana Jones (V1.14)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 114f, 113, "Indiana Jones (V1.14) (French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 114g, 113, "Indiana Jones (V1.14) (German)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 114i, 113, "Indiana Jones (V1.14) (Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 114l, 113, "Indiana Jones (V1.14) (Spanish)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(ij4, 116, 113, "Indiana Jones (V1.16)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 116f, 113, "Indiana Jones (V1.16) (French)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 116g, 113, "Indiana Jones (V1.16) (German)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 116i, 113, "Indiana Jones (V1.16) (Italian)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 116l, 113, "Indiana Jones (V1.16) (Spanish)", 2008, "Stern", sam, 0)

CORE_CLONEDEF(ij4, 210, 113, "Indiana Jones (V2.1)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(ij4, 210f, 113, "Indiana Jones (V2.1) (French)", 2009, "Stern", sam, 0)

//Batman: Dark Knight - good - complete
INITGAME(bdk, GEN_SAM, sam_dmd128x32, SAM_3COL, SAM_NOMINI3);

SAM_ROMLOAD(bdk_130, "bdk130.bin", CRC(83a32958) SHA1(0326891bc142c8b92bd4f6d29bd4301bacbed0e7), 0x01BA1E94)
SAM_ROMEND
SAM_ROMLOAD(bdk_150, "bdk150.bin", CRC(ed11b88c) SHA1(534224de597cbd3632b902397d945ab725e24912), 0x018EE5E8)
SAM_ROMEND
SAM_ROMLOAD(bdk_160, "bdk160.bin", CRC(5554ea47) SHA1(0ece4779ad9a3d6c8428306774e2bf36a20d680d), 0x01B02F70)
SAM_ROMEND
SAM_ROMLOAD(bdk_200, "bdk200.bin", CRC(07b716a9) SHA1(4cde06308bb967435c7c1bf078a2cda36088e3ec), 0x01B04378)
SAM_ROMEND
SAM_ROMLOAD(bdk_201, "bdk201.bin", CRC(ac84fef1) SHA1(bde3250f3d95a12a5f3b74ac9d11ba0bd331e9cd), 0x01B96D94)
SAM_ROMEND
SAM_ROMLOAD(bdk_202, "bdk202.bin", CRC(6e415ce7) SHA1(30a3938817da20ccb87c7e878cdd8a13ada097ab), 0x01b96d94)
SAM_ROMEND
SAM_ROMLOAD(bdk_290, "bdk290.bin", CRC(09ce777e) SHA1(79b6d3f91aa4d42318c698a44444bf875ad573f2), 0x1d3d2d4)
SAM_ROMEND
SAM_ROMLOAD(bdk_294, "bdk294.bin", CRC(e087ec82) SHA1(aad2c43e6de9a520954eb50b6c824a138cd6f47f), 0x01C00844)
SAM_ROMEND

SAM_INPUT_PORTS_START(bdk, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(bdk, 130, "Batman: Dark Knight (V1.3)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 150, 130, "Batman: Dark Knight (V1.5)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 160, 130, "Batman: Dark Knight (V1.6)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 200, 130, "Batman: Dark Knight (V2.0)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 201, 130, "Batman: Dark Knight (V2.01)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 202, 130, "Batman: Dark Knight (V2.02)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 290, 130, "Batman: Dark Knight (V2.90)", 2010, "Stern", sam, 0)
CORE_CLONEDEF(bdk, 294, 130, "Batman: Dark Knight (V2.94)", 2010, "Stern", sam, 0)

//C.S.I. - good - bugged
INITGAME(csi, GEN_SAM, sam_dmd128x32, SAM_3COL, SAM_NOMINI4);

SAM_ROMLOAD(csi_102, "csi102a.bin", CRC(770f4ab6) SHA1(7670022926fcf5bb8f8848374cf1a6237803100a), 0x01e21fc0)
SAM_ROMEND
SAM_ROMLOAD(csi_103, "csi103a.bin", CRC(371bc874) SHA1(547588b85b4d6e79123178db3f3e51354e8d2229 ), 0x01E61C88)
SAM_ROMEND
SAM_ROMLOAD(csi_104, "csi104a.bin", CRC(15694586) SHA1(3a6b70d43f9922d7a459e1dc4c235bcf03e7858e), 0x01e21fc0)
SAM_ROMEND
SAM_ROMLOAD(csi_200, "csi200a.bin", CRC(ecb25112) SHA1(385bede7955e06c1e1b7cd06e988a64b0e6ea54f), 0x01e21fc0)
SAM_ROMEND
SAM_ROMLOAD(csi_210, "csi210a.bin", CRC(afebb31f) SHA1(9b8179baa2f6e61852b57aaad9a28def0c014861), 0x01e21fc0)
SAM_ROMEND
SAM_ROMLOAD(csi_230, "csi230a.bin", CRC(c25ccc67) SHA1(51a21fca06db4b05bda2c7d5a09d655c97ba19c6), 0x01e21fc0)
SAM_ROMEND
SAM_ROMLOAD(csi_240, "csi240a.bin", CRC(2be97fa3) SHA1(5aa231bde81f7787cc06567c8b3d28c750588071), 0x01e21fc0)
SAM_ROMEND

SAM_INPUT_PORTS_START(csi, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(csi, 102, "C.S.I. (V1.02)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(csi, 103, 102, "C.S.I. (V1.03)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(csi, 104, 102, "C.S.I. (V1.04)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(csi, 200, 102, "C.S.I. (V2.0)", 2008, "Stern", sam, 0)
CORE_CLONEDEF(csi, 210, 102, "C.S.I. (V2.1)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(csi, 230, 102, "C.S.I. (V2.3)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(csi, 240, 102, "C.S.I. (V2.4)", 2009, "Stern", sam, 0)

//24
INITGAME(twenty4, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(twenty4_130, "24_130a.bin", CRC(955a5c12) SHA1(66e33fb438c831679aeb3ba68af7b4a3c59966ef), 0x01C08280)
SAM_ROMEND
SAM_ROMLOAD(twenty4_140, "24_140a.bin", CRC(bab92fb1) SHA1(07c8d9c28730411dd0f23d5960a223beb4c587b2), 0x01C08280)
SAM_ROMEND
SAM_ROMLOAD(twenty4_144, "24_144a.bin", CRC(29c47da0) SHA1(8d38e35a0df843a71cac6cd4dd6aa460347a208c), 0x01CA8E50)
SAM_ROMEND
SAM_ROMLOAD(twenty4_150, "24_150a.bin", CRC(9d7d87cc) SHA1(df6b2f60b87226fdda33bdbbe03ea87d690fc563), 0x01CA8E50)
SAM_ROMEND

SAM_INPUT_PORTS_START(twenty4, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(twenty4, 130, "24 (V1.3)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(twenty4, 140, 130, "24 (V1.4)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(twenty4, 144, 130, "24 (V1.44)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(twenty4, 150, 130, "24 (V1.5)", 2010, "Stern", sam, 0)

//NBA
INITGAME(nba, GEN_SAM, sam_dmd128x32, SAM_0COL, SAM_NOMINI);

SAM_ROMLOAD(nba_500, "nba500.bin", CRC(01b0c27a) SHA1(d7f4f6b24630b55559a48cde4475422905811106), 0x019112d0)
SAM_ROMEND
SAM_ROMLOAD(nba_600, "nba600.bin", CRC(af2fbcf4) SHA1(47df1992a1eb6c4cd5ec246912eab9f5636499a7), 0x019112d0)
SAM_ROMEND
SAM_ROMLOAD(nba_700, "nba700.bin", CRC(15ece43b) SHA1(90cc8b4c52a61da9701fcaba0a21144fe576eaf4), 0x019112d0)
SAM_ROMEND
SAM_ROMLOAD(nba_801, "nba801.bin", CRC(0f8b146e) SHA1(090d73a9bff0a0b0c17ced1557d5e63e5c986e95), 0x019112d0)
SAM_ROMEND
SAM_ROMLOAD(nba_802, "nba802.bin", CRC(ba681dac) SHA1(184f3315a54b1a5295b19222c718ac38fa60d340), 0x019112d0)
SAM_ROMEND

SAM_INPUT_PORTS_START(nba, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(nba, 500, "NBA (V5.0)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(nba, 600, 500, "NBA (V6.0)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(nba, 700, 500, "NBA (V7.0)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(nba, 801, 500, "NBA (V8.01)", 2009, "Stern", sam, 0)
CORE_CLONEDEF(nba, 802, 500, "NBA (V8.02)", 2009, "Stern", sam, 0)

//Big Buck Hunter Pro
INITGAME(bbh, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(bbh_140, "bbh140.bin", CRC(302e29f0) SHA1(0c500c0a5588f8476a71599be70b515ba3e19cab), 0x1bb8fa4)
SAM_ROMEND
SAM_ROMLOAD(bbh_150, "bbh150.bin", CRC(18bad072) SHA1(16e499046107baceda6f6c934d70ba2108915973), 0x1bb8fa4)
SAM_ROMEND
SAM_ROMLOAD(bbh_160, "bbh160.bin", CRC(75077f85) SHA1(c58a2ae5c1332390f0d1191ee8ff920ceec23352), 0x01BB8FA4)
SAM_ROMEND
SAM_ROMLOAD(bbh_170, "bbh170.bin", CRC(0c2d3e64) SHA1(9a71959c57b9a75028e21bce9ee03871f8914138), 0x01BB8FD0)
SAM_ROMEND

SAM_INPUT_PORTS_START(bbh, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(bbh, 160, "Big Buck Hunter Pro (V1.6)", 2010, "Stern", sam, 0)
CORE_CLONEDEF(bbh, 140, 160, "Big Buck Hunter Pro (V1.4)", 2010, "Stern", sam, 0)
CORE_CLONEDEF(bbh, 150, 160, "Big Buck Hunter Pro (V1.5)", 2010, "Stern", sam, 0)
CORE_CLONEDEF(bbh, 170, 160, "Big Buck Hunter Pro (V1.7)", 2010, "Stern", sam, 0)

//Ironman 2
INITGAME(im2, GEN_SAM, sam_dmd128x32, SAM_0COL, SAM_NOMINI);

SAM_ROMLOAD(im2_100, "im2_100.bin", CRC(b27d12bf) SHA1(dfb497f2edaf4321823b243cced9d9e2b7bac628), 0x1b8fe44)
SAM_ROMEND
SAM_ROMLOAD(im2_110, "im2_110.bin", CRC(3140cb7c) SHA1(20b0e84b61069e09f189d79e6b4d5abf0369a893), 0x1b8fe44)
SAM_ROMEND
SAM_ROMLOAD(im2_120, "im2_120.bin", CRC(71df27ad) SHA1(9e1745522d28af6bdcada56f2cf0b489656fc885), 0x1b8fe44)
SAM_ROMEND
SAM_ROMLOAD(im2_140, "im2_140.bin", CRC(9cbfd6ef) SHA1(904c058a00c268593a62988127f8a18d974eda5e), 0x01CB8870)
SAM_ROMEND
SAM_ROMLOAD(im2_160, "im2_160.bin", CRC(ed0dd2bb) SHA1(789b9dc5f5d97a86eb406f864f2785f371db6ca5), 0x01C1FD64)
SAM_ROMEND
SAM_ROMLOAD(im2_181, "im2_181.bin", CRC(915d972b) SHA1(0d29929ae304bc4bbdbab7813a279f3200cac6ef), 29699164)
SAM_ROMEND
SAM_ROMLOAD(im2_182, "im2_182.bin", CRC(c65aff0b) SHA1(ce4d26ffdfd8539e8f7fca78dfa55f80247f9334), 29699164)
SAM_ROMEND
SAM_ROMLOAD(im2_183, "im2_183.bin", CRC(cf2791a6) SHA1(eb616e3bf33024374f4e998a579bc88f63282ba6), 29699164)
SAM_ROMEND
SAM_ROMLOAD(im2_183ve, "im2_183ve.bin", CRC(e477183c) SHA1(6314b44b58c79889f95af1792395203dbbb36b0b), 29699164)
SAM_ROMEND

SAM_INPUT_PORTS_START(im2, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(im2, 140, "Ironman 2 (V1.4)", 2010, "Stern", sam, 0)
CORE_CLONEDEF(im2, 100, 140, "Ironman 2 (V1.0)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(im2, 110, 140, "Ironman 2 (V1.1)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(im2, 120, 140, "Ironman 2 (V1.2)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(im2, 160, 140, "Ironman 2 (V1.6)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(im2, 181, 140, "Ironman 2 (V1.81)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(im2, 182, 140, "Ironman 2 (V1.82)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(im2, 183, 140, "Ironman 2 (V1.83)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(im2, 183ve, 140, "Ironman 2 (V1.83VE)", 2014, "Stern", sam, 0)

//Tron: Legacy
INITGAME(trn, GEN_SAM, sam_dmd128x32, SAM_3COL, SAM_NOMINI5);

SAM_ROMLOAD(trn_170, "trn170.bin", CRC(1f3b314d) SHA1(59df759539c02600d2579b4e59a184ac3db64020), 0x01F13C9C)
SAM_ROMEND
SAM_ROMLOAD(trn_140h, "trn140h.bin", CRC(7de92a4b) SHA1(87eb46e1564b8a913d6cc17a86b50828dd1273de), 0x01f286d8)
SAM_ROMEND
SAM_ROMLOAD(trn_174, "trn174.bin", CRC(20e44481) SHA1(88e6e75efb640a7978f4003f0df5ee1e41087f72), 0x01F79E70)
SAM_ROMEND
SAM_ROMLOAD(trn_17402, "trn17402.bin", CRC(94a5946c) SHA1(5026e33a8bb00c83caf06891727b8439d1274fbb), 0x01F79E70)
SAM_ROMEND
SAM_ROMLOAD(trn_174h, "trn174h.bin", CRC(a45224bf) SHA1(40e36764af332175f653e8ddc2a8bb77891c1230), 0x01F93B84)
SAM_ROMEND

SAM_INPUT_PORTS_START(trn, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(trn, 170, "Tron: Legacy Pro (V1.70)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(trn, 140h, 170, "Tron: Legacy Limited Edition (V1.40)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(trn, 174, 170, "Tron: Legacy Pro (V1.74)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(trn, 17402, 170, "Tron: Legacy Pro (V1.7402)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(trn, 174h, 170, "Tron: Legacy Limited Edition (V1.74)", 2013, "Stern", sam, 0)

//Transformers
INITGAME(tf, GEN_SAM, sam_dmd128x32, SAM_3COL, SAM_NOMINI4);

SAM_ROMLOAD(tf_140, "tf140.bin", CRC(b41c8d33) SHA1(e96462df7481759d5c29a192766f03334b2b4562), 0x01D0C800)
SAM_ROMEND
SAM_ROMLOAD(tf_170, "tf170.bin", CRC(cd8707e6) SHA1(847c37988bbc12e8200a6762c2851b610a0b849f), 0x01D24F34)
SAM_ROMEND
SAM_ROMLOAD(tf_180, "tf180.bin", CRC(0b6e3a4f) SHA1(62e1328e8462680694157aca266055d57347e904), 0x01D24F34)
SAM_ROMEND
SAM_ROMLOAD(tf_120h, "tf120h.bin", CRC(0f750246) SHA1(7ab3c9278f443511e5e7fcf062ffc9e8d1456396), 0x01EB1C4C)
SAM_ROMEND
SAM_ROMLOAD(tf_150h, "tf150h.bin", CRC(5cec6bfc) SHA1(30899241c2c0a9d42aa19fa3eb4180452bdaec91), 0x01EB1E5C)
SAM_ROMEND
SAM_ROMLOAD(tf_180h, "tf180h.bin", CRC(467aeeb3) SHA1(feec42b083d81e632ef8ae402eb9f20f3104db08), 0x01EB1E04)
SAM_ROMEND

SAM_INPUT_PORTS_START(tf, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(tf, 140, "Transformers Pro (V1.40)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(tf, 170, 140, "Transformers Pro (V1.70)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(tf, 180, 140, "Transformers Pro (V1.80)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(tf, 120h, 140, "Transformers Limited Edition (V1.20)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(tf, 150h, 140, "Transformers Limited Edition (V1.50)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(tf, 180h, 140, "Transformers Limited Edition (V1.80)", 2013, "Stern", sam, 0)

//Avatar - ?? Seems ok
INITGAME(avr, GEN_SAM, sam_dmd128x32, SAM_0COL, SAM_NOMINI);

SAM_ROMLOAD(avr_106, "avr_106.bin", CRC(695799e5) SHA1(3e216fd4273adb7417294b3e648befd69350ab25), 0x01ED31B4)
SAM_ROMEND
SAM_ROMLOAD(avr_110, "avr_110.bin", CRC(e28df0a8) SHA1(7bc42d329efcb59d71af1736d8881c14ce3f7e5e), 0x01D53CA4)
SAM_ROMEND
SAM_ROMLOAD(avr_120h, "avr_120h.bin", CRC(85a55e02) SHA1(204d796c2cbc776c1305dabade6306527122a13e), 0x01D53CA4)
SAM_ROMEND
SAM_ROMLOAD(avr_200, "avr_200.bin", CRC(dc225785) SHA1(ecaba25a470bf03e6e43ab8779d14898e1b8e67f), 0x01D53CA4)
SAM_ROMEND

SAM_INPUT_PORTS_START(avr, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(avr, 106, "Avatar Pro (V1.06)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(avr, 110, 106, "Avatar Pro (V1.10)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(avr, 120h, 106, "Avatar Limited/Premium Edition (V1.20)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(avr, 200, 106, "Avatar Pro (V2.00)", 2011, "Stern", sam, 0)

//Rolling Stones - ?? Seems ok
INITGAME(rsn, GEN_SAM, sam_dmd128x32, SAM_0COL, SAM_NOMINI);

SAM_ROMLOAD(rsn_110, "rsn_110.bin", CRC(f4aad67f) SHA1(f5dc335a2b9cc92b3da9a33e24cd0b155c6385aa), 0x01EB4FC4)
SAM_ROMEND
SAM_ROMLOAD(rsn_110h, "rsn_110h.bin", CRC(f5122852) SHA1(b92461983d7a3b55ac8be4df4def1b4ca12327af), 0x01EB50CC)
SAM_ROMEND

SAM_INPUT_PORTS_START(rsn, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(rsn, 110, "Rolling Stones Pro (V1.10)", 2011, "Stern", sam, 0)
CORE_CLONEDEF(rsn, 110h, 110, "Rolling Stones Limited/Premium Edition (V1.10)", 2011, "Stern", sam, 0)

//ACDC
 
#define SAM_ROMLOAD_ACDC1(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x04800000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x08000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000)
 
#define SAM_ROMLOAD_ACDC2(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x05000000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x08000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000)
 
#define SAM_ROMLOAD_ACDC3(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x07300000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x0A800000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x09000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x09800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x0A000000, 0x00300000)
 
INITGAME(acd, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);
 
SAM_ROMLOAD_ACDC1(acd_121, "acdc_121.bin", CRC(4f5f43e9) SHA1(19045e9cdb2522770013c24c6fed265009278dea), 0x03D8F40C)
SAM_ROMEND
SAM_ROMLOAD_ACDC1(acd_130, "acdc_130.bin", CRC(da97014e) SHA1(f0a2684076008b0234c089fea8f95e4f3d8816dd), 0x040C4038)
SAM_ROMEND
SAM_ROMLOAD_ACDC1(acd_140, "acdc_140.bin", CRC(43bbbf54) SHA1(33e3795ab850dfab1fd8b1b4f6364a696cc62aa9), 0x040C4038)
SAM_ROMEND
SAM_ROMLOAD_ACDC1(acd_152, "acdc_152.bin", CRC(78cef38c) SHA1(656acabed2241587f512cdd53a095228d9642d1b), 0x04185458)
SAM_ROMEND
SAM_ROMLOAD_ACDC1(acd_152h, "acdc_152h.bin", CRC(bbf6b303) SHA1(8f29a5e8b5503df59ec8a6039a36e78cf7d871a9), 0x0414A0E8)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_160, "acdc_160.bin", CRC(6b98a14c) SHA1(a34841b1e136c9647c89f83e2bf59ecdccb2a0fb), 0x04E942C8)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_160h, "acdc_160h.bin", CRC(733f15a4) SHA1(61e96ceac327387e84b8e24467aee2f5c0a8ce97), 0x04E942E0)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_161, "acdc_161.bin", CRC(a0c27c59) SHA1(83d19fe6b344eb95866f7d5179b65ed26938b9da), 0x04E58B6C)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_161h, "acdc_161h.bin", CRC(1c66055b) SHA1(f33e5bd5753acc90202565639b6a8d22d6380054), 0x04E58B6C)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_163, "acdc_163.bin", CRC(0bf53436) SHA1(0758d4a881ce87c9af90132741bf1e5c89fc575b), 0x04E5EFE8)
SAM_ROMEND
SAM_ROMLOAD_ACDC2(acd_163h, "acdc_163h.bin", CRC(12c404e8) SHA1(e3a5937abaa9e5b4b18b214b4f5c74f1c110247f), 0x04E5EFE8)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(acd_165, "acdc_165.bin", CRC(819b2b35) SHA1(f29814ba985a5887f5cd382666e7f14f8d6e3702), 0x07223FA0)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(acd_165h, "acdc_165h.bin", CRC(9f9c41e9) SHA1(b4a61944218ab57af128e91b032a82342c8c4ccc), 0x07223FA0)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(acd_168, "acdc_168.bin", CRC(9fdcb32e) SHA1(f36b289e1868a051f4302b2551750b750fa52e30), 119685024)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(acd_168h, "acdc_168h.bin", CRC(5a4246a1) SHA1(725eb666ffaef894d2bd694d412658395c7fa7f9), 0x07223FA0)
SAM_ROMEND
SAM_INPUT_PORTS_START(acd, 1) SAM_INPUT_PORTS_END
 
CORE_GAMEDEF(acd, 121, "AC/DC Pro (V1.21)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 130, 121, "AC/DC Pro (V1.30)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 140, 121, "AC/DC Pro (V1.40)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 152, 121, "AC/DC Pro (V1.52)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 152h, 121, "AC/DC Limited Edition (V1.52)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 160, 121, "AC/DC Pro (V1.60)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 160h, 121, "AC/DC Limited Edition (V1.60)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 161, 121, "AC/DC Pro (V1.61)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 161h, 121, "AC/DC Limited Edition (V1.61)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(acd, 163, 121, "AC/DC Pro (V1.63)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(acd, 163h, 121, "AC/DC Limited Edition (V1.63)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(acd, 165, 121, "AC/DC Pro (V1.65)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(acd, 165h, 121, "AC/DC Limited Edition (V1.65)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(acd, 168, 121, "AC/DC Pro (V1.68)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(acd, 168h, 121, "AC/DC Limited Edition (V1.68)", 2014, "Stern", sam, 0)

//X-Men
INITGAME(xmn, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(xmn_100, "xmn_100.bin", CRC(997b2973) SHA1(68bb379860a0fe5be6a8a8f28b6fd8fe640e172a), 0x01FB7DEC)
SAM_ROMEND
SAM_ROMLOAD(xmn_102, "xmn_102.bin", CRC(5df923e4) SHA1(28f86abc792008aa816d93e91dcd9b62fd2d01ee), 0x01FB7DEC)
SAM_ROMEND
SAM_ROMLOAD(xmn_121h, "xmn_121h.bin", CRC(7029ce71) SHA1(c7559ed963e18eecb8115214a3e154874c214f89), 0x01FB7DEC)
SAM_ROMEND
SAM_ROMLOAD(xmn_104, "xmn_104.bin", CRC(59f2e106) SHA1(10e9fb0ec72462654c0e4fb53c5cc9f2cbb3dbcb), 0x01FB7DEC)
SAM_ROMEND
SAM_ROMLOAD(xmn_123h, "xmn_123h.bin", CRC(66c74598) SHA1(c0c0cd2e8e37eba6668aaadab76325afca103b32), 0x01FB7DEC)
SAM_ROMEND
SAM_ROMLOAD(xmn_105, "xmn_105.bin", CRC(e585d64b) SHA1(6126b29c9355398bac427e1b214e58e8e407bec4), 0x01FB850C)
SAM_ROMEND
SAM_ROMLOAD(xmn_124h, "xmn_124h.bin", CRC(662591e9) SHA1(1abb26c589fbb1b5a4ec5577a4a842e8a84484a3), 0x01FB850C)
SAM_ROMEND
SAM_ROMLOAD(xmn_130, "xmn_130.bin", CRC(1fff4f39) SHA1(e8c02ab980499fbb81569ce1f191d0d2e5c13234), 0x01FB887C)
SAM_ROMEND
SAM_ROMLOAD(xmn_130h, "xmn_130h.bin", CRC(b2a7f125) SHA1(a42834c3e562c239f56c27c0cb65885fdffd261c), 0x01FB887C)
SAM_ROMEND
SAM_ROMLOAD(xmn_150, "xmn_150.bin", CRC(fc579436) SHA1(2aa71da4a5f61165e41e7a63f3534202880c3b90), 0x01FA5268)
SAM_ROMEND
SAM_ROMLOAD(xmn_150h, "xmn_150h.bin", CRC(8e2c3870) SHA1(ddfb4370bb4f32d440538f1432d1be09df9b5557), 0x01FA5268)
SAM_ROMEND
SAM_ROMLOAD(xmn_151, "xmn_151.bin", CRC(84c744a4) SHA1(db4339be7e9d47c46a13f95520dfe58da8450a19), 0x01FA5268)
SAM_ROMEND
SAM_ROMLOAD(xmn_151h, "xmn_151h.bin", CRC(21d1088f) SHA1(9a0278c0324fbf549b5b7bcc93bc327f3eb65e19), 0x01FA5268)
SAM_ROMEND

SAM_INPUT_PORTS_START(xmn, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(xmn, 100, "X-Men Pro (V1.00)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 102, 100, "X-Men Limited Edition (V1.02)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 121h, 100, "X-Men Limited Edition (V1.21)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 104, 100, "X-Men Pro (V1.04)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 123h, 100, "X-Men Limited Edition (V1.23)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 105, 100, "X-Men Pro (V1.05)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 124h, 100, "X-Men Limited Edition (V1.24)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 130, 100, "X-Men Pro (V1.30)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 130h, 100, "X-Men Limited Edition (V1.30)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 150, 100, "X-Men Pro (V1.50)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 150h, 100, "X-Men Limited Edition (V1.50)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 151, 100, "X-Men Pro (V1.51)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(xmn, 151h, 100, "X-Men Limited Edition (V1.51)", 2014, "Stern", sam, 0)

//Avengers
INITGAME(avs, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);

SAM_ROMLOAD(avs_110, "avs_110.bin", CRC(2cc01e3c) SHA1(0ae7c9ced7e1d48b0bf4afadb6db508e558a7ebb), 30421676)
SAM_ROMEND
SAM_ROMLOAD(avs_120h, "avs_120h.bin", CRC(a74b28c4) SHA1(35f65691312c547ec6c6bf52d0c5e330b5d464ca), 31617232)
SAM_ROMEND
SAM_ROMLOAD(avs_140, "avs_140.bin", CRC(92642508) SHA1(1d55cd178104b43377f079fd0209d74d1b10bea8), 0x01F2EDA0)
SAM_ROMEND
SAM_ROMLOAD(avs_140h, "avs_140h.bin", CRC(9b7e13f8) SHA1(eb97e92013a8d1d706a119b50d36d69eb26cb273), 0x01F2EDA0)
SAM_ROMEND

SAM_INPUT_PORTS_START(avs, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(avs, 110, "Avengers Pro (V1.10)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(avs, 120h, 110, "Avengers Limited Edition (V1.20)", 2012, "Stern", sam, 0)
CORE_CLONEDEF(avs, 140, 110, "Avengers Pro (V1.40)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(avs, 140h, 110, "Avengers Limited Edition (V1.40)", 2013, "Stern", sam, 0)

//Metallica
 
#define SAM_ROMLOAD_MTL1(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x05000000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x08000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000)
 
#define SAM_ROMLOAD_MTL2(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x05800000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x08800000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08000000, 0x00800000)
 
#define SAM_ROMLOAD_MTL3(name, n1, chk1, size) \
  ROM_START(name) \
    ROM_REGION(0x06000000, SAM_ROMREGION, 2) \
      ROM_LOAD(n1, 0x00000000, size, chk1) \
    ROM_REGION(0x09000000, SAM_CPU1REGION, SAM_CPU2REGION) \
    ROM_COPY( SAM_ROMREGION, 0, 0x00000000, 0x00FFFFFF) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x04800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x05800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x06800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x07800000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08000000, 0x00800000) \
    ROM_COPY( SAM_ROMREGION, 0, 0x08800000, 0x00800000)

INITGAME(mtl, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI);
 
SAM_ROMLOAD_MTL1(mtl_103, "mtl_103.bin", CRC(9b073858) SHA1(129872e38d21d9d6d20f81388825113f13645bab), 0x04D24D04)
SAM_ROMEND
SAM_ROMLOAD_MTL1(mtl_105, "mtl_105.bin", CRC(4699e2cf) SHA1(b56e85583362056b33f7b8eb6255d34d234ea5ea), 0x04DEDE5C)
SAM_ROMEND
SAM_ROMLOAD_MTL1(mtl_106, "mtl_106.bin", CRC(5ac6c70a) SHA1(aaa68eebd1b894383416d2a491ac074a73be8d91), 0x04E8A214)
SAM_ROMEND
SAM_ROMLOAD_MTL1(mtl_112, "mtl_112.bin", CRC(093ba7ef) SHA1(e49810ca3500be503343296105ea9dd85e2c00f0), 0x04FDCD28)
SAM_ROMEND
SAM_ROMLOAD_MTL1(mtl_113, "mtl_113.bin", CRC(be73c2e7) SHA1(c91bbb554aaa21520360773e1215fe80557d6c2f), 0x04FDCD28)
SAM_ROMEND
SAM_ROMLOAD_MTL1(mtl_113h, "mtl_113h.bin", CRC(3392e27c) SHA1(3cfa5c5fdc51bd6886ffe6865739cf71de145ef1), 0x04FDCD28)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_116, "mtl_116.bin", CRC(85793613) SHA1(454813cb405bb6bda1d26288b10606c3a4ec72fc), 0x050FFF04)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_116h, "mtl_116h.bin", CRC(3a96d383) SHA1(671fae4565c0739d98c9c40b05b9b41ae7917671), 0x050FFF04)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_120, "mtl120.bin", CRC(43028b40) SHA1(64ab9306b28f3dc59e645ce49dbf3468e7f590bd), 0x0512C72C)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_120h, "mtl120h.bin", CRC(1c5b4643) SHA1(8075e7ecf1031fc89e75cc5a5487340bc3fae507), 0x0512C72C)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_122, "mtl122.bin", CRC(5201e6a6) SHA1(46e76f3e518448627419edf1aa08cc42259b39d2), 0x0512C72C)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_122h, "mtl122h.bin", CRC(0d439a2b) SHA1(91eb84184cb93cfa83e0129a448760ac6586e85d), 0x0512C72C)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_150, "mtl150.bin", CRC(647cbad4) SHA1(d49906906a075a656d7768bffc47bd88a6306699), 0x0542CA94)
SAM_ROMEND
SAM_ROMLOAD_MTL2(mtl_150h, "mtl150h.bin", CRC(9f314ca1) SHA1(b620c4742a3ce137cbb099857d96fb6af67b7fec), 0x0542CA94)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_151, "mtl151.bin", CRC(dac2819e) SHA1(c57fe4252a7a84cd543458bc54038b6ae9d79816), 0x05D44000)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_151h, "mtl151h.bin", CRC(18e5a613) SHA1(ebd697bdc8f67188e160e2f8e76f908206127d26), 0x05D44000)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_160, "mtl160.bin", CRC(c440d2f5) SHA1(4584542430579f9ff0174f4dd2817afbc778bc40), 0x05DD011C)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_160h, "mtl160h.bin", CRC(cb69b0fe) SHA1(aa7275f33db95742a8b1ae8a5f6973a0b27953fa), 0x05DD011C)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_163, "mtl163.bin", CRC(94d38355) SHA1(0f51c3d99e1227dcde132738ef539d0d452ca003), 0x05DD0138)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_163d, "mtl163d.bin", CRC(de390393) SHA1(23a9f02514bc592e0799c91cd42786809bfc8c1d), 0x05DD0138)
SAM_ROMEND
SAM_ROMLOAD_ACDC3(mtl_163h, "mtl163h.bin", CRC(12c1a5bb) SHA1(701eda4251ebdfcce2bea3ec9c84ac9c35832e2f), 0x05DD0138)
SAM_ROMEND
 
SAM_INPUT_PORTS_START(mtl, 1) SAM_INPUT_PORTS_END
 
CORE_GAMEDEF(mtl, 103, "Metallica Pro (V1.03)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 105, 103, "Metallica Pro (V1.05)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 106, 103, "Metallica Pro (V1.06)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 112, 103, "Metallica Pro (V1.12)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 113, 103, "Metallica Pro (V1.13)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 113h, 103, "Metallica Limited Edition (V1.13)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 116, 103, "Metallica Pro (V1.16)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 116h, 103, "Metallica Limited Edition (V1.16)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 120, 103, "Metallica Pro (V1.20)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 120h, 103, "Metallica Limited Edition (V1.20)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 122, 103, "Metallica Pro (V1.22)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 122h, 103, "Metallica Limited Edition (V1.22)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 150, 103, "Metallica Pro (V1.50)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 150h, 103, "Metallica Limited Edition (V1.50)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 151, 103, "Metallica Pro (V1.51)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 151h, 103, "Metallica Limited Edition (V1.51)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 160, 103, "Metallica Pro (V1.60)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 160h, 103, "Metallica Limited Edition (V1.60)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 163, 103, "Metallica Pro (V1.63)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 163d, 103, "Metallica Pro Led (V1.63)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mtl, 163h, 103, "Metallica Limited Edition (V1.63)", 2014, "Stern", sam, 0)

//Star Trek

INITGAME(st, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI); //!! SAM_8COL ?

SAM_ROMLOAD64(st_120, "st_120.bin", CRC(dde9db23) SHA1(09e67564bce0ff7c67f1d16c4f9d8595f8130372), 45174012)
SAM_ROMEND
SAM_ROMLOAD64(st_130, "st_130.bin", CRC(f501eb87) SHA1(6fa2f4e30cdd397d5443dfc690463495d22d9229), 45554328)
SAM_ROMEND
SAM_ROMLOAD64(st_140, "st_140.bin", CRC(c4f97ce3) SHA1(ef2d7cef153b5a6e9ab90c1ea31fdf5667eb327f), 49791896)
SAM_ROMEND
SAM_ROMLOAD64(st_140h, "st_140h.bin", CRC(6f84cec4) SHA1(d92391005eed3c4dcb66ac0bccd19a50c4120792), 49791896)
SAM_ROMEND
SAM_ROMLOAD64(st_141h, "st_141h.bin", CRC(ae20d360) SHA1(0a840767b4e9fee26d7a4c2a9545fa7fd818d74e), 0x02F7C398)													
SAM_ROMEND
SAM_ROMLOAD64(st_142h, "st_142h.bin", CRC(01acc115) SHA1(9881ea34852890a3fc960b78db96b70f17a28e56), 49791896)
SAM_ROMEND
SAM_ROMLOAD64(st_150, "st_150.bin", CRC(979c9644) SHA1(20a89ad337690a9ab652a599a77e30ccf2018e14), 56306580)
SAM_ROMEND
SAM_ROMLOAD64(st_150h, "st_150h.bin", CRC(a187581c) SHA1(b68ca52140bafd6b309b120d38df5b3bcf633a13), 0x035B2B94)													
SAM_ROMEND

SAM_INPUT_PORTS_START(st, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(st, 120, "Star Trek Pro (V1.20)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(st, 130, 120, "Star Trek Pro (V1.30)", 2013, "Stern", sam, 0)
CORE_CLONEDEF(st, 140, 120, "Star Trek Pro (V1.40)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(st, 140h, 120, "Star Trek Limited Edition (V1.40h)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(st, 141h, 120, "Star Trek Limited Edition (V1.41)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(st, 142h, 120, "Star Trek Limited Edition (V1.42h)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(st, 150, 120, "Star Trek Pro (V1.50)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(st, 150h, 120, "Star Trek Limited Edition (V1.50)", 2014, "Stern", sam, 0)

//Mustang

INITGAME(mt, GEN_SAM, sam_dmd128x32, SAM_2COL, SAM_NOMINI); //!! SAM_8COL ?

SAM_ROMLOAD64(mt_120, "mt_120.bin", CRC(be7437ac) SHA1(5db10d7f48091093c33d522a663f13f262c08c3e), 58566124)
SAM_ROMEND
SAM_ROMLOAD64(mt_130, "mt_130.bin", CRC(b6086db1) SHA1(0a50864b0de1b4eb9a764f36474b6fddea767c0d), 52647740)
SAM_ROMEND
SAM_ROMLOAD64(mt_130h, "mt_130h.bin", CRC(dcb5c923) SHA1(cf9e6042ae33080368ecffac233379135bf680ae), 0x03BF07F0)													
SAM_ROMEND
SAM_ROMLOAD64(mt_140, "mt_140.bin", CRC(48010b61) SHA1(1bc615a86c4718ff407116a4e637e38e8386ded0), 52851520)
SAM_ROMEND

SAM_INPUT_PORTS_START(mt, 1) SAM_INPUT_PORTS_END

CORE_GAMEDEF(mt, 120, "Mustang Pro (V1.20)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mt, 130, 120, "Mustang Pro (V1.30)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mt, 130h, 120 , "Mustang Limited Edition (V1.30)", 2014, "Stern", sam, 0)
CORE_CLONEDEF(mt, 140, 120, "Mustang Pro (V1.40)", 2014, "Stern", sam, 0)

//Walking Dead
 
INITGAME(twd, GEN_SAM, sam_dmd128x32, SAM_8COL, SAM_NOMINI);
 
SAM_ROMLOAD_ACDC3(twd_105, "twd_105.bin", CRC(59b4e4d6) SHA1(642e827d58c9877a9f3c29b75784660894f045ad), 0x04F4FBF8)
SAM_ROMEND
 
SAM_INPUT_PORTS_START(twd, 1) SAM_INPUT_PORTS_END
 
CORE_GAMEDEF(twd, 105, "The Walking Dead (V1.05)", 2014, "Stern", sam, 0)

#endif