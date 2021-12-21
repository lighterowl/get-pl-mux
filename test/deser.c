#include "../deser.h"

#include <glib.h>

struct constchar_read_ctx {
  const char *const src;
  const size_t siz;
  size_t pos;
};

static gssize read_from_const_char(guint8 *buf, gsize bufsiz, void *ctx) {
  struct constchar_read_ctx *const readctx = ctx;
  const size_t left = readctx->siz - readctx->pos;
  const size_t to_copy = (left > bufsiz) ? bufsiz : left;
  memcpy(buf, readctx->src + readctx->pos, to_copy);
  readctx->pos += to_copy;
  return to_copy;
}

static MuxData *muxdata_from_const_char(const char *markup, size_t length) {
  GError *err = NULL;
  struct constchar_read_ctx ctx = {.src = markup, .siz = length, .pos = 0};
  MuxData *const md =
      deserialize_muxdata_hash(read_from_const_char, &ctx, &err);

  g_assert_cmpuint(ctx.siz, ==, ctx.pos);
  g_assert_no_error(err);
  g_assert_nonnull(md);

  return md;
}

static void test_deser_from_markup(void) {
  const char markup[] = "<mux name=\"MUX-1\">"
                        "<transmitter>"
                        "<name>testme</name>"
                        "<distance>42.25</distance>"
                        "<frequency>500000</frequency>"
                        "<bandwidth>8</bandwidth>"
                        "<modulation>3</modulation>"
                        "<delsys>3</delsys>"
                        "</transmitter>"
                        "</mux>";

  MuxData *const md = muxdata_from_const_char(markup, sizeof(markup) - 1);
  GList *const muxes = mux_data_get_muxes(md);
  g_assert_cmpuint(g_list_length(muxes), ==, 1);
  g_assert_cmpstr(muxes->data, ==, "MUX-1");
  g_list_free(muxes);

  GArray *const transmitters = mux_data_get_transmitters_for_mux(md, "MUX-1");
  g_assert_cmpuint(transmitters->len, ==, 1);

  const struct mux_params *const muxpars =
      &g_array_index(transmitters, struct mux_params, 0);
  g_assert_cmpstr(muxpars->name, ==, "testme");
  g_assert_null(muxpars->info_html);
  g_assert_cmpfloat(muxpars->distance, ==, 42.25);

  const struct tune_params *const tunepars = &muxpars->tune_parms;
  g_assert_cmpuint(tunepars->freq_khz, ==, 500000);
  g_assert_cmpuint(tunepars->bw_mhz, ==, 8);
  g_assert_cmpuint(tunepars->mod, ==, QAM_64);
  g_assert_cmpuint(tunepars->dvb_type, ==, SYS_DVBT);

  mux_data_destroy(md);
}

static void append_to_gstring(const guint8 *buf, gssize buflen, void *ctx) {
  g_string_append_len(ctx, (const gchar *)buf, buflen);
}

static void test_deser_to_markup(void) {
  MuxData *const md = mux_data_new();

  const struct mux_params transmitter = {.distance = 5.5,
                                         .name = g_strdup("foobar"),
                                         .tune_parms = {.bw_mhz = 7,
                                                        .dvb_type = SYS_DVBT2,
                                                        .freq_khz = 188000,
                                                        .mod = QAM_256}};
  mux_data_append_transmitter(md, "MUX-42", &transmitter);

  GString *output = g_string_new(NULL);
  serialize_muxdata_hash(md, append_to_gstring, output);

  const char expected[] = "<mux name=\"MUX-42\">"
                          "<transmitter>"
                          "<name>foobar</name>"
                          "<distance>5.5</distance>"
                          "<frequency>188000</frequency>"
                          "<bandwidth>7</bandwidth>"
                          "<modulation>5</modulation>"
                          "<delsys>16</delsys>"
                          "</transmitter>"
                          "</mux>";
  g_assert_cmpstr(output->str, ==, expected);

  mux_data_destroy(md);
  g_string_free(output, TRUE);
}

static void test_transmitter_sort(void) {
  const char markup[] = "<mux name=\"MUX-1\">"
                        "<transmitter>"
                        "<name>FarFarAway</name>"
                        "<distance>9000</distance>"
                        "<frequency>424000</frequency>"
                        "<bandwidth>8</bandwidth>"
                        "<modulation>3</modulation>"
                        "<delsys>3</delsys>"
                        "</transmitter>"
                        "<transmitter>"
                        "<name>MuchCloser</name>"
                        "<distance>1</distance>"
                        "<frequency>666000</frequency>"
                        "<bandwidth>8</bandwidth>"
                        "<modulation>3</modulation>"
                        "<delsys>3</delsys>"
                        "</transmitter>"
                        "</mux>";

  MuxData *const md = muxdata_from_const_char(markup, sizeof(markup) - 1);
  GArray *const transmitters = mux_data_get_transmitters_for_mux(md, "MUX-1");
  g_assert_cmpuint(transmitters->len, ==, 2);

  const struct mux_params *muxpars =
      &g_array_index(transmitters, struct mux_params, 0);
  g_assert_cmpstr(muxpars->name, ==, "MuchCloser");
  g_assert_cmpfloat(muxpars->distance, ==, 1);

  muxpars++;
  g_assert_cmpstr(muxpars->name, ==, "FarFarAway");
  g_assert_cmpfloat(muxpars->distance, ==, 9000);

  mux_data_destroy(md);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/deser/test_deser_from_markup", test_deser_from_markup);
  g_test_add_func("/deser/test_deser_to_markup", test_deser_to_markup);
  g_test_add_func("/deser/deserialize_sorts_transmitters",
                  test_transmitter_sort);

  return g_test_run();
}
