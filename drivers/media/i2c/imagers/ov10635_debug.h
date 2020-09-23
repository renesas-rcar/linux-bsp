
#if 0
{0x4700, 0x02}, // BT656
{0x381d, 0x40}, // mirror off
{0x381c, 0x00}, // flip off
{0x4300, 0x3a}, // YUV: UYVY
{0x4708, 0x00}, // PCLK rising edge

// clk = 24Mhz/3*22/2= 88Mhz
{0x3003, 0x16},
{0x3004, 0x30},
#endif

#define WIDTH 1280
#define HEIGHT 720

// DVP frame size
{0x3808, WIDTH >> 8},
{0x3809, WIDTH & 0xff},
{0x380a, HEIGHT >> 8},
{0x380b, HEIGHT & 0xff},

{0x3802, ((814 - HEIGHT)/2) >> 8}, // vert crop start
{0x3803, ((814 - HEIGHT)/2) & 0xff},
{0x3806, ((814 - HEIGHT)/2 + HEIGHT + 1) >> 8}, // vert crop end
{0x3807, ((814 - HEIGHT)/2 + HEIGHT + 1) & 0xff},

#if 0
#define HTS 0x6f6 // got from above table 1782
#define VTS (0x2ec+80) // got from above table 748 + 80

{0x380c, HTS >> 8}, // hts
{0x380d, HTS & 0xff},
{0x380e, VTS >> 8}, // vts
{0x380f, VTS & 0xff},

// fifo
{0x4606, (2*HTS) >> 8}, // fifo_line_length = 2*hts
{0x4607, (2*HTS) & 0xff},
{0x460a, (2*(HTS-1280)) >> 8}, // fifo_hsync_start = 2*(hts - xres)
{0x460b, (2*(HTS-1280)) & 0xff },

// exposure
{0xC488, (VTS-8)*16 >> 8},
{0xC489, (VTS-8)*16 & 0xff},
{0xC48A, (VTS-8)*16 >> 8},
{0xC48B, (VTS-8)*16 & 0xff},

// vts/hts
{0xC518, VTS >> 8},
{0xC519, VTS & 0xff},
{0xC51A, HTS >> 8},
{0xC51B, HTS & 0xff},
#endif
