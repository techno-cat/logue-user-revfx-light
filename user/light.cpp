/*
Copyright 2019 Tomoaki Itoh
This software is released under the MIT License, see LICENSE.txt.
//*/

#include "userrevfx.h"
#include "buffer_ops.h"
#include "LCWCommon.h"

#define LCW_DELAY_BUFFER_DEC(p) ( ((p)->pointer - 1) & (p)->mask )
#define LCW_DELAY_BUFFER_LUT(p, i) ( (p)->buffer[((p)->pointer + (i)) & (p)->mask] )

typedef struct {
    int32_t *buffer;
    uint32_t size;
    uint32_t mask;
    int32_t pointer;
    int32_t gain;
} LCWDelayBuffer;

#define LCW_REVERB_GAIN_TABLE_SIZE (64 + 1)

#define LCW_REVERB_COMB_SIZE (1<<14)
#define LCW_REVERB_COMB_MAX (6)
#define LCW_REVERB_COMB_BUFFER_TOTAL (LCW_REVERB_COMB_SIZE * LCW_REVERB_COMB_MAX)

#define LCW_REVERB_AP_SIZE (1<<12)
#define LCW_REVERB_AP_MAX (6)
#define LCW_REVERB_AP_BUFFER_TOTAL (LCW_REVERB_AP_SIZE * LCW_REVERB_AP_MAX)

static __sdram int32_t s_reverb_ram_comb_buffer[LCW_REVERB_COMB_BUFFER_TOTAL];
static __sdram int32_t s_reverb_ram_ap_buffer[LCW_REVERB_AP_BUFFER_TOTAL];

static LCWDelayBuffer revCombBuffers[LCW_REVERB_COMB_MAX];
static LCWDelayBuffer revApBuffers[LCW_REVERB_AP_MAX];

static struct {
  float time = 0.f;
  float depth = 0.f;
  float mix = 0.5f;
} s_param;

static struct {
  SQ7_24 dst[2] = {
    LCW_SQ7_24( 0.5f ), LCW_SQ7_24( 0.5f )
  };
  SQ7_24 mix[2] = {
    LCW_SQ7_24( 0.5f ), LCW_SQ7_24( 0.5f )
  };
  float inputGain = 0.f;
} s_state;

