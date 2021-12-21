#ifndef GETPLMUX_MUX_PARAMS_H
#define GETPLMUX_MUX_PARAMS_H

#include <glib.h>

#include "tune_params.h"

struct mux_params {
  gchar *name;
  gchar *info_html;
  gdouble distance;
  struct tune_params tune_parms;
};

static inline void mux_params_clear(struct mux_params *params) {
  g_free(params->name);
  g_free(params->info_html);
}

#endif
