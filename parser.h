#ifndef GETPLMUX_PARSER_H
#define GETPLMUX_PARSER_H

#include <glib.h>

#include "muxdata.h"

/* key : MUX identifier as char*, i.e. "MUX-n"
 * value : GArray of mux_params sorted by distance
 * everything will be freed when the hash table is destroyed. if you want to
 * keep something, steal it.
 */
MuxData *parse_mux_params_from_html(const char *html, int size);

/* parse the list of all transmitters in order to fill in missing tune_params
 * members, namely bandwidth, modulation, and DVB-T/T2. */
void parse_tune_params_to_mux_params(MuxData *muxdata, const char *html,
                                     int size);

#endif
