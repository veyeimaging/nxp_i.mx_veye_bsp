
#ifndef CS_IMX307_H
#define CS_IMX307_H

#define CSIMX307_WAIT_MS_CMD	5
#define CSIMX307_WAIT_MS_STREAM	5

typedef enum 
{
	CS_MIPI_IMX307 = 0x0037,	
	CS_LVDS_IMX307 = 0x0038,	
	CS_USB_IMX307 = 0x0039,	
	CS_MIPI_SC132 = 0x0132,	
	CS_LVDS_SC132 = 0x0133,	
	CS_USB_SC132 = 0x0134
}ENProductID;

typedef enum
{
    deviceID = 0x00,
    HardWare = 0x01,
    LoadingDone = 0x02,

    Csi2_Enable = 0x03,
    Fpga_CAP_L = 0x04,
    Fpga_CAP_H = 0x05,
    
    TriggerMode = 0x10,
    SlaveMode = 0x11,
    TrigDly_H = 0x12,
    TrigDly_M = 0x13,
    TrigDly_U = 0x14,
    TrigDly_L = 0x15,
    VerTotalTime_H = 0x16,
    VerTotalTime_L = 0x17,
    HorTotalTime_H = 0x18,
    HorTotalTime_L = 0x19,
    YUV_SEQ = 0x28,
    //for arm part
    //for arm part
    ARM_VER_L = 0x0100,
    ARM_VER_H = 0x0101,
    PRODUCTID_L = 0x0102,
    PRODUCTID_H = 0x0103,
    SYSTEM_RESET = 0x0104,
    PARAM_SAVE = 0x0105,
    VIDEOFMT_CAP = 0x0106, 
    
    VIDEOFMT_NUM = 0x0107,

    FMTCAP_WIDTH_L = 0x0108,
    FMTCAP_WIDTH_H = 0x0109,
    FMTCAP_HEIGHT_L = 0x010A,
    FMTCAP_HEIGHT_H = 0x010B,
    FMTCAP_FRAMRAT_L = 0x010C,
    FMTCAP_FRAMRAT_H = 0x010D,
    
    FMT_WIDTH_L = 0x0180,
    FMT_WIDTH_H = 0x0181,
    FMT_HEIGHT_L = 0x0182,
    FMT_HEIGHT_H = 0x0183,
    FMT_FRAMRAT_L = 0x0184,
    FMT_FRAMRAT_H = 0x0185,
    IMAGE_DIR = 0x0186,
    SYSTEM_REBOOT = 0x0187,
    NEW_FMT_FRAMRAT_MODE = 0x0188,
    NEW_FMT_FRAMRAT_L = 0x0189,
    NEW_FMT_FRAMRAT_H = 0x018A,
    
    EXP_FRM_MODE = 0x020F,
    AE_MODE = 0x0210,
    EXP_TIME_L = 0x0211,
    EXP_TIME_M = 0x0212,
    EXP_TIME_H = 0x0213,
    EXP_TIME_E = 0x0214,

    AGAIN_NOW_DEC = 0x0215,
    AGAIN_NOW_INTER = 0x0216,
    DGAIN_NOW_DEC = 0x0217,
    DGAIN_NOW_INTER = 0x0218,

    AE_SPEED = 0x0219,
    AE_TARGET = 0x021A,
    AE_MAXTIME_L = 0x021B,
    AE_MAXTIME_M = 0x021C,
    AE_MAXTIME_H = 0x021D,
    AE_MAXTIME_E = 0x021E,
    AE_MAXGAIN_DEC = 0x021F,
    AE_MAXGAIN_INTER = 0x0220,
    //ISP cap
    ISP_CAP_L = 0x0200,
    ISP_CAP_M = 0x0201,
    ISP_CAP_H = 0x0202,
    ISP_CAP_E = 0x0203,
    POWER_HZ = 0x0204,
}ECAMERA_REG;

enum yuv_order {
	YUV_ORDER_YVYU = 0,
    YUV_ORDER_YUYV = 1,
};

