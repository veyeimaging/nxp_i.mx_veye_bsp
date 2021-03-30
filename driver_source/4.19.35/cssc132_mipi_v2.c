
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

#include "cs_sc132.h"

#define CSSC132_VOLTAGE_ANALOG               3300000
#define CSSC132_VOLTAGE_DIGITAL_CORE         1500000//do not use
#define CSSC132_VOLTAGE_DIGITAL_IO           2000000

#define MIN_FPS 1
#define MAX_FPS 45
#define DEFAULT_FPS 45
//we do not use this
#define CSSC132_XCLK_MIN 6000000
#define CSSC132_XCLK_MAX 24000000

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;
static struct regulator *gpo_regulator;
static DEFINE_MUTEX(cssc132_mutex);

static int cssc132_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int cssc132_remove(struct i2c_client *client);

static s32 cssc132_read_reg(struct cssc132 *sensor,u16 reg, u8 *val);
static s32 cssc132_write_reg(struct cssc132 *sensor,u16 reg, u8 val);

static const struct i2c_device_id cssc132_id[] = {
	{"cssc132_mipi", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, cssc132_id);

#ifdef CONFIG_OF
static const struct of_device_id cssc132_mipi_v2_of_match[] = {
	{ .compatible = "veye,cssc132_mipi",},
	{ /* sentinel */ }
};

static struct i2c_driver cssc132_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "cssc132_mipi",
    #ifdef CONFIG_OF
		  .of_match_table = of_match_ptr(cssc132_mipi_v2_of_match),
    #endif
		  },
	.probe  = cssc132_probe,
	.remove = cssc132_remove,
	.id_table = cssc132_id,
};

/*
static struct cssc132 cssc132_data;
static int pwn_gpio, rst_gpio;
*/
static struct cssc132 *to_cssc132(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct cssc132, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct veye_datafmt
			*cssc132_find_datafmt(u32 code)
{
	int i;
   // dev_dbg(dev,"%s:find code %d\n", __func__,code);
	for (i = 0; i < ARRAY_SIZE(veye_colour_fmts); i++)
		if (veye_colour_fmts[i].code == code)
        {
         //   dev_dbg(dev,"%s:find code found %d\n", __func__,i);
			return veye_colour_fmts + i;
        }

	return NULL;
}
static int get_capturemode(int width, int height)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cssc132_mode_info_data); i++) {
		if ((cssc132_mode_info_data[i].width == width) &&
		     (cssc132_mode_info_data[i].height == height))
			return i;
	}
	return -1;
}

static inline void cssc132_power_down(struct cssc132 *sensor,int enable)
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

static void cssc132_reset(struct cssc132 *sensor)
{
	if (sensor->rst_gpio < 0)
		return;

	/* camera reset */
	gpio_set_value_cansleep(sensor->rst_gpio, 0);
    msleep(5);
    gpio_set_value_cansleep(sensor->rst_gpio, 1);
    msleep(500);

}

static int cssc132_regulator_enable(struct device *dev)
{
	int ret = 0;

	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      CSSC132_VOLTAGE_DIGITAL_IO,
				      CSSC132_VOLTAGE_DIGITAL_IO);
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
				      CSSC132_VOLTAGE_DIGITAL_CORE,
				      CSSC132_VOLTAGE_DIGITAL_CORE);
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
				      CSSC132_VOLTAGE_ANALOG,
				      CSSC132_VOLTAGE_ANALOG);
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



MODULE_DEVICE_TABLE(of, cssc132_mipi_v2_of_match);
#endif


static s32 cssc132_write_reg(struct cssc132 *sensor,u16 reg, u8 val)
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

static s32 cssc132_read_reg(struct cssc132 *sensor,u16 reg, u8 *val)
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


static void cssc132_stream_on(struct cssc132 *sensor)
{
	cssc132_write_reg(sensor,Csi2_Enable, 0x01);
    msleep(CSSC132_WAIT_MS_STREAM);
}

