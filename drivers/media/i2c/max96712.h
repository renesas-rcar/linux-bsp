#define MAX96712_REG4			0x04
#define MAX96712_REG5			0x05
#define MAX96712_REG6			0x06
#define MAX96712_REG14			0x0e
#define MAX96712_REG26			0x10
#define MAX96712_REG27			0x11

#define MAX96712_CTRL0			0x17
#define MAX96712_CTRL1			0x18
#define MAX96712_CTRL2			0x19
#define MAX96712_CTRL3			0x1a
#define MAX96712_CTRL11			0x22
#define MAX96712_CTRL12			0x0a
#define MAX96712_CTRL13			0x0b
#define MAX96712_CTRL14			0x0c

#define MAX96712_PWR1			0x13

#define MAX96712_DEV_ID			0x4a
#define MAX96712_REV			0x4c

#define MAX96712_VIDEO_PIPE_SEL(n)	(0xf0 + n)
#define MAX96712_VIDEO_PIPE_EN		0xf4

#define MAX96712_I2C_0(n)		(0x640 + (0x10 * (n)))
#define MAX96712_I2C_1(n)		(0x641 + (0x10 * (n)))

#define MAX96712_RX0(n)			(0x50 + n)

#define MAX_VIDEO_RX_BASE(n)		(n < 5 ? (0x100 + (0x12 * n)) : \
						 (0x160 + (0x12 * (n - 5))))
#define MAX_VIDEO_RX0(n)		(MAX_VIDEO_RX_BASE(n) + 0x00)
#define MAX_VIDEO_RX3(n)		(MAX_VIDEO_RX_BASE(n) + 0x03)
#define MAX_VIDEO_RX8(n)		(MAX_VIDEO_RX_BASE(n) + 0x08)
#define MAX_VIDEO_RX10(n)		(MAX_VIDEO_RX_BASE(n) + 0x0a)

#define MAX_VPRBS(n)			(0x1dc + (0x20 * n))

#define MAX_CROSS_BASE(n)		(0x1c0 + (0x20 * n))
#define MAX_CROSS(n, m)			(MAX_CROSS_BASE(n) + m)

#define MAX_BACKTOP_BASE(bank)		(0x400 + (0x20 * bank))
#define MAX_BACKTOP1(bank)		(MAX_BACKTOP_BASE(bank) + 0x00)
#define MAX_BACKTOP11(bank)		(MAX_BACKTOP_BASE(bank) + 0x0a)
#define MAX_BACKTOP12(bank)		(MAX_BACKTOP_BASE(bank) + 0x0b)
#define MAX_BACKTOP13(bank)		(MAX_BACKTOP_BASE(bank) + 0x0c)
#define MAX_BACKTOP14(bank)		(MAX_BACKTOP_BASE(bank) + 0x0d)
#define MAX_BACKTOP15(bank)		(MAX_BACKTOP_BASE(bank) + 0x0e)
#define MAX_BACKTOP16(bank)		(MAX_BACKTOP_BASE(bank) + 0x0f)
#define MAX_BACKTOP17(bank)		(MAX_BACKTOP_BASE(bank) + 0x10)
#define MAX_BACKTOP18(bank)		(MAX_BACKTOP_BASE(bank) + 0x11)
#define MAX_BACKTOP19(bank)		(MAX_BACKTOP_BASE(bank) + 0x12)
#define MAX_BACKTOP20(bank)		(MAX_BACKTOP_BASE(bank) + 0x13)
#define MAX_BACKTOP21(bank)		(MAX_BACKTOP_BASE(bank) + 0x14)
#define MAX_BACKTOP22(bank)		(MAX_BACKTOP_BASE(bank) + 0x15)
#define MAX_BACKTOP23(bank)		(MAX_BACKTOP_BASE(bank) + 0x16)
#define MAX_BACKTOP24(bank)		(MAX_BACKTOP_BASE(bank) + 0x17)
#define MAX_BACKTOP25(bank)		(MAX_BACKTOP_BASE(bank) + 0x18)
#define MAX_BACKTOP26(bank)		(MAX_BACKTOP_BASE(bank) + 0x19)
#define MAX_BACKTOP27(bank)		(MAX_BACKTOP_BASE(bank) + 0x1a)
#define MAX_BACKTOP28(bank)		(MAX_BACKTOP_BASE(bank) + 0x1b)
#define MAX_BACKTOP29(bank)		(MAX_BACKTOP_BASE(bank) + 0x1c)
#define MAX_BACKTOP30(bank)		(MAX_BACKTOP_BASE(bank) + 0x1d)
#define MAX_BACKTOP31(bank)		(MAX_BACKTOP_BASE(bank) + 0x1e)
#define MAX_BACKTOP32(bank)		(MAX_BACKTOP_BASE(bank) + 0x1f)

