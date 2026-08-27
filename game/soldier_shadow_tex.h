/* The cartridge's soldier shadow: 8x8 I8, RAM 0x80212210 = ROM 0x1B4000.
   Drawn MIRRORED in S and T (G_TX_MIRROR, mask 3), so it is one quadrant
   of a 16x16 radially symmetric blob. Recovered by
   /tmp/wave6/shadows/soldier_shadow.py -- do not hand-edit. */
static const unsigned char SOLDIER_SHADOW_I8[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   4,   0,
      0,   0,   0,   6,  17,  91, 131, 156,
      0,   0,   4,  70, 161, 217, 237, 240,
      0,   0,  13, 165, 240, 253, 254, 254,
      0,   0,  93, 221, 253, 255, 255, 255,
      0,   1, 132, 238, 254, 255, 255, 255,
      0,   0, 161, 241, 254, 255, 254, 252,
};