// q12
static const int32_t gainTable[LCW_REVERB_GAIN_TABLE_SIZE][LCW_REVERB_COMB_MAX] = {
  { 0x000, 0x000, 0x000, 0x000, 0x000 }, // [ 0]
// 10181, 8501, 6791, 3407, 13997, 9337
// 2969, 7211, 8059, 9767
// 1231, 1319, 1571,  977
  { 0xFF264, 0x19364B, 0x28301B, 0x651CDE, 0x5A248, 0x1412B9 }, // [ 1] 0.528
  { 0x1275C4, 0x1C7D35, 0x2C4F0E, 0x6A301B, 0x6E3B8, 0x16F4D7 }, // [ 2] 0.557
  { 0x153443, 0x1FFBF4, 0x3099A3, 0x6F3A8B, 0x855EB, 0x1A1156 }, // [ 3] 0.588
  { 0x182DEA, 0x23B0B3, 0x350C72, 0x743997, 0x9FC15, 0x1D674F }, // [ 4] 0.621
  { 0x1B620B, 0x2798FC, 0x39A39F, 0x792A99, 0xBD8F2, 0x20F528 }, // [ 5] 0.655
  { 0x1ECE3E, 0x2BB089, 0x3E5991, 0x7E098B, 0xDEE09, 0x24B783 }, // [ 6] 0.692
  { 0x2271EA, 0x2FF58A, 0x432BD8, 0x82D60F, 0x103DC0, 0x28AD1F }, // [ 7] 0.730
  { 0x26494B, 0x3462B6, 0x48145A, 0x878C57, 0x12C83F, 0x2CD164 }, // [ 8] 0.771
  { 0x2A5183, 0x38F425, 0x4D0EDC, 0x8C2AC5, 0x158DDF, 0x3120E0 }, // [ 9] 0.814
  { 0x2E87A5, 0x3DA620, 0x521791, 0x90B044, 0x188EA0, 0x35982C }, // [10] 0.859
  { 0x32E83F, 0x42748E, 0x572A7C, 0x951BA9, 0x1BC9C9, 0x3A3371 }, // [11] 0.907
  { 0x376DFA, 0x47598C, 0x5C41ED, 0x996A76, 0x1F3CD1, 0x3EECFD }, // [12] 0.958
  { 0x3C1601, 0x4C5225, 0x615B79, 0x9D9D0A, 0x22E6AC, 0x43C1D8 }, // [13] 1.011
  { 0x40DB41, 0x51592A, 0x667285, 0xA1B1F1, 0x26C433, 0x48ACCD }, // [14] 1.067
  { 0x45B9C3, 0x566AC8, 0x6B83FF, 0xA5A8F7, 0x2AD2BA, 0x4DA9E8 }, // [15] 1.127
  { 0x4AAC0D, 0x5B81C4, 0x708B83, 0xA980E4, 0x2F0DF4, 0x52B3B9 }, // [16] 1.189
  { 0x4FAF07, 0x609B77, 0x758756, 0xAD3A7F, 0x33733B, 0x57C755 }, // [17] 1.255
  { 0x54BD60, 0x65B311, 0x7A73A9, 0xB0D4ED, 0x37FDB3, 0x5CDF9B }, // [18] 1.325
  { 0x59D350, 0x6AC56D, 0x7F4E5A, 0xB45086, 0x3CA984, 0x61F908 }, // [19] 1.399
  { 0x5EEC45, 0x6FCEAC, 0x84149F, 0xB7AD17, 0x4171DA, 0x670F59 }, // [20] 1.477
  { 0x64050D, 0x74CC5E, 0x88C510, 0xBAEB53, 0x4652EC, 0x6C1FB4 }, // [21] 1.559
  { 0x691971, 0x79BB1C, 0x8D5D5A, 0xBE0B35, 0x4B47C2, 0x712641 }, // [22] 1.646
  { 0x6E263A, 0x7E9886, 0x91DC23, 0xC10D4E, 0x504C2E, 0x76202F }, // [23] 1.737
  { 0x7327F0, 0x8361FF, 0x963FD9, 0xC3F1F4, 0x555B96, 0x7B0A6C }, // [24] 1.834
  { 0x781BBD, 0x88158C, 0x9A8777, 0xC6B9C5, 0x5A71E0, 0x7FE28E }, // [25] 1.936
  { 0x7CFF51, 0x8CB1B0, 0x9EB266, 0xC96592, 0x5F8B67, 0x84A6AA }, // [26] 2.044
  { 0x81CFAE, 0x913449, 0xA2BF75, 0xCBF5B3, 0x64A3BB, 0x89542E }, // [27] 2.158
  { 0x868AAC, 0x959BFF, 0xA6AE20, 0xCE6ADD, 0x69B73A, 0x8DE958 }, // [28] 2.278
  { 0x8B2EC7, 0x99E80D, 0xAA7E60, 0xD0C5FD, 0x6EC2ED, 0x926504 }, // [29] 2.404
  { 0x8FB9BF, 0x9E16FD, 0xAE2F87, 0xD30788, 0x73C309, 0x96C558 }, // [30] 2.538
  { 0x942AB2, 0xA22893, 0xB1C1F7, 0xD53086, 0x78B542, 0x9B09C4 }, // [31] 2.679
  { 0x987FE5, 0xA61BC9, 0xB53553, 0xD7417E, 0x7D965D, 0x9F30EA }, // [32] 2.828
  { 0x9CB8A5, 0xA9F081, 0xB88A06, 0xD93B5C, 0x82644B, 0xA33A63 }, // [33] 2.986
  { 0xA0D3F4, 0xADA655, 0xBBC02F, 0xDB1ED1, 0x871CB3, 0xA7257D }, // [34] 3.152
  { 0xA4D126, 0xB13D22, 0xBED822, 0xDCECA0, 0x8BBDA0, 0xAAF1D5 }, // [35] 3.327
  { 0xA8B00C, 0xB4B52B, 0xC1D280, 0xDEA5AD, 0x9045B7, 0xAE9F76 }, // [36] 3.513
  { 0xAC7001, 0xB80E46, 0xC4AF87, 0xE04A95, 0x94B324, 0xB22DFB }, // [37] 3.708
  { 0xB01121, 0xBB48ED, 0xC76FFA, 0xE1DC35, 0x990500, 0xB59DB2 }, // [38] 3.914
  { 0xB3934D, 0xBE655E, 0xCA145F, 0xE35B3F, 0x9D3A2C, 0xB8EEAB }, // [39] 4.132
  { 0xB6F6A5, 0xC1640A, 0xCC9D63, 0xE4C871, 0xA151E3, 0xBC2134 }, // [40] 4.362
  { 0xBA3B58, 0xC44568, 0xCF0BB0, 0xE6247F, 0xA54B80, 0xBF35A0 }, // [41] 4.605
  { 0xBD61B2, 0xC70A03, 0xD15FFD, 0xE7701B, 0xA92691, 0xC22C5F }, // [42] 4.861
  { 0xC06A28, 0xC9B284, 0xD39B12, 0xE8ABF7, 0xACE2DF, 0xC50600 }, // [43] 5.131
  { 0xC35520, 0xCC3F80, 0xD5BDA4, 0xE9D8B9, 0xB08034, 0xC7C304 }, // [44] 5.417
  { 0xC62323, 0xCEB1A7, 0xD7C876, 0xEAF703, 0xB3FE8F, 0xCA6409 }, // [45] 5.718
  { 0xC8D4D9, 0xD109C1, 0xD9BC5C, 0xEC077D, 0xB75E26, 0xCCE9C9 }, // [46] 6.037
  { 0xCB6ADA, 0xD3487F, 0xDB9A15, 0xED0ABE, 0xBA9F23, 0xCF54EB }, // [47] 6.373
  { 0xCDE5CF, 0xD56EA1, 0xDD6265, 0xEE015A, 0xBDC1D7, 0xD1A626 }, // [48] 6.727
  { 0xD0466F, 0xD77CEB, 0xDF1610, 0xEEEBE3, 0xC0C6A8, 0xD3DE37 }, // [49] 7.102
  { 0xD28D76, 0xD97426, 0xE0B5DC, 0xEFCAE4, 0xC3AE14, 0xD5FDE3 }, // [50] 7.497
  { 0xD4BB9F, 0xDB5512, 0xE24285, 0xF09EE3, 0xC678A0, 0xD805EB }, // [51] 7.914
  { 0xD6D1B6, 0xDD207E, 0xE3BCCF, 0xF16864, 0xC926EE, 0xD9F71B }, // [52] 8.354
  { 0xD8D074, 0xDED724, 0xE5256A, 0xF227DF, 0xCBB993, 0xDBD22F }, // [53] 8.819
  { 0xDAB8AC, 0xE079D3, 0xE67D18, 0xF2DDD0, 0xCE314D, 0xDD97F9 }, // [54] 9.310
  { 0xDC8B1A, 0xE20943, 0xE7C480, 0xF38AA7, 0xD08EC4, 0xDF4935 }, // [55] 9.828
  { 0xDE4889, 0xE38635, 0xE8FC54, 0xF42ED4, 0xD2D2BB, 0xE0E6A8 }, // [56] 10.375
  { 0xDFF1B8, 0xE4F160, 0xEA2539, 0xF4CABF, 0xD4FDEA, 0xE27110 }, // [57] 10.952
  { 0xE1876F, 0xE64B81, 0xEB3FD7, 0xF55ECE, 0xD71120, 0xE3E92F }, // [58] 11.561
  { 0xE30A67, 0xE79543, 0xEC4CC9, 0xF5EB62, 0xD90D17, 0xE54FB9 }, // [59] 12.205
  { 0xE47B5F, 0xE8CF59, 0xED4CAA, 0xF670D7, 0xDAF29C, 0xE6A566 }, // [60] 12.884
  { 0xE5DB0B, 0xE9FA67, 0xEE400C, 0xF6EF85, 0xDCC270, 0xE7EAE4 }, // [61] 13.601
  { 0xE72A23, 0xEB1716, 0xEF2782, 0xF767C3, 0xDE7D5F, 0xE920E3 }, // [62] 14.358
  { 0xE86952, 0xEC2601, 0xF00393, 0xF7D9E0, 0xE02427, 0xEA4808 }, // [63] 15.157
  { 0xE99945, 0xED27C5, 0xF0D4C5, 0xF8462A, 0xE1B78A, 0xEB60F8 }, // [64] 16.000
};