#define MAX96712_FSYNC_0		0x4a0
#define MAX96712_FSYNC_5		0x4a5
#define MAX96712_FSYNC_6		0x4a6
#define MAX96712_FSYNC_7		0x4a7
#define MAX96712_FSYNC_8		0x4a8
#define MAX96712_FSYNC_9		0x4a9
#define MAX96712_FSYNC_10		0x4aa
#define MAX96712_FSYNC_11		0x4ab
#define MAX96712_FSYNC_15		0x4af
#define MAX96712_FSYNC_17		0x4b1

#define MAX_MIPI_PHY_BASE		0x8a0
#define MAX_MIPI_PHY0			(MAX_MIPI_PHY_BASE + 0x00)
#define MAX_MIPI_PHY2			(MAX_MIPI_PHY_BASE + 0x02)
#define MAX_MIPI_PHY3			(MAX_MIPI_PHY_BASE + 0x03)
#define MAX_MIPI_PHY4			(MAX_MIPI_PHY_BASE + 0x04)
#define MAX_MIPI_PHY5			(MAX_MIPI_PHY_BASE + 0x05)
#define MAX_MIPI_PHY6			(MAX_MIPI_PHY_BASE + 0x06)
#define MAX_MIPI_PHY8			(MAX_MIPI_PHY_BASE + 0x08)
#define MAX_MIPI_PHY9			(MAX_MIPI_PHY_BASE + 0x09)
#define MAX_MIPI_PHY10			(MAX_MIPI_PHY_BASE + 0x0a)
#define MAX_MIPI_PHY11			(MAX_MIPI_PHY_BASE + 0x0b)
#define MAX_MIPI_PHY13			(MAX_MIPI_PHY_BASE + 0x0d)
#define MAX_MIPI_PHY14			(MAX_MIPI_PHY_BASE + 0x0e)

#define MAX_MIPI_TX_BASE(n)		(0x900 + 0x40 * n)
#define MAX_MIPI_TX2(n)			(MAX_MIPI_TX_BASE(n) + 0x02)
#define MAX_MIPI_TX10(n)		(MAX_MIPI_TX_BASE(n) + 0x0a)
#define MAX_MIPI_TX11(n)		(MAX_MIPI_TX_BASE(n) + 0x0b)
#define MAX_MIPI_TX12(n)		(MAX_MIPI_TX_BASE(n) + 0x0c)

/* 16 pairs of source-dest registers */
#define MAX_MIPI_MAP_SRC(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x0d + (2 * n))
#define MAX_MIPI_MAP_DST(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x0e + (2 * n))
/* Phy dst. Each reg contains 4 dest */
#define MAX_MIPI_MAP_DST_PHY(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x2d + n)

