#ifndef GETPLMUX_DESER_H
#define GETPLMUX_DESER_H

#include <glib.h>

#include "muxdata.h"

void serialize_muxdata_hash(MuxData *muxdata,
                            void (*savefn)(const guint8 *, gssize, void *),
                            void *savefn_ctx);

MuxData *deserialize_muxdata_hash(gssize (*readfn)(guint8 *, gsize, void *),
                                  void *readfn_ctx, GError **error);

#endif
