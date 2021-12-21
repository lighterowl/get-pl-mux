#include "fetch.h"

#include <curl/curl.h>

static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                             void *userdata) {
  const size_t realsiz = size * nmemb;
  g_string_append_len(userdata, ptr, realsiz);
  return realsiz;
}

static GString *do_fetch(const char *url, gsize init_size, CURLcode *err,
                         void (*handle_init)(CURL *, void *),
                         void *handle_init_ctx) {
  CURL *const curl = curl_easy_init();
  if (!curl) {
    if (err) {
      *err = CURLE_FAILED_INIT;
    }
    return NULL;
  }

  GString *content = g_string_sized_new(init_size);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, content);
  if (handle_init != NULL) {
    handle_init(curl, handle_init_ctx);
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    g_string_free(content, TRUE);
    content = NULL;
    if (err) {
      *err = res;
    }
  }

  curl_easy_cleanup(curl);

  return content;
}

static void add_location_postdata(CURL *curl, void *postfields) {
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
}

GString *fetch_mux_data_for_location(double lat, double lon, CURLcode *err) {
  char latstr[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(latstr, sizeof(latstr), lat);
  char lonstr[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(lonstr, sizeof(lonstr), lon);
  char postfields[128];
  snprintf(postfields, sizeof(postfields), "Glat=%s&Glng=%s", latstr, lonstr);
  return do_fetch("http://sat-charts.eu/nadajniki.php", 64 * 1024, err,
                  add_location_postdata, postfields);
}

GString *fetch_tune_params_html(CURLcode *err) {
  return do_fetch("http://sat-charts.eu/dvb-t.php", 512 * 1024, err, NULL,
                  NULL);
}