#define MAX_GMSL1_2(ch)			(0xb02 + (0x100 * ch))
#define MAX_GMSL1_4(ch)			(0xb04 + (0x100 * ch))
#define MAX_GMSL1_6(ch)			(0xb06 + (0x100 * ch))
#define MAX_GMSL1_7(ch)			(0xb07 + (0x100 * ch))
#define MAX_GMSL1_8(ch)			(0xb08 + (0x100 * ch))
#define MAX_GMSL1_D(ch)			(0xb0d + (0x100 * ch))
#define MAX_GMSL1_F(ch)			(0xb0f + (0x100 * ch))
#define MAX_GMSL1_19(ch)		(0xb19 + (0x100 * ch))
#define MAX_GMSL1_1B(ch)		(0xb1b + (0x100 * ch))
#define MAX_GMSL1_1D(ch)		(0xb1d + (0x100 * ch))
#define MAX_GMSL1_20(ch)		(0xb20 + (0x100 * ch))
#define MAX_GMSL1_96(ch)		(0xb96 + (0x100 * ch))
#define MAX_GMSL1_CA(ch)		(0xbca + (0x100 * ch))
#define MAX_GMSL1_CB(ch)		(0xbcb + (0x100 * ch))

#define MAX_RLMS4(ch)			(0x1404 + (0x100 * ch))
#define MAX_RLMSA(ch)			(0x140A + (0x100 * ch))
#define MAX_RLMSB(ch)			(0x140B + (0x100 * ch))
#define MAX_RLMSA4(ch)			(0x14a4 + (0x100 * ch))

#define MAX_RLMS58(ch)			(0x1458 + (0x100 * ch))
#define MAX_RLMS59(ch)			(0x1459 + (0x100 * ch))
#define MAX_RLMS95(ch)			(0x1495 + (0x100 * ch))
#define MAX_RLMSC4(ch)			(0x14c4 + (0x100 * ch))
#define MAX_RLMSC5(ch)			(0x14c5 + (0x100 * ch))

#define MAX9271_ID			0x09
#define MAX9286_ID			0x40
#define MAX9288_ID			0x2A
#define MAX9290_ID			0x2C
#define MAX9295A_ID			0x91
#define MAX9295B_ID			0x93
#define MAX9296A_ID			0x94
#define MAX96705_ID			0x41
#define MAX96706_ID			0x4A
#define MAX96707_ID			0x45 /* MAX96715: same but lack of HS pin */
#define MAX96708_ID			0x4C
#define MAX96712_ID			0x20

#define UB960_ID			0x00 /* strapped */

#define BROADCAST			0x6f

#define REG8_NUM_RETRIES		1 /* number of read/write retries */
#define REG16_NUM_RETRIES		10 /* number of read/write retries */

enum gmsl_mode {
	MODE_GMSL1 = 1,
	MODE_GMSL2,
};

#define MAXIM_I2C_I2C_SPEED_837KHZ	(0x7 << 2) /* 837kbps */
#define MAXIM_I2C_I2C_SPEED_533KHZ	(0x6 << 2) /* 533kbps */
#define MAXIM_I2C_I2C_SPEED_339KHZ	(0x5 << 2) /* 339 kbps */
#define MAXIM_I2C_I2C_SPEED_173KHZ	(0x4 << 2) /* 174kbps */
#define MAXIM_I2C_I2C_SPEED_105KHZ	(0x3 << 2) /* 105 kbps */
#define MAXIM_I2C_I2C_SPEED_085KHZ	(0x2 << 2) /* 84.7 kbps */
#define MAXIM_I2C_I2C_SPEED_028KHZ	(0x1 << 2) /* 28.3 kbps */
#define MAXIM_I2C_I2C_SPEED		MAXIM_I2C_I2C_SPEED_339KHZ

#define MIPI_DT_GENERIC			0x10
#define MIPI_DT_GENERIC_1		0x11
#define MIPI_DT_EMB			0x12
#define MIPI_DT_YUV8			0x1e
#define MIPI_DT_YUV10			0x1f
#define MIPI_DT_RGB565			0x22
#define MIPI_DT_RGB666			0x23
#define MIPI_DT_RGB888			0x24
#define MIPI_DT_RAW8			0x2a
#define MIPI_DT_RAW10			0x2b
#define MIPI_DT_RAW12			0x2c
#define MIPI_DT_RAW14			0x2d
#define MIPI_DT_RAW16			0x2e
#define MIPI_DT_RAW20			0x2f
#define MIPI_DT_YUV12			0x30

