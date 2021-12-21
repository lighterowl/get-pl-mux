#ifndef GETPLMUX_TUNE_PARAMS_H
#define GETPLMUX_TUNE_PARAMS_H

#include <linux/dvb/frontend.h>

struct tune_params {
  unsigned int freq_khz;
  unsigned int bw_mhz;
  enum fe_modulation mod;
  enum fe_delivery_system dvb_type;
};

#endif
