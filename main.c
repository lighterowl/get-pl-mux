#include <locale.h>
#include <math.h>

#include <gio/gio.h>
#include <glib.h>
#include <gst/gst.h>

#include "arguments.h"
#include "deser.h"
#include "fetch.h"
#include "mux_params.h"
#include "parser.h"

struct gstdvb_context {
  const struct getplmux_arguments *const program_args;
  GMainLoop *const mainloop;
  GstElement *const pipeline;
  GstElement *const dvbsrc;
  GstElement *const filesink;

  MuxData *const muxdata;
  GList *muxdata_cur_key;
  GArray *muxdata_cur_vals;
  guint muxdata_val_idx;

  int timeout_src_id;
  gboolean tuning_failed;
  unsigned int num_read_fails;
};

#define READ_FAILS_THRESHOLD 10

static GFile *mux_data_get_target_file(void) {
  return g_file_new_build_filename(g_get_user_data_dir(), "getplmux",
                                   "transmitters.xml", NULL);
}

static const struct mux_params *
gstdvb_ctx_get_current_muxparm(const struct gstdvb_context *ctx) {
  return &g_array_index(ctx->muxdata_cur_vals, struct mux_params,
                        ctx->muxdata_val_idx);
}

static void dvbsrc_set_extra_params(GstElement *dvbsrc,
                                    const GstStructure *extra_params) {
  for (gint i = 0; i < gst_structure_n_fields(extra_params); ++i) {
    const gchar *fieldname = gst_structure_nth_field_name(extra_params, i);
    const GValue *const value =
        gst_structure_get_value(extra_params, fieldname);
    g_object_set_property(G_OBJECT(dvbsrc), fieldname, value);
  }
}

static void dvbsrc_set_tune_params(GstElement *dvbsrc,
                                   const struct tune_params *params) {
  const guint bw_hz = params->bw_mhz * 1000000;
  const guint freq_hz = params->freq_khz * 1000;
  g_object_set(dvbsrc, "bandwidth-hz", bw_hz, "delsys", params->dvb_type,
               "frequency", freq_hz, "modulation", params->mod, NULL);
}

static void filesink_set_filename(const struct gstdvb_context *ctx) {
  const struct mux_params *const muxparm = gstdvb_ctx_get_current_muxparm(ctx);

  GString *const dup_name = g_string_new(muxparm->name);
  g_string_replace(dup_name, "\"", "", 0);
  g_string_replace(dup_name, " ", "_", 0);

  gchar *const fname =
      g_strdup_printf("%s_%s_%u_kHz.ts", (char *)ctx->muxdata_cur_key->data,
                      dup_name->str, muxparm->tune_parms.freq_khz);
  g_object_set(ctx->filesink, "location", fname, NULL);

  g_free(fname);
  g_string_free(dup_name, TRUE);
}

static void pipeline_set_properties(const struct gstdvb_context *ctx) {
  filesink_set_filename(ctx);
  dvbsrc_set_tune_params(ctx->dvbsrc,
                         &gstdvb_ctx_get_current_muxparm(ctx)->tune_parms);
  if (ctx->program_args->dvbsrc_extra_props) {
    dvbsrc_set_extra_params(ctx->dvbsrc, ctx->program_args->dvbsrc_extra_props);
  }
}

static void capture_start(struct gstdvb_context *ctx) {
  ctx->tuning_failed = FALSE;
  ctx->num_read_fails = 0;
  pipeline_set_properties(ctx);
  const struct mux_params *const muxparm = gstdvb_ctx_get_current_muxparm(ctx);
  g_print("Starting tune to %s, transmitter %s\n",
          (const char *)ctx->muxdata_cur_key->data, muxparm->name);
  gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
}

static gboolean pipeline_set_null_state(gpointer user_data) {
  struct gstdvb_context *const ctx = user_data;
  gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
  return FALSE;
}

static void switch_to_next_mux(struct gstdvb_context *ctx) {
  ctx->muxdata_cur_key = ctx->muxdata_cur_key->next;
  ctx->muxdata_val_idx = 0;
  if (ctx->muxdata_cur_key) {
    ctx->muxdata_cur_vals = mux_data_get_transmitters_for_mux(
        ctx->muxdata, ctx->muxdata_cur_key->data);
  }
}