#define MAX9295A_DEFAULT_ADDR	0x40

#define MAXIM_I2C_I2C_SPEED_400KHZ	MAX9295A_I2CMSTBT_339KBPS
#define MAXIM_I2C_I2C_SPEED_100KHZ	MAX9295A_I2CMSTBT_105KBPS
#define MAXIM_I2C_SPEED			MAXIM_I2C_I2C_SPEED_100KHZ

/* Register 0x04 */
#define MAX9295A_SEREN			BIT(7)
#define MAX9295A_CLINKEN		BIT(6)
#define MAX9295A_PRBSEN			BIT(5)
#define MAX9295A_SLEEP			BIT(4)
#define MAX9295A_INTTYPE_I2C		(0 << 2)
#define MAX9295A_INTTYPE_UART		(1 << 2)
#define MAX9295A_INTTYPE_NONE		(2 << 2)
#define MAX9295A_REVCCEN		BIT(1)
#define MAX9295A_FWDCCEN		BIT(0)
/* Register 0x07 */
#define MAX9295A_DBL			BIT(7)
#define MAX9295A_DRS			BIT(6)
#define MAX9295A_BWS			BIT(5)
#define MAX9295A_ES			BIT(4)
#define MAX9295A_HVEN			BIT(2)
#define MAX9295A_EDC_1BIT_PARITY	(0 << 0)
#define MAX9295A_EDC_6BIT_CRC		(1 << 0)
#define MAX9295A_EDC_6BIT_HAMMING	(2 << 0)
/* Register 0x08 */
#define MAX9295A_INVVS			BIT(7)
#define MAX9295A_INVHS			BIT(6)
#define MAX9295A_REV_LOGAIN		BIT(3)
#define MAX9295A_REV_HIVTH		BIT(0)
/* Register 0x09 */
#define MAX9295A_ID_REG			0x09
/* Register 0x0d */
#define MAX9295A_I2CLOCACK		BIT(7)
#define MAX9295A_I2CSLVSH_1046NS_469NS	(3 << 5)
#define MAX9295A_I2CSLVSH_938NS_352NS	(2 << 5)
#define MAX9295A_I2CSLVSH_469NS_234NS	(1 << 5)
#define MAX9295A_I2CSLVSH_352NS_117NS	(0 << 5)
#define MAX9295A_I2CMSTBT_837KBPS	(7 << 2)
#define MAX9295A_I2CMSTBT_533KBPS	(6 << 2)
#define MAX9295A_I2CMSTBT_339KBPS	(5 << 2)
#define MAX9295A_I2CMSTBT_173KBPS	(4 << 2)
#define MAX9295A_I2CMSTBT_105KBPS	(3 << 2)
#define MAX9295A_I2CMSTBT_84KBPS	(2 << 2)
#define MAX9295A_I2CMSTBT_28KBPS	(1 << 2)
#define MAX9295A_I2CMSTBT_8KBPS		(0 << 2)
#define MAX9295A_I2CSLVTO_NONE		(3 << 0)
#define MAX9295A_I2CSLVTO_1024US	(2 << 0)
#define MAX9295A_I2CSLVTO_256US		(1 << 0)
#define MAX9295A_I2CSLVTO_64US		(0 << 0)
/* Register 0x0f */
#define MAX9295A_GPIO5OUT		BIT(5)
#define MAX9295A_GPIO4OUT		BIT(4)
#define MAX9295A_GPIO3OUT		BIT(3)
#define MAX9295A_GPIO2OUT		BIT(2)
#define MAX9295A_GPIO1OUT		BIT(1)
#define MAX9295A_SETGPO			BIT(0)
/* Register 0x15 */
#define MAX9295A_PCLKDET		BIT(0)

