#include "muxdata.h"

#include <glib.h>

struct MuxData_ {
  GHashTable *hash;
};

static void mux_params_clear_wrap(gpointer p) { mux_params_clear(p); }

static void garray_free_with_segment(gpointer p) { g_array_free(p, TRUE); }

static gint g_strcmp0_gcomparefunc(gconstpointer a, gconstpointer b) {
  return g_strcmp0(a, b);
}

MuxData *mux_data_new(void) {
  MuxData *rv = g_new(MuxData, 1);
  rv->hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                   garray_free_with_segment);
  return rv;
}

void mux_data_destroy(MuxData *md) {
  g_hash_table_destroy(md->hash);
  g_free(md);
}

GList *mux_data_get_muxes(MuxData *md) {
  return g_list_sort(g_hash_table_get_keys(md->hash), g_strcmp0_gcomparefunc);
}

void mux_data_append_transmitter(MuxData *md, const gchar *mux,
                                 const struct mux_params *params) {
  GHashTable *const hash = md->hash;
  GArray *param_array = g_hash_table_lookup(hash, mux);
  if (!param_array) {
    param_array = g_array_new(FALSE, FALSE, sizeof(struct mux_params));
    g_array_set_clear_func(param_array, mux_params_clear_wrap);
    g_hash_table_insert(hash, g_strdup(mux), param_array);
  }
  g_array_append_val(param_array, *params);
}

static gint by_distance_cmpfn(gconstpointer a, gconstpointer b) {
  const struct mux_params *const pA = a, *pB = b;
  if (pA->distance < pB->distance) {
    return -1;
  } else if (pA->distance > pB->distance) {
    return 1;
  } else {
    return 0;
  }
}

static void sort_transmitter_array(gpointer key, gpointer value,
                                   gpointer user_data) {
  (void)key;
  (void)user_data;
  g_array_sort(value, by_distance_cmpfn);
}

void mux_data_sort_transmitters(MuxData *md) {
  g_hash_table_foreach(md->hash, sort_transmitter_array, NULL);
}

GArray *mux_data_get_transmitters_for_mux(MuxData *md, const gchar *mux) {
  return g_hash_table_lookup(md->hash, mux);
}

struct foreach_wrap_ctx {
  void (*fn)(const gchar *, const GArray *, void *);
  void *fn_ctx;
};

static void foreach_wrap_fn(gpointer key, gpointer value, gpointer user_data) {
  struct foreach_wrap_ctx *const wrapctx = user_data;
  wrapctx->fn(key, value, wrapctx->fn_ctx);
}

void mux_data_foreach(MuxData *md,
                      void (*fn)(const gchar *, const GArray *, void *),
                      void *fn_ctx) {
  struct foreach_wrap_ctx wrapctx = {.fn = fn, .fn_ctx = fn_ctx};
  g_hash_table_foreach(md->hash, foreach_wrap_fn, &wrapctx);
}