static void cssc132_stream_off(struct cssc132 *sensor)
{
	cssc132_write_reg(sensor,Csi2_Enable, 0x00);
    msleep(CSSC132_WAIT_MS_STREAM);
}
/* download cssc132 settings to sensor through i2c */
static int cssc132_download_firmware(struct cssc132 *sensor,struct reg_value *pModeSetting, s32 ArySize)
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
			retval = cssc132_read_reg(sensor,RegAddr, &RegVal);
			if (retval < 0)
				goto err;

			RegVal &= ~(u8)Mask;
			Val &= Mask;
			Val |= RegVal;
		}

		retval = cssc132_write_reg(sensor,RegAddr, Val);
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
static int cssc132_change_mode_direct(struct cssc132 *sensor,u32 frame_rate,
				enum cssc132_mode mode)
{
	//struct reg_value *pModeSetting = NULL;
    struct reg_value reg_list[6];
	s32 ArySize = 0;
	int retval = 0;
    struct device *dev = &sensor->i2c_client->dev;
    dev_info(dev,"cssc132_change_mode_direct %d\n",mode);
    if (mode > CSSC132_mode_MAX || mode < CSSC132_mode_MIN) {
        //new_mode = CSSC132_MODE_1920X1080_30FPS;
        dev_info(dev,"V4L2_BUF_TYPE_VIDEO_CAPTURE set cssc132 mode %d not supported\n",mode);
        retval = -EINVAL;
        goto err;
    } 
    if (frame_rate > cssc132_mode_info_data[mode].max_framerate || frame_rate < MIN_FPS) {
        //new_mode = CSSC132_MODE_1920X1080_30FPS;
        dev_info(dev,"V4L2_BUF_TYPE_VIDEO_CAPTURE set cssc132 framerate %d not supported\n",frame_rate);
        retval = -EINVAL;
        goto err;
    } 
	/* check if the input mode and frame rate is valid */
	//pModeSetting =
	//	cssc132_mode_info_data[mode].init_data_ptr;
    memcpy(&reg_list[0], cssc132_mode_info_data[mode].init_data_ptr,sizeof(reg_list));
	ArySize = cssc132_mode_info_data[mode].init_data_size;
    //change the frame rate 
    reg_list[4].u8Val = frame_rate&0xFF;
    reg_list[5].u8Val = (frame_rate&0xFF00) >> 8;
    dev_info(dev,"set cssc132 framerate %d \n",frame_rate);
	sensor->pix.width =
		cssc132_mode_info_data[mode].width;
	sensor->pix.height =
		cssc132_mode_info_data[mode].height;
    sensor->framerate = frame_rate;
	if (sensor->pix.width == 0 || sensor->pix.height == 0 || ArySize == 0)
    {
        dev_err(dev,"cssc132_change_mode_direct failed EINVAL! \n");
		return -EINVAL;
    }
   /* dev_info(dev,"set cssc132 %x %x \n",reg_list[0].u16RegAddr,reg_list[0].u8Val);
    dev_info(dev,"set cssc132 %x %x \n",reg_list[1].u16RegAddr,reg_list[1].u8Val);
    dev_info(dev,"set cssc132 %x %x \n",reg_list[2].u16RegAddr,reg_list[2].u8Val);
    dev_info(dev,"set cssc132 %x %x \n",reg_list[3].u16RegAddr,reg_list[3].u8Val);
    dev_info(dev,"set cssc132 %x %x \n",reg_list[4].u16RegAddr,reg_list[4].u8Val);
    dev_info(dev,"set cssc132 %x %x \n",reg_list[5].u16RegAddr,reg_list[5].u8Val);*/
	/* Write capture setting */
	retval = cssc132_download_firmware(sensor,reg_list, ArySize);
	if (retval < 0)
		goto err;

err:
	return retval;
}

static int cssc132_init_mode(struct cssc132 *sensor,u32 frame_rate,
			    enum cssc132_mode mode)
{
	struct device *dev = &sensor->i2c_client->dev;
	int retval = 0;
	u32 msec_wait4stable = 0;

	if ((mode > CSSC132_mode_MAX || mode < CSSC132_mode_MIN)
		&& (mode != CSSC132_mode_INIT)) {
		dev_err(dev,"Wrong cssc132 mode detected!\n");
		return -1;
	}
	if (mode == CSSC132_mode_INIT) {
        mode = SC132_MODE_1280X1080_45FPS;
	}
    dev_info(dev,"cssc132_init_mode framerate %d mode %d\n",frame_rate, mode);
    retval = cssc132_change_mode_direct(sensor,frame_rate, mode);

	if (retval < 0)
		goto err;
   
	msec_wait4stable = 30;
	msleep(msec_wait4stable);

err:
	return retval;
}