// return Value in [-1.0, 1.0].
__fast_inline SQ7_24 wn(void)
{
  return LCW_SQ7_24(_fx_white());
}

__fast_inline float softlimiter(float c, float x)
{
  float xf = si_fabsf(x);
  if ( xf < c ) {
    return x;
  }
  else {
    return si_copysignf( c + fx_softclipf(c, xf - c), x );
  }
}

#define FIR_LPF_TAP (5)
__fast_inline int32_t lut_with_lpf(LCWDelayBuffer *p, int32_t i)
{
#if(1)
  int32_t fir[] = { 598867, 4044352, 7490778, 4044352, 598867 };
  int64_t sum = 0;
  for (int32_t j=0; j<FIR_LPF_TAP; j++) {
    int64_t tmp = LCW_DELAY_BUFFER_LUT(p, i + j - (FIR_LPF_TAP/2));
    sum += (tmp * fir[j]);
  }

  return (int32_t)(sum >> 24);
#else
  return LCW_DELAY_BUFFER_LUT(p, i);
#endif
}

#define FIR_HPF_TAP (5)
__fast_inline int32_t lut_with_hpf(LCWDelayBuffer *p, int32_t i)
{
#if(1)
  int32_t fir[] = { 113276, 2771903, -9881080, 2771903, 113276 };
  int64_t sum = 0;
  for (int32_t j=0; j<FIR_HPF_TAP; j++) {
    int64_t tmp = LCW_DELAY_BUFFER_LUT(p, i + j - (FIR_HPF_TAP/2));
    sum += (tmp * fir[j]);
  }

  return (int32_t)(sum >> 24);
#else
  return LCW_DELAY_BUFFER_LUT(p, i);
#endif
}

