
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define VEYE327_VOLTAGE_ANALOG               3300000
#define VEYE327_VOLTAGE_DIGITAL_CORE         1500000//do not use
#define VEYE327_VOLTAGE_DIGITAL_IO           2000000

#define MIN_FPS 25
#define MAX_FPS 30
#define DEFAULT_FPS 30

//we do not use this
#define VEYE327_XCLK_MIN 6000000
#define VEYE327_XCLK_MAX 24000000

/* veye327 sensor register address */
#define VEYE327_MODEL_ID_ADDR		0x0001
#define VEYE327_REG_STREAM_ON       0x001d
#define VEYE327_REG_YUV_SEQ         0x001e


enum veye327_mode {
	veye327_mode_MIN = 0,
	veye327_mode_1080P_1920_1080 = 0,
	veye327_mode_MAX = 1,
	veye327_mode_INIT = 0xff, /*only for sensor init*/
};

enum veye327_frame_rate {
	veye327_25_fps,
	veye327_30_fps
};

static int veye327_framerates[] = {
	[veye327_25_fps] = 25,
	[veye327_30_fps] = 30,
};

struct veye327_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

struct veye327_mode_info {
	enum veye327_mode mode;
	u32 width;
	u32 height;
	struct reg_value *init_data_ptr;
	u32 init_data_size;
};

struct veye327 {
	struct v4l2_subdev		subdev;
	struct i2c_client *i2c_client;
	struct v4l2_pix_format pix;
	const struct veye327_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
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

	void (*io_init)(struct veye327 *);
	int pwn_gpio, rst_gpio;
};
/*!
 * Maintains the information on the current state of the sesor.
 */

static struct reg_value veye327_init_setting[] = {
//nothing
};

static struct reg_value veye327_setting_25fps_1080P_1920_1080[] = {
    {0x0010, 0xDE,0,0},
    {0x0011, 0xC2,0,0},
    {0x0012, 0x00,0,0},
    {0x0013, 0x00,0,0},
};
static struct reg_value veye327_setting_30fps_1080P_1920_1080[] = {
    {0x0010, 0xDE,0,0},
    {0x0011, 0xC2,0,0},
    {0x0012, 0x01,0,0},
    {0x0013, 0x00,0,0},
};

static struct veye327_mode_info veye327_mode_info_data[2][veye327_mode_MAX + 1] = {
	{
		{veye327_mode_1080P_1920_1080, 1920, 1080,
		veye327_setting_25fps_1080P_1920_1080,
		ARRAY_SIZE(veye327_setting_25fps_1080P_1920_1080)},
	},
	{
		{veye327_mode_1080P_1920_1080, 1920, 1080,
		veye327_setting_30fps_1080P_1920_1080,
		ARRAY_SIZE(veye327_setting_30fps_1080P_1920_1080)},
	},
};

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;
static struct regulator *gpo_regulator;
static DEFINE_MUTEX(veye327_mutex);

static int veye327_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int veye327_remove(struct i2c_client *client);

static s32 veye327_read_reg(struct veye327 *sensor,u16 reg, u8 *val);
static s32 veye327_write_reg(struct veye327 *sensor,u16 reg, u8 val);

static const struct i2c_device_id veye327_id[] = {
	{"veye327_mipi", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, veye327_id);

#ifdef CONFIG_OF
static const struct of_device_id veye327_mipi_v2_of_match[] = {
	{ .compatible = "veye,veye327_mipi",
	},
	{ /* sentinel */ }
};

static struct i2c_driver veye327_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "veye327_mipi",
    #ifdef CONFIG_OF
		  .of_match_table = of_match_ptr(veye327_mipi_v2_of_match),
    #endif
		  },
	.probe  = veye327_probe,
	.remove = veye327_remove,
	.id_table = veye327_id,
};

static const struct veye327_datafmt veye327_colour_fmts[] = {
	{MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_REC709},
};

