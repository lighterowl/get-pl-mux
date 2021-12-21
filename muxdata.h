#ifndef GETPLMUX_MUXDATA_H
#define GETPLMUX_MUXDATA_H

typedef struct MuxData_ MuxData;

#include "mux_params.h"

MuxData *mux_data_new(void);
void mux_data_destroy(MuxData *);

void mux_data_foreach(MuxData *,
                      void (*)(const gchar *, const GArray *, void *), void *);

GList *mux_data_get_muxes(MuxData *);

void mux_data_append_transmitter(MuxData *, const gchar *,
                                 const struct mux_params *);
void mux_data_sort_transmitters(MuxData *);
GArray *mux_data_get_transmitters_for_mux(MuxData *, const gchar *);

#endif