enum csimx307_mode{
	CSIMX307_mode_MIN = 0,
	CSIMX307_MODE_1920X1080_30FPS = 0,
    CSIMX307_MODE_1280X720_CROP_60FPS = 1,
    CSIMX307_MODE_640X480_CROP_130FPS = 2,
	CSIMX307_mode_MAX = 2,
	CSIMX307_mode_INIT = 0xff, /*only for sensor init*/
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

struct csimx307_mode_info {
	enum csimx307_mode mode;
	u32 width;
	u32 height;
    u32 max_framerate; // add here
	struct reg_value *init_data_ptr;
	u32 init_data_size;
};

struct veye_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

static const struct veye_datafmt veye_colour_fmts[] = {
	{MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_REC709},
    {MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_REC709},
};

static struct reg_value csimx307_reg_1920x1080_30fps[] = {
    {FMT_WIDTH_L,0x80,0,0},
    {FMT_WIDTH_H,0x7,0,CSIMX307_WAIT_MS_CMD},
    {FMT_HEIGHT_L,0x38,0,0},
    {FMT_HEIGHT_H,0x4,0,CSIMX307_WAIT_MS_CMD},
    {FMT_FRAMRAT_L,0x1E,0,0},
    {FMT_FRAMRAT_H,0x00,0,CSIMX307_WAIT_MS_STREAM},
};

static struct reg_value csimx307_reg_1280x720_crop_60fps[] = {
    {FMT_WIDTH_L,0x00,0,0},
    {FMT_WIDTH_H,0x5,0,CSIMX307_WAIT_MS_CMD},
    {FMT_HEIGHT_L,0xD0,0,0},
    {FMT_HEIGHT_H,0x2,0,CSIMX307_WAIT_MS_CMD},
    {FMT_FRAMRAT_L,0x3C,0,0},
    {FMT_FRAMRAT_H,0x00,0,CSIMX307_WAIT_MS_STREAM},
};

static struct reg_value csimx307_reg_640x480_crop_130fps[] = {
    {FMT_WIDTH_L,0x80,0,0},
    {FMT_WIDTH_H,0x2,0,CSIMX307_WAIT_MS_CMD},
    {FMT_HEIGHT_L,0xE0,0,0},
    {FMT_HEIGHT_H,0x1,0,CSIMX307_WAIT_MS_CMD},
    {FMT_FRAMRAT_L,0x82,0,0},
    {FMT_FRAMRAT_H,0x00,0,CSIMX307_WAIT_MS_STREAM},
};

static struct csimx307_mode_info csimx307_mode_info_data[CSIMX307_mode_MAX+1] = {
	{
		CSIMX307_MODE_1920X1080_30FPS, 1920, 1080,30,
		csimx307_reg_1920x1080_30fps,
		ARRAY_SIZE(csimx307_reg_1920x1080_30fps)
	},
	{
		CSIMX307_MODE_1280X720_CROP_60FPS, 1280, 720,60,
		csimx307_reg_1280x720_crop_60fps,
		ARRAY_SIZE(csimx307_reg_1280x720_crop_60fps)
	},
    {
		CSIMX307_MODE_640X480_CROP_130FPS, 640, 480,130,
		csimx307_reg_640x480_crop_130fps,
		ARRAY_SIZE(csimx307_reg_640x480_crop_130fps)
	},
};

struct csimx307 {
	struct v4l2_subdev		subdev;
	struct i2c_client *i2c_client;
	struct v4l2_pix_format pix;
	const struct veye_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
    u32 framerate;
	bool on;

	/* control settings */
	int brightness;
	int hue;
	int contrast;
	int saturation;
	int red;
	int green;
	int blue;
	int ae_mode;

	u32 mclk;
	u8 mclk_source;
	struct clk *sensor_clk;
	int csi;

	void (*io_init)(struct csimx307 *);
	int pwn_gpio, rst_gpio;
};

#endif