//todo here
/*
static struct veye327 veye327_data;
static int pwn_gpio, rst_gpio;
*/
static struct veye327 *to_veye327(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct veye327, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct veye327_datafmt
			*veye327_find_datafmt(u32 code)
{
	int i;
   // dev_dbg(dev,"%s:find code %d\n", __func__,code);
	for (i = 0; i < ARRAY_SIZE(veye327_colour_fmts); i++)
		if (veye327_colour_fmts[i].code == code)
        {
         //   dev_dbg(dev,"%s:find code found %d\n", __func__,i);
			return veye327_colour_fmts + i;
        }

	return NULL;
}

static inline void veye327_power_down(struct veye327 *sensor,int enable)
{
    return;
/*	if (sensor->pwn_gpio < 0)
		return;
//327s do not support powerdown now.
	if (!enable)
		gpio_set_value_cansleep(sensor->pwn_gpio, 0);
	else
		gpio_set_value_cansleep(sensor->pwn_gpio, 1);

	msleep(2);*/
    
}

static void veye327_reset(struct veye327 *sensor)
{
	if (sensor->rst_gpio < 0 || sensor->pwn_gpio < 0)
		return;

	/* camera reset */
	gpio_set_value_cansleep(sensor->rst_gpio, 0);
    msleep(5);
    gpio_set_value_cansleep(sensor->rst_gpio, 1);
    msleep(500);

}

static int veye327_regulator_enable(struct device *dev)
{
	int ret = 0;

	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      VEYE327_VOLTAGE_DIGITAL_IO,
				      VEYE327_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(io_regulator);
		if (ret) {
			dev_err(dev,"%s:io set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:io set voltage ok\n", __func__);
		}
	} else {
		dev_err(dev,"%s: cannot get io voltage error\n", __func__);
		io_regulator = NULL;
	}

	core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(core_regulator)) {
		regulator_set_voltage(core_regulator,
				      VEYE327_VOLTAGE_DIGITAL_CORE,
				      VEYE327_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(core_regulator);
		if (ret) {
			dev_err(dev,"%s:core set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:core set voltage ok\n", __func__);
		}
	} else {
		core_regulator = NULL;
		dev_err(dev,"%s: cannot get core voltage error\n", __func__);
	}

	analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      VEYE327_VOLTAGE_ANALOG,
				      VEYE327_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			dev_err(dev,"%s:analog set voltage error\n",
				__func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:analog set voltage ok\n", __func__);
		}
	} else {
		analog_regulator = NULL;
		dev_err(dev,"%s: cannot get analog voltage error\n", __func__);
	}

	return ret;
}



MODULE_DEVICE_TABLE(of, ov5640_mipi_v2_of_match);
#endif


static s32 veye327_write_reg(struct veye327 *sensor,u16 reg, u8 val)
{
	u8 au8Buf[3] = {0};
    struct device *dev = &sensor->i2c_client->dev;
	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(sensor->i2c_client, au8Buf, 3) < 0) {
		dev_err(dev,"%s:write reg error:reg=%x,val=%x\n",
			__func__, reg, val);
		return -1;
	}

	return 0;
}

