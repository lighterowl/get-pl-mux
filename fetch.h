#ifndef GETPLMUX_FETCH_H
#define GETPLMUX_FETCH_H

#include <curl/curl.h>
#include <glib.h>

GString *fetch_mux_data_for_location(double lat, double lon, CURLcode *err);
GString *fetch_tune_params_html(CURLcode *err);

#endif
