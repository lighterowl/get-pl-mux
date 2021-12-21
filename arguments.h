#ifndef GETPLMUX_ARGUMENTS_H
#define GETPLMUX_ARGUMENTS_H

#include <glib.h>

typedef struct _GstStructure GstStructure;

struct getplmux_arguments {
  GstStructure *dvbsrc_extra_props;
  double latitude;
  double longitude;
  gint capture_duration_seconds;
  gboolean force_refresh;
};

int parse_arguments(struct getplmux_arguments *args, int argc, char **argv);

void free_arguments(struct getplmux_arguments *args);

#endif
