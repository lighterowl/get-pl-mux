#include "arguments.h"

#include <gst/gst.h>

#include <math.h>

static void init_arguments(struct getplmux_arguments *args) {
  args->dvbsrc_extra_props = NULL;
  args->capture_duration_seconds = 30;
  args->force_refresh = FALSE;
  args->latitude = args->longitude = NAN;
}

static GstStructure *parse_as_gst_struct(const gchar *value, GError **error) {
  /* the serialized representation always starts with the struct name which we
   * must add manually here. */
  gchar *const serialized = g_strdup_printf("dvbsrc_params, %s", value);
  gchar *end = NULL;
  GstStructure *const struc = gst_structure_from_string(serialized, &end);
  g_free(serialized);

  if (!struc) {
    if (end) {
      const ptrdiff_t err_loc = end - serialized;
      *error =
          g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                      "Could not parse %s as a GstStructure : error around %td",
                      value, err_loc);
    } else {
      *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                           "Could not parse %s as a GstStructure", value);
    }
  }

  return struc;
}

static guint get_num_splitted_strs(gchar **splitted) {
  guint idx = 0;
  while (*splitted) {
    ++idx;
    ++splitted;
  }
  return idx;
}

static gboolean latlon_parse(gdouble *out, const char *str) {
  errno = 0;
  char *endp;
  const double parsed = strtod(str, &endp);
  if (errno == 0 && *endp == 0 && parsed > -90.0 && parsed < 90.0) {
    *out = parsed;
    return TRUE;
  } else {
    return FALSE;
  }
}

struct argparse_ctx {
  struct getplmux_arguments *const args;
};

#define latlon_parse_or_error(member, strval)                                  \
  do {                                                                         \
    if (!latlon_parse(&args->member, (strval))) {                              \
      *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_FAILED,              \
                           "Could not parse %s as " #member, (strval));        \
      goto beach;                                                              \
    }                                                                          \
  } while (0)

static gboolean location_parse(const gchar *option_name, const gchar *value,
                               gpointer data, GError **error) {
  (void)option_name;
  gchar **splitted = g_strsplit(value, ":", -1);
  gboolean rv = FALSE;
  if (get_num_splitted_strs(splitted) != 2) {
    *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                         "The location specifier must contain two "
                         "colon-delimited decimal numbers");
    goto beach;
  }

  struct argparse_ctx *const parse_ctx = data;
  struct getplmux_arguments *const args = parse_ctx->args;
  latlon_parse_or_error(latitude, splitted[0]);
  latlon_parse_or_error(longitude, splitted[1]);
  rv = TRUE;

beach:
  g_strfreev(splitted);
  return rv;
}

#undef latlon_parse_or_error

int parse_arguments(struct getplmux_arguments *args, int argc, char **argv) {
  init_arguments(args);

  gchar *dvbsrc_params = NULL;
  const GOptionEntry options[] = {
      {"duration", 'd', 0, G_OPTION_ARG_INT, &args->capture_duration_seconds,
       "Capture duration (in seconds)", NULL},
      {"location", 0, 0, G_OPTION_ARG_CALLBACK, location_parse,
       "The location to lookup transmitters for as colon-separated latitude "
       "and longitude, for example : 52.393:16.857",
       NULL},
      {"refresh", 'r', 0, G_OPTION_ARG_NONE, &args->force_refresh,
       "Force refreshing cached transmitter data", NULL},
      {"dvbsrc-extra-params", 0, 0, G_OPTION_ARG_STRING, &dvbsrc_params,
       "Additional properties to apply to the dvbsrc element as a serialized "
       "GstStructure, for example : adapter=5,frontend=2",
       NULL},
      G_OPTION_ENTRY_NULL};

  struct argparse_ctx parse_ctx = {.args = args};
  int rv = 1;

  GOptionContext *const option_ctx = g_option_context_new(NULL);
  GOptionGroup *const main_group =
      g_option_group_new(NULL, NULL, NULL, &parse_ctx, NULL);
  g_option_group_add_entries(main_group, options);
  g_option_context_set_main_group(option_ctx, main_group);
  g_option_context_add_group(option_ctx, gst_init_get_option_group());
  GError *err = NULL;
  if (!g_option_context_parse(option_ctx, &argc, &argv, &err)) {
    g_printerr("Error initializing: %s\n", GST_STR_NULL(err->message));
    g_error_free(err);
    goto beach;
  }

  /* I tried integrating this as a G_OPTION_ARG_CALLBACK, but the gst_structure
   * functions segfaulted, which I presume is due to to gst_init() not being
   * called before the argument-parsing callback is. oh well. */
  if (dvbsrc_params) {
    GstStructure *const stru = parse_as_gst_struct(dvbsrc_params, &err);
    g_free(dvbsrc_params);
    if (!stru) {
      g_printerr("Error initializing: %s\n", GST_STR_NULL(err->message));
      g_error_free(err);
      goto beach;
    }
    args->dvbsrc_extra_props = stru;
  }

  rv = 0;

beach:
  g_option_context_free(option_ctx);
  return rv;
}

void free_arguments(struct getplmux_arguments *args) {
  gst_clear_structure(&args->dvbsrc_extra_props);
}
