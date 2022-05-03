#include "deser.h"

#include "mux_params.h"

struct serialize_ctx {
  void (*savefn)(const guint8 *, gssize, void *);
  void *savefn_ctx;
};

static void save_as_markupv(const struct serialize_ctx *ctx, const char *format,
                            va_list args) {
  gchar *const res = g_markup_vprintf_escaped(format, args);
  ctx->savefn((const guint8 *)res, -1, ctx->savefn_ctx);
  g_free(res);
}

G_GNUC_PRINTF(2, 3)
static void save_as_markup(const struct serialize_ctx *ctx, const char *format,
                           ...) {
  va_list args;
  va_start(args, format);
  save_as_markupv(ctx, format, args);
  va_end(args);
}

static void save_mux_params(const struct serialize_ctx *ctx,
                            const struct mux_params *muxparms) {
  const struct tune_params *const tunepars = &muxparms->tune_parms;
  gchar distance_str[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(distance_str, sizeof(distance_str), muxparms->distance);
  save_as_markup(ctx,
                 " <transmitter>\n"
                 "  <name>%s</name>\n"
                 "  <distance>%s</distance>\n"
                 "  <frequency>%u</frequency>\n"
                 "  <bandwidth>%u</bandwidth>\n"
                 "  <modulation>%u</modulation>\n"
                 "  <delsys>%u</delsys>\n"
                 " </transmitter>\n",
                 muxparms->name, distance_str, tunepars->freq_khz,
                 tunepars->bw_mhz, tunepars->mod, tunepars->dvb_type);
}

static void muxdata_hash_print(const gchar *mux, const GArray *transmitters,
                               void *user_data) {
  const struct serialize_ctx *const ser_ctx = user_data;

  save_as_markup(ser_ctx, "<mux name=\"%s\">\n", mux);

  for (guint i = 0; i < transmitters->len; ++i) {
    const struct mux_params *const param =
        &g_array_index(transmitters, struct mux_params, i);
    save_mux_params(ser_ctx, param);
  }

  save_as_markup(ser_ctx, "</mux>\n");
}

void serialize_muxdata_hash(MuxData *muxdata,
                            void (*savefn)(const guint8 *, gssize, void *),
                            void *savefn_ctx) {
  struct serialize_ctx ctx = {.savefn = savefn, .savefn_ctx = savefn_ctx};
  mux_data_foreach(muxdata, muxdata_hash_print, &ctx);
}

struct gmarkup_parse_state {
  MuxData *mux_data;
  gchar *elem_name;
  gchar *cur_mux;
  GString *text_buf;
  struct mux_params mux_parm_buf;
};

static void parse_state_destroy(struct gmarkup_parse_state *state) {
  g_clear_pointer(&state->elem_name, g_free);
  g_clear_pointer(&state->cur_mux, g_free);
  g_string_free(state->text_buf, TRUE);
}

static void on_transmitter_end_element(GMarkupParseContext *context,
                                       const gchar *element_name,
                                       gpointer user_data, GError **error) {
  (void)context;

  struct gmarkup_parse_state *const state = user_data;
  struct mux_params *const muxparm = &state->mux_parm_buf;
  struct tune_params *const tuneparms = &muxparm->tune_parms;
  const gchar *const input = state->text_buf->str;

  if (g_strcmp0(element_name, "name") == 0) {
    muxparm->name = g_strdup(input);
  } else if (g_strcmp0(element_name, "distance") == 0) {
    muxparm->distance = g_ascii_strtod(input, NULL);
  } else if (g_strcmp0(element_name, "frequency") == 0) {
    tuneparms->freq_khz = g_ascii_strtoull(input, NULL, 10);
  } else if (g_strcmp0(element_name, "bandwidth") == 0) {
    tuneparms->bw_mhz = g_ascii_strtoull(input, NULL, 10);
  } else if (g_strcmp0(element_name, "modulation") == 0) {
    tuneparms->mod = g_ascii_strtoull(input, NULL, 10);
  } else if (g_strcmp0(element_name, "delsys") == 0) {
    tuneparms->dvb_type = g_ascii_strtoull(input, NULL, 10);
  } else {
    *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                         "Element %s not expected at this point", element_name);
  }

  g_string_set_size(state->text_buf, 0);
}