/*!
 * cssc132_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int cssc132_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);

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
 * cssc132_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int cssc132_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
	struct device *dev = &sensor->i2c_client->dev;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        dev_info(dev,"cssc132_g_parm V4L2_BUF_TYPE_VIDEO_CAPTURE mode %d\n", sensor->streamcap.capturemode);
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
 * cssc132_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int cssc132_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
	struct device *dev = &sensor->i2c_client->dev;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	//u32 frame_rate;
	enum cssc132_mode new_mode;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
    //set framerate and mode here
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        new_mode = (u32)a->parm.capture.capturemode;
        dev_info(dev,"cssc132_g_parm V4L2_BUF_TYPE_VIDEO_CAPTURE mode %d\n", new_mode);
        // make sure mode is allowed
        if (new_mode > CSSC132_mode_MAX || new_mode < CSSC132_mode_MIN) {
			//new_mode = CSSC132_MODE_1920X1080_30FPS;
             dev_info(dev,"V4L2_BUF_TYPE_VIDEO_CAPTURE set cssc132 mode not supported\n");
            ret = -EINVAL;
            break;
		} 
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;
		if (tgt_fps > cssc132_mode_info_data[new_mode].max_framerate) {
			timeperframe->denominator = cssc132_mode_info_data[new_mode].max_framerate;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}
		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;
    
		//orig_mode = sensor->streamcap.capturemode;
		ret = cssc132_init_mode(sensor,tgt_fps,new_mode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				new_mode;
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

static int cssc132_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct veye_datafmt *fmt = cssc132_find_datafmt(mf->code);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
    struct device *dev = &sensor->i2c_client->dev;
    int capturemode;
    
	if (!fmt) {
		mf->code	= veye_colour_fmts[0].code;
		mf->colorspace	= veye_colour_fmts[0].colorspace;
        fmt		= &veye_colour_fmts[0];
	}
	mf->field	= V4L2_FIELD_NONE;
    
    if(mf->code == MEDIA_BUS_FMT_YUYV8_2X8){
        cssc132_write_reg(sensor,YUV_SEQ, 0x1);//yuyv
        sensor->pix.pixelformat = V4L2_PIX_FMT_YUYV; 
        dev_info(dev,"set pixel format YUYV\n");
    }else{
        cssc132_write_reg(sensor,YUV_SEQ, 0x0);//uyvy
        sensor->pix.pixelformat = V4L2_PIX_FMT_UYVY; 
        dev_info(dev,"set pixel format UYVY\n");
    }
	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	sensor->fmt = fmt;
    
	capturemode = get_capturemode(mf->width, mf->height);
	if (capturemode >= 0) {
		sensor->streamcap.capturemode = capturemode;
		sensor->pix.width = mf->width;
		sensor->pix.height = mf->height;
		return 0;
	}
    
	return 0;
}

static int cssc132_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
	const struct veye_datafmt *fmt = sensor->fmt;

	if (format->pad)
		return -EINVAL;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	mf->width	= sensor->pix.width;
	mf->height	= sensor->pix.height;
	return 0;
}

static int cssc132_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
	struct device *dev = &sensor->i2c_client->dev;
    
	if (code->pad || code->index >= ARRAY_SIZE(veye_colour_fmts))
    {
        dev_dbg(dev," index is %d and not supported!\n",code->index);
		return -EINVAL;
    }
	code->code = veye_colour_fmts[code->index].code;
    dev_dbg(dev," index is %d format is %d!\n",code->index,code->code);
	return 0;
}

/*!
 * cssc132_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int cssc132_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > CSSC132_mode_MAX)
		return -EINVAL;

	fse->max_width =cssc132_mode_info_data[fse->index].width;
	fse->min_width = fse->max_width;
    
	fse->max_height = cssc132_mode_info_data[fse->index].height;
	fse->min_height = fse->max_height;
	return 0;
}

/*!
 * cssc132_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int cssc132_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	int i=0;

	if (fie->index < CSSC132_mode_MIN || fie->index > CSSC132_mode_MAX)
		return -EINVAL;

	if (fie->width == 0 || fie->height == 0 ||
	    fie->code == 0) {
		dev_warn(dev, "Please assign pixel format, width and height\n");
		return -EINVAL;
	}
	fie->interval.numerator = 1;
    for (i = 0; i < (CSSC132_mode_MAX + 1); i++) {
        if (fie->width == cssc132_mode_info_data[i].width
         && fie->height == cssc132_mode_info_data[i].height
         && cssc132_mode_info_data[i].init_data_ptr != NULL) {
             fie->interval.denominator = cssc132_mode_info_data[i].max_framerate;
        }
    }
    
	return -EINVAL;
}

/*!
 * dev_init - V4L2 sensor init
 * @s: pointer to standard V4L2 device structure
 * 1080p@30fps mode
 */
static int init_device(struct cssc132 *sensor)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	u32  frame_rate;
	int ret;
    struct device *dev = &sensor->i2c_client->dev;
	sensor->on = true;

	/* mclk */
	tgt_xclk = sensor->mclk;
	tgt_xclk = min(tgt_xclk, (u32)CSSC132_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)CSSC132_XCLK_MIN);
	sensor->mclk = tgt_xclk;

	dev_dbg(dev,"   Setting mclk to %d MHz\n", tgt_xclk / 1000000);

	/* Default camera frame rate is set in probe */
	tgt_fps = sensor->streamcap.timeperframe.denominator /
		  sensor->streamcap.timeperframe.numerator;
    frame_rate = tgt_fps;//30fps
	ret = cssc132_init_mode(sensor,frame_rate, CSSC132_mode_INIT);
    
	return ret;
}