static void switch_to_next_param(struct gstdvb_context *ctx) {
  if (ctx->tuning_failed || ctx->num_read_fails >= READ_FAILS_THRESHOLD) {
    /* capture incomplete/failed : try with next transmitter for this MUX */
    ctx->muxdata_val_idx++;
    if (ctx->muxdata_val_idx >= ctx->muxdata_cur_vals->len) {
      g_print(
          "All transmitters for %s tried, switching to next MUX if available\n",
          (const char *)ctx->muxdata_cur_key->data);
      switch_to_next_mux(ctx);
    }
  } else {
    /* capture for this MUX successful, go to next one */
    switch_to_next_mux(ctx);
  }

  if (ctx->muxdata_cur_key) {
    capture_start(ctx);
  } else {
    /* we're done */
    g_main_loop_quit(ctx->mainloop);
  }
}

static void pipeline_state_changed(GstMessage *msg,
                                   struct gstdvb_context *ctx) {
  GstState old_state, new_state;
  gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);

  if (old_state == GST_STATE_PAUSED && new_state == GST_STATE_PLAYING) {
    g_print("Tuned to %d kHz, starting capture for %d seconds...\n",
            gstdvb_ctx_get_current_muxparm(ctx)->tune_parms.freq_khz,
            ctx->program_args->capture_duration_seconds);
    ctx->timeout_src_id =
        g_timeout_add_seconds(ctx->program_args->capture_duration_seconds,
                              pipeline_set_null_state, ctx);
  } else if (new_state == GST_STATE_NULL) {
    switch_to_next_param(ctx);
  }
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  (void)bus;

  struct gstdvb_context *const ctx = data;

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    g_print("End of stream\n");
    g_main_loop_quit(ctx->mainloop);
    break;

  case GST_MESSAGE_WARNING: {
    gchar *debug;
    GError *warning;

    gst_message_parse_warning(msg, &warning, &debug);
    g_free(debug);

    g_printerr("Warning: %s\n", warning->message);
    g_error_free(warning);
  } break;

  case GST_MESSAGE_ERROR: {
    if (msg->src == GST_OBJECT(ctx->dvbsrc) && ctx->tuning_failed) {
      g_print("Tuning failed, trying next param if available...\n");
      g_idle_add(pipeline_set_null_state, ctx);
      break;
    }
    gchar *debug;
    GError *error;

    gst_message_parse_error(msg, &error, &debug);
    g_free(debug);

    g_printerr("Error: %s\n", error->message);
    g_error_free(error);

    g_main_loop_quit(ctx->mainloop);
  } break;

  case GST_MESSAGE_ELEMENT:
    if (msg->src == GST_OBJECT(ctx->dvbsrc)) {
      const GstStructure *const stru = gst_message_get_structure(msg);
      const gchar *const name = gst_structure_get_name(stru);
      if (g_strcmp0(name, "dvb-read-failure") == 0) {
        ++ctx->num_read_fails;
      }
      if (ctx->num_read_fails >= READ_FAILS_THRESHOLD) {
        g_print("Signal lost, jumping to next param");
        g_source_remove(ctx->timeout_src_id);
        g_idle_add(pipeline_set_null_state, ctx);
      }
    }
    break;

  case GST_MESSAGE_STATE_CHANGED: {
    /* we don't really care about individual elements */
    if (msg->src == GST_OBJECT(ctx->pipeline)) {
      pipeline_state_changed(msg, ctx);
    }
    break;
  }

  default:
    break;
  }

  return TRUE;
}

static void on_tuning_fail(GstElement *object, gpointer user_data) {
  (void)object;

  struct gstdvb_context *const ctx = user_data;
  ctx->tuning_failed = TRUE;
}

static gboolean on_event_loop_start(gpointer user_data) {
  capture_start(user_data);
  return FALSE;
}

static MuxData *fetch_muxdata_hash(double lat, double lon) {
  CURLcode err;
  GString *content = fetch_mux_data_for_location(lat, lon, &err);
  if (!content) {
    g_printerr("Could not obtain location-based transmitter list : %s",
               curl_easy_strerror(err));
    return NULL;
  }
  MuxData *const parsed =
      parse_mux_params_from_html(content->str, (int)content->len);
  g_string_free(content, TRUE);

  content = fetch_tune_params_html(&err);
  if (!content) {
    g_printerr("Could not obtain complete transmitter list : %s",
               curl_easy_strerror(err));
    mux_data_destroy(parsed);
    return NULL;
  }

  parse_tune_params_to_mux_params(parsed, content->str, (int)content->len);
  g_string_free(content, TRUE);
  return parsed;
}

static void write_to_outstream(const guint8 *buf, gssize bufsiz, void *ctx) {
  g_output_stream_write(ctx, buf,
                        bufsiz < 0 ? strlen((const char *)buf) : (gsize)bufsiz,
                        NULL, NULL);
}