static void on_transmitter_text(GMarkupParseContext *context, const gchar *text,
                                gsize text_len, gpointer user_data,
                                GError **error) {
  (void)context;
  (void)error;

  struct gmarkup_parse_state *const state = user_data;
  g_string_append_len(state->text_buf, text, text_len);
}

static const GMarkupParser transmitter_parser = {.start_element = NULL,
                                                 .end_element =
                                                     on_transmitter_end_element,
                                                 .text = on_transmitter_text,
                                                 .passthrough = NULL,
                                                 .error = NULL};

static void on_toplevel_start_element(GMarkupParseContext *context,
                                      const gchar *element_name,
                                      const gchar **attribute_names,
                                      const gchar **attribute_values,
                                      gpointer user_data, GError **error) {
  struct gmarkup_parse_state *const state = user_data;
  if (!state->cur_mux) {
    if (g_strcmp0(element_name, "mux") == 0) {
      gchar *muxname;
      if (g_markup_collect_attributes(element_name, attribute_names,
                                      attribute_values, error,
                                      G_MARKUP_COLLECT_STRDUP, "name", &muxname,
                                      G_MARKUP_COLLECT_INVALID)) {
        state->cur_mux = muxname;
      }
    } else {
      *error =
          g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                      "Element %s not expected at this point", element_name);
    }
  } else if (g_strcmp0(element_name, "transmitter") == 0) {
    g_markup_parse_context_push(context, &transmitter_parser, user_data);
  } else {
    *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                         "Element %s not expected at this point", element_name);
  }
}

static void on_toplevel_end_element(GMarkupParseContext *context,
                                    const gchar *element_name,
                                    gpointer user_data, GError **error) {
  struct gmarkup_parse_state *const state = user_data;

  if (g_strcmp0(element_name, "transmitter") == 0) {
    g_markup_parse_context_pop(context);
    mux_data_append_transmitter(state->mux_data, state->cur_mux,
                                &state->mux_parm_buf);
    memset(&state->mux_parm_buf, 0, sizeof(state->mux_parm_buf));
  } else if (g_strcmp0(element_name, "mux") == 0) {
    g_clear_pointer(&state->cur_mux, g_free);
  } else {
    *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                         "Element %s not expected at this point", element_name);
  }
}

static const GMarkupParser toplevel_parser = {
    .start_element = on_toplevel_start_element,
    .end_element = on_toplevel_end_element,
    .text = NULL,
    .passthrough = NULL,
    .error = NULL};

MuxData *deserialize_muxdata_hash(gssize (*readfn)(guint8 *, gsize, void *),
                                  void *readfn_ctx, GError **error) {
  MuxData *md = mux_data_new();
  struct gmarkup_parse_state state;
  memset(&state, 0, sizeof(state));
  state.mux_data = md;
  state.text_buf = g_string_new(NULL);

  GMarkupParseContext *const markup_ctx =
      g_markup_parse_context_new(&toplevel_parser, 0, &state, NULL);
  guint8 buf[4096];
  gssize read_size;
  while ((read_size = readfn(buf, sizeof(buf), readfn_ctx)) > 0) {
    if (!g_markup_parse_context_parse(markup_ctx, (const gchar *)buf, read_size,
                                      error)) {
      g_clear_pointer(&md, mux_data_destroy);
      goto beach;
    }
  }
  if (!g_markup_parse_context_end_parse(markup_ctx, error)) {
    g_clear_pointer(&md, mux_data_destroy);
    goto beach;
  }

  mux_data_sort_transmitters(md);

beach:
  g_markup_parse_context_free(markup_ctx);
  parse_state_destroy(&state);
  return md;
}