static s32 veye327_read_reg(struct veye327 *sensor,u16 reg, u8 *val)
{
	u8 au8RegBuf[2] = {0};
	u8 u8RdVal = 0;
    struct device *dev = &sensor->i2c_client->dev;
	au8RegBuf[0] = reg >> 8;
	au8RegBuf[1] = reg & 0xff;

	if (2 != i2c_master_send(sensor->i2c_client, au8RegBuf, 2)) {
		dev_err(dev,"%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
	}

	if (1 != i2c_master_recv(sensor->i2c_client, &u8RdVal, 1)) {
		dev_err(dev,"%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, u8RdVal);
		return -1;
	}

	*val = u8RdVal;

	return u8RdVal;
}


static void veye327_stream_on(struct veye327 *sensor)
{
	veye327_write_reg(sensor,VEYE327_REG_STREAM_ON, 0x01);
}

static void veye327_stream_off(struct veye327 *sensor)
{
	veye327_write_reg(sensor,VEYE327_REG_STREAM_ON, 0x00);
}
/* download veye327 settings to sensor through i2c */
static int veye327_download_firmware(struct veye327 *sensor,struct reg_value *pModeSetting, s32 ArySize)
{
	register u32 Delay_ms = 0;
	register u16 RegAddr = 0;
	register u8 Mask = 0;
	register u8 Val = 0;
	u8 RegVal = 0;
	int i, retval = 0;

	for (i = 0; i < ArySize; ++i, ++pModeSetting) {
		Delay_ms = pModeSetting->u32Delay_ms;
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u8Val;
		Mask = pModeSetting->u8Mask;

		if (Mask) {
			retval = veye327_read_reg(sensor,RegAddr, &RegVal);
			if (retval < 0)
				goto err;

			RegVal &= ~(u8)Mask;
			Val &= Mask;
			Val |= RegVal;
		}

		retval = veye327_write_reg(sensor,RegAddr, Val);
		if (retval < 0)
			goto err;

		if (Delay_ms)
			msleep(Delay_ms);
	}
err:
	return retval;
}
/* if sensor changes inside scaling or subsampling
 * change mode directly
 * */
static int veye327_change_mode_direct(struct veye327 *sensor,enum veye327_frame_rate frame_rate,
				enum veye327_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;
    struct device *dev = &sensor->i2c_client->dev;
    
	/* check if the input mode and frame rate is valid */
	pModeSetting =
		veye327_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		veye327_mode_info_data[frame_rate][mode].init_data_size;

	sensor->pix.width =
		veye327_mode_info_data[frame_rate][mode].width;
	sensor->pix.height =
		veye327_mode_info_data[frame_rate][mode].height;

	if (sensor->pix.width == 0 || sensor->pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
    {
        dev_err(dev,"veye327_change_mode_direct failed EINVAL! \n");
		return -EINVAL;
    }

	/* Write capture setting */
	retval = veye327_download_firmware(sensor,pModeSetting, ArySize);
	if (retval < 0)
		goto err;

err:
	return retval;
}

static int veye327_init_mode(struct veye327 *sensor,enum veye327_frame_rate frame_rate,
			    enum veye327_mode mode, enum veye327_mode orig_mode)
{
	struct device *dev = &sensor->i2c_client->dev;
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;
	u32 msec_wait4stable = 0;

	if ((mode > veye327_mode_MAX || mode < veye327_mode_MIN)
		&& (mode != veye327_mode_INIT)) {
		dev_err(dev,"Wrong veye327 mode detected!\n");
		return -1;
	}
	if (mode == veye327_mode_INIT) {
		pModeSetting = veye327_init_setting;
		ArySize = ARRAY_SIZE(veye327_init_setting);
		pModeSetting = veye327_setting_30fps_1080P_1920_1080;
		ArySize = ARRAY_SIZE(veye327_setting_30fps_1080P_1920_1080);

		sensor->pix.width = 1920;
		sensor->pix.height = 1080;
	} else{
		/* change inside subsampling or scaling
		 * download firmware directly */
         dev_dbg(dev,"veye327_change_mode_direct framerate %d mode %d\n",frame_rate, mode);
		retval = veye327_change_mode_direct(sensor,frame_rate, mode);
	}
    
	if (retval < 0)
		goto err;
    
	msec_wait4stable = 30;
	msleep(msec_wait4stable);

err:
	return retval;
}

/*!
 * veye327_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int veye327_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);

	if (on && !sensor->on) {
		if (io_regulator)
			if (regulator_enable(io_regulator) != 0)
				return -EIO;
		if (core_regulator)
			if (regulator_enable(core_regulator) != 0)
				return -EIO;
		if (gpo_regulator)
			if (regulator_enable(gpo_regulator) != 0)
				return -EIO;
		if (analog_regulator)
			if (regulator_enable(analog_regulator) != 0)
				return -EIO;
	} else if (!on && sensor->on) {
		if (analog_regulator)
			regulator_disable(analog_regulator);
		if (core_regulator)
			regulator_disable(core_regulator);
		if (io_regulator)
			regulator_disable(io_regulator);
		if (gpo_regulator)
			regulator_disable(gpo_regulator);
	}

	sensor->on = on;

	return 0;
}

/*!
 * veye327_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int veye327_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);
	struct device *dev = &sensor->i2c_client->dev;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		dev_dbg(dev,"   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * veye327_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int veye327_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);
	struct device *dev = &sensor->i2c_client->dev;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum veye327_frame_rate frame_rate;
	enum veye327_mode orig_mode;
	int ret = 0;


	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == 25)
			frame_rate = veye327_25_fps;
		else if (tgt_fps == 30)
			frame_rate = veye327_30_fps;
		else {
			dev_err(dev," The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		orig_mode = sensor->streamcap.capturemode;
		ret = veye327_init_mode(sensor,frame_rate,
				(u32)a->parm.capture.capturemode, orig_mode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				(u32)a->parm.capture.capturemode;

		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		dev_dbg(dev,"type is not " \
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",
			a->type);
		ret = -EINVAL;
		break;

	default:
		dev_dbg(dev,"type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int veye327_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct veye327_datafmt *fmt = veye327_find_datafmt(mf->code);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);

	if (!fmt) {
		mf->code	= veye327_colour_fmts[0].code;
		mf->colorspace	= veye327_colour_fmts[0].colorspace;
        fmt		= &veye327_colour_fmts[0];
	}

	mf->field	= V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	sensor->fmt = fmt;

	return 0;
}

static int veye327_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);
	const struct veye327_datafmt *fmt = sensor->fmt;

	if (format->pad)
		return -EINVAL;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	mf->width	= sensor->pix.width;
	mf->height	= sensor->pix.height;
	return 0;
}

static int veye327_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);
	struct device *dev = &sensor->i2c_client->dev;
    
	if (code->pad || code->index >= ARRAY_SIZE(veye327_colour_fmts))
    {
        dev_dbg(dev," index is %d and not supported!\n",code->index);
		return -EINVAL;
    }
	code->code = veye327_colour_fmts[code->index].code;
    dev_dbg(dev," index is %d format is %d!\n",code->index,code->code);
	return 0;
}

/*!
 * veye327_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int veye327_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > veye327_mode_MAX)
		return -EINVAL;

	fse->max_width =
			max(veye327_mode_info_data[0][fse->index].width,
			    veye327_mode_info_data[1][fse->index].width);
	fse->min_width = fse->max_width;
	fse->max_height =
			max(veye327_mode_info_data[0][fse->index].height,
			    veye327_mode_info_data[1][fse->index].height);
	fse->min_height = fse->max_height;
	return 0;
}

/*!
 * veye327_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int veye327_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	int i, j, count = 0;

	if (fie->index < 0 || fie->index > veye327_mode_MAX)
		return -EINVAL;

	if (fie->width == 0 || fie->height == 0 ||
	    fie->code == 0) {
		dev_warn(dev, "Please assign pixel format, width and height\n");
		return -EINVAL;
	}

	fie->interval.numerator = 1;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(veye327_mode_info_data); i++) {
		for (j = 0; j < (veye327_mode_MAX + 1); j++) {
			if (fie->width == veye327_mode_info_data[i][j].width
			 && fie->height == veye327_mode_info_data[i][j].height
			 && veye327_mode_info_data[i][j].init_data_ptr != NULL) {
				count++;
			}
			if (fie->index == (count - 1)) {
				fie->interval.denominator =
						veye327_framerates[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

/*!
 * dev_init - V4L2 sensor init
 * @s: pointer to standard V4L2 device structure
 *
 */
static int init_device(struct veye327 *sensor)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum veye327_frame_rate frame_rate;
	int ret;
    struct device *dev = &sensor->i2c_client->dev;
	sensor->on = true;

	/* mclk */
	tgt_xclk = sensor->mclk;
	tgt_xclk = min(tgt_xclk, (u32)VEYE327_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)VEYE327_XCLK_MIN);
	sensor->mclk = tgt_xclk;

	dev_dbg(dev,"   Setting mclk to %d MHz\n", tgt_xclk / 1000000);

	/* Default camera frame rate is set in probe */
	tgt_fps = sensor->streamcap.timeperframe.denominator /
		  sensor->streamcap.timeperframe.numerator;

	if (tgt_fps == 25)
		frame_rate = veye327_25_fps;
	else if (tgt_fps == 30)
		frame_rate = veye327_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	ret = veye327_init_mode(sensor,frame_rate, veye327_mode_INIT, veye327_mode_INIT);

	return ret;
}

static int veye327_s_stream(struct v4l2_subdev *sd, int enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct veye327 *sensor = to_veye327(client);
	struct device *dev = &sensor->i2c_client->dev;
	dev_info(dev, "s_stream: %d\n", enable);
	if (enable)
		veye327_stream_on(sensor);
	else
		veye327_stream_off(sensor);
	return 0;
}

static struct v4l2_subdev_video_ops veye327_subdev_video_ops = {
	.g_parm = veye327_g_parm,
	.s_parm = veye327_s_parm,
	.s_stream = veye327_s_stream,
};

static const struct v4l2_subdev_pad_ops veye327_subdev_pad_ops = {
	.enum_frame_size       = veye327_enum_framesizes,
	.enum_frame_interval   = veye327_enum_frameintervals,
	.enum_mbus_code        = veye327_enum_mbus_code,
	.set_fmt               = veye327_set_fmt,
	.get_fmt               = veye327_get_fmt,
};

static struct v4l2_subdev_core_ops veye327_subdev_core_ops = {
	.s_power	= veye327_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= veye327_get_register,
	.s_register	= veye327_set_register,
#endif
};

static struct v4l2_subdev_ops veye327_subdev_ops = {
	.core	= &veye327_subdev_core_ops,
	.video	= &veye327_subdev_video_ops,
	.pad	= &veye327_subdev_pad_ops,
};


/*!
 * veye327 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int veye327_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	struct veye327 *sensor;
	u8 chip_id;
	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	/* veye327 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_warn(dev, "no  pin available\n");
	}

	/* request power down pin */
	sensor->pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(sensor->pwn_gpio)) {
		dev_warn(dev, "no sensor pwdn pin available\n");
		sensor->pwn_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, sensor->pwn_gpio, GPIOF_OUT_INIT_HIGH,
						"veye327_mipi_pwdn");
		if (retval < 0) {
			dev_warn(dev, "Failed to set power pin\n");
			return retval;
		}
	}

	/* request reset pin */
	sensor->rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(sensor->rst_gpio)) {
		dev_warn(dev, "no sensor reset pin available\n");
		sensor->rst_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, sensor->rst_gpio, 
		GPIOF_OUT_INIT_HIGH,"veye327_mipi_reset");
		if (retval < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			return retval;
		}
	}

	sensor->sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(sensor->sensor_clk)) {
		/* assuming clock enabled by default */
		sensor->sensor_clk = NULL;
		dev_err(dev, "clock-frequency missing or invalid\n");
		return PTR_ERR(sensor->sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&(sensor->mclk));
	if (retval) {
		dev_err(dev, "mclk missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(sensor->mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(sensor->csi));
	if (retval) {
		dev_err(dev, "csi id missing or invalid\n");
		return retval;
	}

	clk_prepare_enable(sensor->sensor_clk);

	sensor->io_init = veye327_reset;
	sensor->i2c_client = client;
	sensor->pix.pixelformat = V4L2_PIX_FMT_YUYV; 
	sensor->pix.width = 1920;
	sensor->pix.height = 1080;
	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = 0;
	sensor->streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->streamcap.timeperframe.numerator = 1;

	veye327_regulator_enable(&client->dev);

	veye327_reset(sensor);

	veye327_power_down(sensor,0);

	retval = veye327_read_reg(sensor,VEYE327_MODEL_ID_ADDR, &chip_id);
	if (retval < 0 || chip_id != 0x06) {
		pr_warning("camera veye327_mipi is not found\n");
		clk_disable_unprepare(sensor->sensor_clk);
		return -ENODEV;
	}
    //set camera yuv seq to yuyv  format
    veye327_write_reg(sensor,VEYE327_REG_YUV_SEQ, 0x1);

	retval = init_device(sensor);
	if (retval < 0) {
		clk_disable_unprepare(sensor->sensor_clk);
		pr_warning("camera veye327 init failed\n");
		veye327_power_down(sensor,1);
		return retval;
	}

	v4l2_i2c_subdev_init(&sensor->subdev, client, &veye327_subdev_ops);

	sensor->subdev.grp_id = 9527;
	retval = v4l2_async_register_subdev(&sensor->subdev);
	if (retval < 0)
		dev_err(&client->dev,
					"%s--Async register failed, ret=%d\n", __func__, retval);

	veye327_stream_off(sensor);
	pr_info("camera veye327_mipi is found\n");
	return retval;
}

/*!
 * veye327 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int veye327_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct veye327 *sensor = to_veye327(client);

	v4l2_async_unregister_subdev(sd);

	clk_disable_unprepare(sensor->sensor_clk);

	veye327_power_down(sensor,1);

	if (gpo_regulator)
		regulator_disable(gpo_regulator);

	if (analog_regulator)
		regulator_disable(analog_regulator);

	if (core_regulator)
		regulator_disable(core_regulator);

	if (io_regulator)
		regulator_disable(io_regulator);

	return 0;
}

module_i2c_driver(veye327_i2c_driver);

MODULE_AUTHOR("xumm@csoneplus.com from www.veye.cc");
MODULE_DESCRIPTION("VEYE327 MIPI Camera Driver");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