#define MAX9295_REG2			0x02
#define MAX9295_REG7			0x07
#define MAX9295_CTRL0			0x10
#define MAX9295_I2C2			0x42
#define MAX9295_I2C3			0x43
#define MAX9295_I2C4			0x44
#define MAX9295_I2C5			0x45
#define MAX9295_I2C6			0x46

#define MAX9295_CROSS(n)		(0x1b0 + n)

#define MAX9295_GPIO_A(n)		(0x2be + (3 * n))
#define MAX9295_GPIO_B(n)		(0x2bf + (3 * n))
#define MAX9295_GPIO_C(n)		(0x2c0 + (3 * n))

#define MAX9295_VIDEO_TX_BASE(n)	(0x100 + (0x8 * n))
#define MAX9295_VIDEO_TX0(n)		(MAX9295_VIDEO_TX_BASE(n) + 0)
#define MAX9295_VIDEO_TX1(n)		(MAX9295_VIDEO_TX_BASE(n) + 1)

#define MAX9295_FRONTTOP_0		0x308
#define MAX9295_FRONTTOP_9		0x311
#define MAX9295_FRONTTOP_12		0x314
#define MAX9295_FRONTTOP_13		0x315

#define MAX9295_MIPI_RX0		0x330
#define MAX9295_MIPI_RX1		0x331
#define MAX9295_MIPI_RX2		0x332
#define MAX9295_MIPI_RX3		0x333

struct max9295a_device {
	struct i2c_client *client;
	int stream_count;
};

int max9295a_s_stream(struct max9295a_device *dev, bool enable);
int max9295a_configure_i2c(struct max9295a_device *dev);
int max9295a_configure_gmsl_link(struct max9295a_device *dev);
int max9295a_set_gpio(struct max9295a_device *dev, u8 val);
int max9295a_verify_id(struct max9295a_device *dev);
int max9295a_set_address(struct max9295a_device *dev, u8 addr);

struct max9295a_reg {
	u16	reg;
	u8	val;
};

static inline char *chip_name(int id)
{
	switch (id) {
	case MAX9271_ID:
		return "MAX9271";
	case MAX9286_ID:
		return "MAX9286";
	case MAX9288_ID:
		return "MAX9288";
	case MAX9290_ID:
		return "MAX9290";
	case MAX9295A_ID:
		return "MAX9295A";
	case MAX9295B_ID:
		return "MAX9295B";
	case MAX9296A_ID:
		return "MAX9296A";
	case MAX96705_ID:
		return "MAX96705";
	case MAX96706_ID:
		return "MAX96706";
	case MAX96707_ID:
		return "MAX96707";
	case MAX96712_ID:
		return "MAX96712";
	default:
		return "serializer";
	}
}

static const struct max9295a_reg configuretable_ar0231[] = {
	{0x0002, 0x03}, /* VideoTX Disable and write 3 to reserved bits */
	{0x0100, 0x60}, /* VIDEO_TX) - Line CRC enabled.  Encoding ON. Read back 62. */
	{0x0101, 0x0A}, /* VIDEO_TX) - BPP Setting. */

// GPIO8: FV_OUT in <== Camera-Ser output
	{0x02D6, 0x63}, /* GPIO_A - for GPIO8 ... MFP8: FV_OUT in <=== ISP(AP0200)[from Camera-Ser(GPIO8)] */
	{0x02D7, 0x2B}, /* GPIO_B - for GPIO8 ... GPIO_TX_ID=11 */
	{0x02D8, 0x0B}, /* GPIO_C(dummy) */

// GPIO7: FR_SYNC out ==> Camera-Ser input
	{0x02D3, 0x84}, /* GPIO_A - for GPIO7 ... MFP7: FR_SYNC out==> ISP(AP0200)[to Camera-Ser(GPIO8)] */
	{0x02D4, 0x2C}, /* GPIO_B(dummy) */
	{0x02D5, 0x0C}, /* GPIO_C - for GPIO7 ... GPIO_RX_ID=12 */

	{0x0007, 0xC7}, /* Configure serializer for parallel sensor input */
	{0x0332, 0xEE}, /* PHY lane mapping */
	{0x0333, 0xE4}, /* PHY lane mapping */
	{0x0314, 0x2B}, /* Select designated datatype to route to Video Pipeline X */
	{0x0316, 0x22}, /* Select designated datatype to route to Video Pipeline Y */
	{0x0318, 0x22}, /* Select designated datatype to route to Video Pipeline Z */
	{0x031A, 0x22}, /* Select designated datatype to route to Video Pipeline U */
	{0x031C, 0x2A}, /* Soft BPP Pipe X */
	{0x0002, 0x13}, /* Video transmit Pipe X enable */
	{0x03F1, 0x89}  /* Output RCLK to Sensor */
};