static void mux_data_save_to_file(MuxData *md) {
  GFile *const f = mux_data_get_target_file();
  {
    GFile *const parent = g_file_get_parent(f);
    g_file_make_directory_with_parents(parent, NULL, NULL);
    g_object_unref(parent);
  }

  GOutputStream *out =
      G_OUTPUT_STREAM(g_file_create(f, G_FILE_CREATE_NONE, NULL, NULL));
  GObject *unref_me = G_OBJECT(out);
  if (!out) {
    GFileIOStream *io = g_file_open_readwrite(f, NULL, NULL);
    if (!io) {
      goto beach;
    }
    out = g_io_stream_get_output_stream(G_IO_STREAM(io));
    unref_me = G_OBJECT(io);
  }

  serialize_muxdata_hash(md, write_to_outstream, out);
  g_object_unref(unref_me);

beach:
  g_object_unref(f);
}

static gssize read_from_input_stream(guint8 *buf, gsize bufsiz, void *ctx) {
  return g_input_stream_read(ctx, buf, bufsiz, NULL, NULL);
}

static MuxData *mux_data_read_from_file(void) {
  GFile *const f = mux_data_get_target_file();
  GFileInputStream *const is = g_file_read(f, NULL, NULL);
  MuxData *md = NULL;
  if (is) {
    md = deserialize_muxdata_hash(read_from_input_stream, is, NULL);
    g_object_unref(is);
  }
  g_object_unref(f);
  return md;
}

static gboolean location_is_specified(const struct getplmux_arguments *args) {
  return isfinite(args->latitude) && isfinite(args->longitude);
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  {
    CURLcode init_rv = curl_global_init(CURL_GLOBAL_ALL);
    if (init_rv != CURLE_OK) {
      g_printerr("Failed to initialise libcurl : %s",
                 curl_easy_strerror(init_rv));
      return 1;
    }
  }

  int rv = 1;
  struct getplmux_arguments program_args;
  if (parse_arguments(&program_args, argc, argv)) {
    goto beach;
  }

  GstElement *source = gst_element_factory_make("dvbsrc", NULL);
  if (!source) {
    g_printerr("Failed to create a 'dvbsrc' element.\n"
               "Make sure you have gst-plugins-bad installed.\n");
    goto beach;
  }

  MuxData *muxdata = NULL;
  if (!program_args.force_refresh) {
    muxdata = mux_data_read_from_file();
  }

  if (!muxdata) {
    if (location_is_specified(&program_args)) {
      muxdata =
          fetch_muxdata_hash(program_args.latitude, program_args.longitude);
    } else {
      g_printerr("Cached transmitters not available, but location not "
                 "specified so cannot fetch - quitting.\n");
    }
  }

  if (!muxdata) {
    goto beach2;
  }

  GList *const muxdata_keys = mux_data_get_muxes(muxdata);
  if (muxdata_keys == NULL) {
    g_printerr("No transmitters found.\n");
    goto beach3;
  }

  rv = 0;

  GstElement *const sink = gst_element_factory_make("filesink", NULL);
  GstElement *const pipeline = gst_pipeline_new("mux-recorder");
  gst_pipeline_set_auto_flush_bus(GST_PIPELINE(pipeline), FALSE);

  GstBus *const bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  GMainLoop *const loop = g_main_loop_new(NULL, FALSE);

  struct gstdvb_context ctx = {
      .program_args = &program_args,
      .mainloop = loop,
      .pipeline = pipeline,
      .dvbsrc = source,
      .filesink = sink,
      .muxdata = muxdata,
      .muxdata_cur_key = muxdata_keys,
      .muxdata_cur_vals =
          mux_data_get_transmitters_for_mux(muxdata, muxdata_keys->data),
      .muxdata_val_idx = 0};

  const guint bus_watch_id = gst_bus_add_watch(bus, bus_call, &ctx);
  gst_object_unref(bus);

  gst_bin_add_many(GST_BIN(pipeline), source, sink, NULL);
  gst_element_link(source, sink);

  g_signal_connect(G_OBJECT(source), "tuning-fail", G_CALLBACK(on_tuning_fail),
                   &ctx);
  source = NULL; /* ownership transferred to pipeline */

  g_print("Starting...\n");
  g_idle_add(on_event_loop_start, &ctx);
  g_main_loop_run(loop);
  g_print("All captures completed, shutting down\n");
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);
  g_list_free(muxdata_keys);

beach3:
  mux_data_save_to_file(muxdata);
  mux_data_destroy(muxdata);

beach2:
  g_clear_pointer(&source, gst_object_unref);

beach:
  free_arguments(&program_args);
  curl_global_cleanup();

  return rv;
}