void REVFX_INIT(uint32_t platform, uint32_t api)
{
  for (int32_t i=0; i<LCW_REVERB_COMB_MAX; i++) {
    LCWDelayBuffer *buf = &(revCombBuffers[i]);
    buf->buffer = &(s_reverb_ram_comb_buffer[LCW_REVERB_COMB_SIZE * i]);
    buf->size = LCW_REVERB_COMB_SIZE;
    buf->mask = LCW_REVERB_COMB_SIZE - 1;
    buf->pointer = 0;
    buf->gain = LCW_SQ7_24( 0.7 );
  }

  for (int32_t i=0; i<LCW_REVERB_AP_MAX; i++) {
    LCWDelayBuffer *buf = &(revApBuffers[i]);
    buf->buffer = &(s_reverb_ram_ap_buffer[LCW_REVERB_AP_SIZE * i]);
    buf->size = LCW_REVERB_AP_SIZE;
    buf->mask = LCW_REVERB_AP_SIZE - 1;
    buf->pointer = 0;
    buf->gain = LCW_SQ7_24( 0.7 );
  }

  s_param.mix = 0.5f;
  s_param.depth = 0.f;
  s_param.time = 0.f;
  s_state.inputGain = 0.f;
}

void REVFX_PROCESS(float *xn, uint32_t frames)
{
  float * __restrict x = xn;
  const float * x_e = x + 2*frames;

  const float dry = 1.f - s_param.mix;
  const float wet = s_param.mix;

  int32_t time1 = (int32_t)((LCW_REVERB_GAIN_TABLE_SIZE - 1) * s_param.time);
  int32_t time2 = (int32_t)((LCW_REVERB_GAIN_TABLE_SIZE - 1) * s_param.depth);

  for (int32_t i=0; i<4; i++) {
    revCombBuffers[i].gain = gainTable[time1][i];
  }
  for (int32_t i=4; i<LCW_REVERB_COMB_MAX; i++) {
    revCombBuffers[i].gain = gainTable[time2][i];
  }

  LCWDelayBuffer *comb = &(revCombBuffers[0]);
  LCWDelayBuffer *ap = &revApBuffers[0];

  const int32_t combDelay[] = { 10181, 8501, 6791, 3407, 13997, 9337 };
  const int32_t preDelay1[] = { 2969, 7211, 8059, 9767 };
  const int32_t preDelay2[] = { 1231, 1319, 1571,  977 };

  const int32_t apDelay[] = {
    //523, // = 48000 * 0.011
    337, // = 48000 * 0.007
    241, // = 48000 * 0.005
    //109, // = 48000 * 0.0023
    //53  // = 48000 * 0.0017
  };

  const SQ7_24 mix[] = {
    s_state.mix[0],
    LCW_SQ7_24(1.0) - s_state.mix[0],
    s_state.mix[1],
    LCW_SQ7_24(1.0) - s_state.mix[1]
  };

  for (; x != x_e; ) {
    float xL = *x;
    // float xR = *(x + 1);
    //int32_t inL = (int32_t)( s_state.inputGain * xL * (1 << 24) );
    int32_t inL = (int32_t)( s_state.inputGain * xL * (1 << 24) );

#if(1)
    {
      int32_t j = 4;
      LCWDelayBuffer *p = ap + j;
      int32_t out = lut_with_lpf(p, FIR_LPF_TAP);
      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = (int32_t)inL;
      inL = out;
    }
#endif

#if(1)
    {
      int32_t j = 5;
      LCWDelayBuffer *p = ap + j;
      int32_t out = lut_with_hpf(p, FIR_HPF_TAP);
      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = (int32_t)inL;
      inL = out << 1;
    }
#endif

    int64_t in1 = 0;
    {
      int32_t k = 4;
      LCWDelayBuffer *p = comb + k;

      int32_t offset = preDelay1[0] - 1;
      for (int32_t j=0; j<4; j++) {
        //int64_t tmp = lut_with_hpf(p, preDelay1[j]);
        int64_t tmp = LCW_DELAY_BUFFER_LUT(p, preDelay1[j] - offset);
        in1 += (tmp * mix[j]) >> 24;
      }

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = inL;
    }

    int64_t in2 = 0;
    {
      int32_t k = 5;
      LCWDelayBuffer *p = comb + k;

      for (int32_t j=0; j<4; j++) {
        //in2 += lut_with_hpf(p, preDelay2[j]);
        in2 += LCW_DELAY_BUFFER_LUT(p, preDelay2[j]);
      }

      //int64_t tmp = LCW_DELAY_BUFFER_LUT(p, combDelay[k]);
      int64_t tmp = lut_with_hpf(p, combDelay[k]);
      int64_t z = (tmp * p->gain) >> 24;

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = in1 + (int32_t)z;
    }

#if(1)
    int64_t combSum = 0;
    for (int32_t j=0; j<4; j++) {
      LCWDelayBuffer *p = comb + j;
      //int64_t tmp = lut_with_lpf(p, combDelay[j]);
      int64_t tmp = LCW_DELAY_BUFFER_LUT(p, combDelay[j]);
      int64_t z = (tmp * p->gain) >> 24;
      combSum += tmp;

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = (int32_t)( (in2 >> 1) + z );
    }
#else
    int64_t combSum = in2;
#endif

#if(1)
    /* comb[] -> AP */
    int64_t out = combSum >> 2;
    for (int32_t j=0; j<2; j++) {
      LCWDelayBuffer *p = ap + j;

      int64_t z = LCW_DELAY_BUFFER_LUT(p, apDelay[j]);
      int64_t in = out + ((z * p->gain) >> 24);
      out = z - ((in * p->gain) >> 24);

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = (int32_t)in;
    }
#else
    int64_t out = combSum >> 1;
#endif

    float outL = out / (float)(1 << 24);
    float yL = softlimiter( 0.1f, (dry * xL) + (wet * outL) );
    //float yL = softlimiter( 0.1f, inL1 / (float)(1 << 24) );

    *(x++) = yL;
    *(x++) = yL;

    if ( s_state.inputGain < 0.99998f ) {
      s_state.inputGain += ( (1.f - s_state.inputGain) * 0.0625f );
    }
    else { s_state.inputGain = 1.f; }
  }

  for (int32_t j=0; j<2; j++) {
    const int32_t diff = s_state.dst[j] - s_state.mix[j];
    if ( frames < LCW_ABS(diff) ) {
      if ( s_state.mix[j] < s_state.dst[j] ) {
        s_state.mix[j] += frames;
      }
      else {
        s_state.mix[j] -= frames;
      }
    }
    else {
      // dst = 0.5 + (-0.25 〜 +0.25)
      s_state.dst[j] = 0x8000 + (wn() >> 2);
    }
  }
}

void REVFX_RESUME(void)
{
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_comb_buffer,
    LCW_REVERB_COMB_BUFFER_TOTAL );
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_ap_buffer,
    LCW_REVERB_AP_BUFFER_TOTAL );
  s_state.inputGain = 0.f;
}

void REVFX_PARAM(uint8_t index, int32_t value)
{
  const float valf = q31_to_f32(value);
  switch (index) {
  case k_user_revfx_param_time:
    s_param.time = clip01f(valf);
    break;
  case k_user_revfx_param_depth:
    s_param.depth = clip01f(valf);
    break;
  case k_user_revfx_param_shift_depth:
    // Rescale to add notch around 0.5f
    s_param.mix = (valf <= 0.49f) ? 1.02040816326530612244f * valf : (valf >= 0.51f) ? 0.5f + 1.02f * (valf-0.51f) : 0.5f;
    break;
  default:
    break;
  }
}