static inline int mipi_dt_to_bpp(unsigned int dt)
{
	switch (dt) {
	case 0x2a:
	case 0x10 ... 0x12:
	case 0x31 ... 0x37:
		return 0x08;
	case 0x2b:
		return 0x0a;
	case 0x2c:
		return 0x0c;
	case 0x0d:
		return 0x0e;
	case 0x22:
	case 0x1e:
	case 0x2e:
		return 0x10;
	case 0x23:
		return 0x12;
	case 0x1f:
	case 0x2f:
		return 0x14;
	case 0x24:
	case 0x30:
		return 0x18;
	default:
		return 0x08;
	}
}

static inline int reg8_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret, retries;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = ret;
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret, retries;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
#ifdef WRITE_VERIFY
		u8 val2;
		reg8_read(client, reg, &val2);
		if (val != val2)
			dev_err(&client->dev,
				"write verify mismatch: chip 0x%x reg=0x%x "
				"0x%x->0x%x\n", client->addr, reg, val, val2);
#endif
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, buf, 1);
			if (ret == 1)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = buf[0];
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret, retries;
	u8 buf[3] = {reg >> 8, reg & 0xff, val};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 3);
		if (ret == 3)
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
#ifdef WRITE_VERIFY
		u8 val2;
		reg16_read(client, reg, &val2);
		if (val != val2)
			dev_err(&client->dev,
				"write verify mismatch: chip 0x%x reg=0x%x "
				"0x%x->0x%x\n", client->addr, reg, val, val2);
#endif
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read16(struct i2c_client *client, u16 reg, u16 *val)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, buf, 2);
			if (ret == 2)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = ((u16)buf[0] << 8) | buf[1];
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_write16(struct i2c_client *client, u16 reg, u16 val)
{
	int ret, retries;
	u8 buf[4] = {reg >> 8, reg & 0xff, val >> 8, val & 0xff};

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 4);
		if (ret == 4)
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read_n(struct i2c_client *client,
			       u16 reg, u8 *val, int n)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, val, n);
			if (ret == n)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x registers 0x%x-0x%x: %d\n",
			client->addr, reg, reg + n, ret);
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_read_addr(struct i2c_client *client, int addr,
				 u8 reg, u8 *val)
{
	int ret, retries;
	union i2c_smbus_data data;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_xfer(client->adapter, addr, client->flags,
				     I2C_SMBUS_READ, reg,
				     I2C_SMBUS_BYTE_DATA, &data);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			addr, reg, ret);
	} else {
		*val = data.byte;
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_write_addr(struct i2c_client *client, u8 addr,
				  u8 reg, u8 val)
{
	int ret, retries;
	union i2c_smbus_data data;

	data.byte = val;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_xfer(client->adapter, addr, client->flags,
				     I2C_SMBUS_WRITE, reg,
				     I2C_SMBUS_BYTE_DATA, &data);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x value 0x%0x: %d\n",
			addr, reg, val, ret);
	}

	return ret < 0 ? ret : 0;
}