static int cssc132_s_stream(struct v4l2_subdev *sd, int enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cssc132 *sensor = to_cssc132(client);
	struct device *dev = &sensor->i2c_client->dev;
	dev_info(dev, "cssc132_s_stream: %d\n", enable);
	if (enable)
		cssc132_stream_on(sensor);
	else
		cssc132_stream_off(sensor);
	return 0;
}

static struct v4l2_subdev_video_ops cssc132_subdev_video_ops = {
	.g_parm = cssc132_g_parm,
	.s_parm = cssc132_s_parm,
	.s_stream = cssc132_s_stream,
};

static const struct v4l2_subdev_pad_ops cssc132_subdev_pad_ops = {
	.enum_frame_size       = cssc132_enum_framesizes,
	.enum_frame_interval   = cssc132_enum_frameintervals,
	.enum_mbus_code        = cssc132_enum_mbus_code,
	.set_fmt               = cssc132_set_fmt,
	.get_fmt               = cssc132_get_fmt,
};

static struct v4l2_subdev_core_ops cssc132_subdev_core_ops = {
	.s_power	= cssc132_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= cssc132_get_register,
	.s_register	= cssc132_set_register,
#endif
};

static struct v4l2_subdev_ops cssc132_subdev_ops = {
	.core	= &cssc132_subdev_core_ops,
	.video	= &cssc132_subdev_video_ops,
	.pad	= &cssc132_subdev_pad_ops,
};

static int cssc132_check_id(struct cssc132 *sensor)
{
    int  err = -ENODEV;
    u8 reg_val[2];
    u16 cameraid = 0;
    struct device *dev = &sensor->i2c_client->dev;
    /* Probe sensor model id registers */
	err = cssc132_read_reg(sensor, PRODUCTID_L, &reg_val[0]);
	if (err < 0) {
		dev_err(dev, "%s: error during i2c read probe (%d)\n",
			__func__, err);
		goto err_reg_probe;
	}
     err = cssc132_read_reg(sensor, PRODUCTID_H, &reg_val[1]);
    if (err < 0) {
		dev_err(dev, "%s: error during i2c read probe (%d)\n",
			__func__, err);
		goto err_reg_probe;
	}
    cameraid = ((u16)reg_val[1]<<8) + reg_val[0];
	dev_err(dev,"read sensor id %04x \n", cameraid);
	if (cameraid == CS_MIPI_SC132) 
    {
        err = 0;
        dev_err(dev, " camera id is cs-mipi-imx307\n");
    }
    else
    {
        err = -ENODEV;
		dev_err(dev, "%s: invalid sensor model id: %d\n",
			__func__, cameraid);
    }
err_reg_probe:
    return err;
}

/*!
 * cssc132 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int cssc132_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	struct cssc132 *sensor;
	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	/* cssc132 pinctrl */
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
						"cssc132_mipi_pwdn");
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
		GPIOF_OUT_INIT_HIGH,"cssc132_mipi_reset");
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

	sensor->io_init = cssc132_reset;
	sensor->i2c_client = client;
	sensor->pix.pixelformat = V4L2_PIX_FMT_YUYV; 
	sensor->pix.width = 1280;
	sensor->pix.height = 1080;
    
	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = 0;
	sensor->streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->streamcap.timeperframe.numerator = 1;
    sensor->framerate = DEFAULT_FPS;
    
	cssc132_regulator_enable(&client->dev);

	cssc132_reset(sensor);

	cssc132_power_down(sensor,0);

	retval = cssc132_check_id(sensor);
    if (retval) {
		dev_err(dev, "cssc132 sensor id check failed\n");
		return retval;
	}
    //set camera yuv seq to yuyv  format
    cssc132_write_reg(sensor,YUV_SEQ, 0x1);
   
	retval = init_device(sensor);
	if (retval < 0) {
		clk_disable_unprepare(sensor->sensor_clk);
		pr_warning("camera cssc132 init failed\n");
		cssc132_power_down(sensor,1);
		return retval;
	}

	v4l2_i2c_subdev_init(&sensor->subdev, client, &cssc132_subdev_ops);

	sensor->subdev.grp_id = 9527;
	retval = v4l2_async_register_subdev(&sensor->subdev);
	if (retval < 0)
		dev_err(&client->dev,
					"%s--Async register failed, ret=%d\n", __func__, retval);

	cssc132_stream_off(sensor);
	pr_info("camera cssc132_mipi is found\n");
	return retval;
}

/*!
 * cssc132 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int cssc132_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cssc132 *sensor = to_cssc132(client);

	v4l2_async_unregister_subdev(sd);

	clk_disable_unprepare(sensor->sensor_clk);

	cssc132_power_down(sensor,1);

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

module_i2c_driver(cssc132_i2c_driver);

MODULE_AUTHOR("xumm@csoneplus.com from www.veye.cc");
MODULE_DESCRIPTION("CSSC132 MIPI Camera Driver");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