static inline int reg16_write_addr(struct i2c_client *client, int chip,
				   u16 reg, u8 val)
{
	struct i2c_msg msg[1];
	u8 wbuf[3];
	int ret;

	msg->addr = chip;
	msg->flags = 0;
	msg->len = 3;
	msg->buf = wbuf;
	wbuf[0] = reg >> 8;
	wbuf[1] = reg & 0xff;
	wbuf[2] = val;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
		dev_dbg(&client->dev,
			"i2c fail: chip 0x%02x wr 0x%04x (0x%02x): %d\n",
			chip, reg, val, ret);
		return ret;
	}

	return 0;
}

static inline int reg16_read_addr(struct i2c_client *client, int chip,
				  u16 reg, int *val)
{
	struct i2c_msg msg[2];
	u8 wbuf[2];
	u8 rbuf[1];
	int ret;

	msg[0].addr = chip;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = wbuf;
	wbuf[0] = reg >> 8;
	wbuf[1] = reg & 0xff;

	msg[1].addr = chip;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c fail: chip 0x%02x rd 0x%04x: %d\n",
			chip, reg, ret);
		return ret;
	}

	*val = rbuf[0];

	return 0;
}

#define __reg8_read(addr, reg, val)	reg8_read_addr(priv->client, addr, reg, val)
#define __reg8_write(addr, reg, val)	reg8_write_addr(priv->client, addr, reg, val)
#define __reg16_read(addr, reg, val)	reg16_read_addr(priv->client, addr, reg, val)
#define __reg16_write(addr, reg, val)	reg16_write_addr(priv->client, addr, reg, val)

struct i2c_mux_priv {
	struct i2c_adapter adap;
	struct i2c_algorithm algo;
	struct i2c_mux_core *muxc;
	u32 chan_id;
};

static inline int get_des_id(struct i2c_client *client)
{
	struct i2c_mux_priv *mux_priv = client->adapter->algo_data;

	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9286"))
		return MAX9286_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9288"))
		return MAX9288_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9296"))
		return MAX9296A_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max96706"))
		return MAX96706_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max96712"))
		return MAX96712_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "ti9x4"))
		return UB960_ID;

	return -EINVAL;
}

static inline int get_des_addr(struct i2c_client *client)
{
	struct i2c_mux_priv *mux_priv = client->adapter->algo_data;

	return to_i2c_client(mux_priv->muxc->dev)->addr;
}

static inline void setup_i2c_translator(struct i2c_client *client, int ser_addr,
					int sensor_addr)
{
	int gmsl_mode = MODE_GMSL2;

	switch (get_des_id(client)) {
	case MAX9286_ID:
	case MAX9288_ID:
	case MAX96706_ID:
		reg8_write_addr(client, ser_addr, 0x09, client->addr << 1);
		reg8_write_addr(client, ser_addr, 0x0A, sensor_addr << 1);
		break;
	case MAX9296A_ID:
	case MAX96712_ID:
		/* parse gmsl mode from deserializer */
		reg16_read_addr(client, get_des_addr(client), 6, &gmsl_mode);
		gmsl_mode = !!(gmsl_mode & BIT(7)) + 1;

		if (gmsl_mode == MODE_GMSL1) {
			reg8_write_addr(client, ser_addr, 0x09,
					client->addr << 1);
			reg8_write_addr(client, ser_addr, 0x0A,
					sensor_addr << 1);
		}
		if (gmsl_mode == MODE_GMSL2) {
			reg16_write_addr(client, ser_addr, MAX9295_I2C2,
					 client->addr << 1);
			reg16_write_addr(client, ser_addr, MAX9295_I2C3,
					 sensor_addr << 1);
		}
		break;
	case UB960_ID:
		reg8_write_addr(client, get_des_addr(client), 0x65,
				client->addr << 1);
		reg8_write_addr(client, get_des_addr(client), 0x5d,
				sensor_addr << 1);
		break;
	}
	usleep_range(2000, 2500);
}
