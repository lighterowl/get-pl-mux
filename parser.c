#include "parser.h"
#include <libxml/HTMLparser.h>

#include "mux_params.h"
#include "muxdata.h"

struct muxparams_parser_ctx {
  struct mux_params parse_buf;
  gchar *parse_buf_mux;

  htmlSAXHandlerPtr sax;
  MuxData *muxdata;

  int cur_row;
  int cur_column;
  bool in_dvb_table;
  bool encoder_error;
};

static void reset_parse_state(struct muxparams_parser_ctx *ctx) {
  mux_params_clear(&ctx->parse_buf);
  g_clear_pointer(&ctx->parse_buf_mux, g_free);
  ctx->cur_row = ctx->cur_column = 0;
  ctx->in_dvb_table = false;
}

static const xmlChar *get_attr(const xmlChar **attrs, const char *name) {
  while (attrs && *attrs) {
    if (strcmp((const char *)attrs[0], name) == 0) {
      return attrs[1];
    }
    attrs += 2;
  }
  return NULL;
}

static bool is_attr(const xmlChar **attrs, const char *name,
                    const char *value) {
  const xmlChar *const val = get_attr(attrs, name);
  return val ? strcmp((const char *)val, value) == 0 : false;
}

static gboolean sanitize_mux_id(gchar *mux_id, gsize len) {
  /* the site uses the symbol MUX-nT2 for transmitters which already operate
   * in DVB-T2 mode. this causes the application to (rightly) treat these
   * MUXes as separate which is not desired in this case. */
  if (strcmp(mux_id + len - 2, "T2") == 0) {
    mux_id[len - 2] = 0;
    return true;
  } else {
    return false;
  }
}

static void characters(void *ctx, const xmlChar *ch, int leni) {
  struct muxparams_parser_ctx *const my_ctx = ctx;
  if ((ch[0] == '~' && ch[1] == 0) || leni <= 0)
    return;

  const gsize len = (gsize)leni;
  const char *const chch = (const char *)ch;
  switch (my_ctx->cur_column) {
  case 1:
    my_ctx->parse_buf.tune_parms.freq_khz =
        (guint)(g_ascii_strtod(chch, NULL) * 1000.0);
    break;
  case 2: {
    gchar *const mux_id = g_strndup(chch, len);
    sanitize_mux_id(mux_id, len);
    my_ctx->parse_buf_mux = mux_id;
    break;
  }
  case 3:
    if (!my_ctx->encoder_error) {
      /* properly decoded CP1250 - just copy. */
      my_ctx->parse_buf.name = g_strndup(chch, len);
    } else {
      /* parsing in "broken" mode. the characters we got are in CP1250. */
      my_ctx->parse_buf.name =
          g_convert(chch, len, "UTF-8", "CP1250", NULL, NULL, NULL);
    }
    break;
  case 5:
    my_ctx->parse_buf.distance = g_ascii_strtod(chch, NULL);
    break;
  default:
    break;
  }
}

static void start_element(void *ctx, const xmlChar *name,
                          const xmlChar **atts) {
  struct muxparams_parser_ctx *const my_ctx = ctx;
  if (strcmp((const char *)name, "table") == 0 &&
      is_attr(atts, "border", "1") && is_attr(atts, "class", "tabelka_dvbt")) {
    my_ctx->in_dvb_table = true;
    my_ctx->cur_row = 0;
  }

  if (my_ctx->in_dvb_table) {
    if (strcmp((const char *)name, "tr") == 0) {
      my_ctx->cur_column = 0;
      if (my_ctx->cur_row >= 1)
        my_ctx->sax->characters = characters;
    }
    if (strcmp((const char *)name, "a") == 0 && my_ctx->cur_row >= 1) {
      const char *const href = (const char *)get_attr(atts, "href");
      if (href) {
        my_ctx->parse_buf.info_html = g_strdup((const gchar *)href);
      }
    }
  }
}

static void end_element(void *ctx, const xmlChar *name) {
  struct muxparams_parser_ctx *const my_ctx = ctx;
  if (strcmp((const char *)name, "table") == 0 && my_ctx->in_dvb_table) {
    my_ctx->in_dvb_table = false;
  }
  if (my_ctx->in_dvb_table) {
    if (strcmp((const char *)name, "td") == 0) {
      my_ctx->cur_column++;
    } else if (strcmp((const char *)name, "tr") == 0) {
      if (my_ctx->cur_row >= 1 && my_ctx->cur_column >= 6 &&
          my_ctx->parse_buf_mux) {
        mux_data_append_transmitter(my_ctx->muxdata, my_ctx->parse_buf_mux,
                                    &my_ctx->parse_buf);
        memset(&my_ctx->parse_buf, 0, sizeof(my_ctx->parse_buf));
      }
      g_clear_pointer(&my_ctx->parse_buf_mux, g_free);
      my_ctx->cur_row++;
      my_ctx->cur_column = -1;
    }
  }
}

static void xml_error(void *ctx, xmlErrorPtr error) {
  struct muxparams_parser_ctx *const my_ctx = ctx;
  if (error->domain == XML_FROM_IO && error->code == XML_IO_ENCODER) {
    my_ctx->encoder_error = true;
  }
}

MuxData *parse_mux_params_from_html(const char *html, int size) {
  /* while developing this, I learned that libxml's SAX parser is much better
   * suited to processing files whose encoding is borked. the tree parser falls
   * back to ISO-8859-1 encoding as a "safe default" even if the API call is
   * supposed to force a given encoding (via one of the parameters) and
   * HTML_PARSE_IGNORE_ENC is given as one of the flags.
   * I thought I could force the tree parser to give me the untouched bytes from
   * the input by specifying "UTF-8" (similarly to the SAX parser), as the only
   * thing that libxml's UTF-8 encoding handlers do is memcpy(). that did not
   * work, as one of the code paths in HTMLparser.c:htmlCurrentChar() actually
   * verifies if the input is valid UTF-8 : obviously, this file fails this
   * check and the encoding for the given parser is reset to ISO-8859-1, which
   * results in CP1250 bytes converted to characters according to ISO-8859-1 and
   * then encoded to UTF-8 : effectively, you get ñ instead of ń - among other
   * things.
   *
   * I actually developed the SAX-based solution first, and then thought to try
   * doing the same thing with a tree-based approach only to learn that it
   * doesn't work because of the encoding thing.
   *
   * I hope that the SAX behaviour is intentional and not an oversight, as
   * handling such borked-encoding files otherwise would require playing around
   * with custom encoding handlers which just sounds a lot harder. */

  LIBXML_TEST_VERSION;

  htmlSAXHandler sax;
  memset(&sax, 0, sizeof(sax));
  sax.startElement = start_element;
  sax.endElement = end_element;

  struct muxparams_parser_ctx my_ctx;
  memset(&my_ctx, 0, sizeof(my_ctx));
  my_ctx.sax = &sax;
  my_ctx.muxdata = mux_data_new();

  htmlParserCtxtPtr ctxt = htmlNewParserCtxt();
  htmlSAXHandlerPtr oldsax = ctxt->sax;
  void *oldctx = ctxt->userData;
  ctxt->sax = &sax;
  ctxt->userData = &my_ctx;

  /* the site is finnicky when it comes to the encoding it uses. sometimes it
   * returns well-encoded files in CP1250, sometimes it does ... weird things
   * when it comes to the character encoding, which is why we try reading it
   * twice : once as actual CP1250 and then, if it fails, in "broken" mode. */
  xmlSetStructuredErrorFunc(&my_ctx, xml_error);
  htmlCtxtReadMemory(ctxt, html, size, NULL, "CP1250", 0);
  xmlSetStructuredErrorFunc(NULL, NULL);
  if (my_ctx.encoder_error) {
    /* when forcing UTF-8 as the character encoding, libxml turns off all
     * validation of the incoming bytes, which is just what we want here seeing
     * how borked the encoding of this site is. fortunately, the interesting
     * bits are all reliably (or so it seems) encoded using CP1250, as the
     * Content-Type header suggests. we just handle the conversion of those
     * ourselves and tell libxml to ignore the specified encoding, which is also
     * why we need to do all these htmlParserCtxtPtr shenanigans, as there's no
     * htmlSAXParseFile variant which takes the options argument. */
    reset_parse_state(&my_ctx);
    htmlCtxtReadMemory(ctxt, html, size, NULL, "UTF-8", HTML_PARSE_IGNORE_ENC);
  }

  ctxt->sax = oldsax;
  ctxt->userData = oldctx;

  htmlFreeParserCtxt(ctxt);

  /* site returns these sorted already, but just to be sure... */
  mux_data_sort_transmitters(my_ctx.muxdata);

  return my_ctx.muxdata;
}

struct tuneparams_parser_ctx {
  MuxData *muxdata;
  htmlSAXHandlerPtr sax;

  struct mux_params parse_buf;
  gchar *parse_buf_mux;

  bool in_table;
  int cur_row;
  int cur_column;
};

static void for_each_mux_param(const GArray *transmitters,
                               const struct mux_params *wanted,
                               void (*func)(struct mux_params *, const void *),
                               const void *func_ctx) {
  /* the array is sorted by distance so we need to do a linear search. this
   * shouldn't really be a problem since it shouldn't have more than 10 elements
   * at most. */
  for (guint i = 0; i < transmitters->len; ++i) {
    struct mux_params *const par =
        &g_array_index(transmitters, struct mux_params, i);
    if (par->tune_parms.freq_khz == wanted->tune_parms.freq_khz &&
        strcmp(par->name, wanted->name) == 0) {
      func(par, func_ctx);
    }
  }
}

static void set_new_tune_params(struct mux_params *found_muxparms,
                                const void *ctx) {
  const struct tune_params *const new_tuneparms = ctx;
  found_muxparms->tune_parms = *new_tuneparms;
}

static void add_tuneparams_to_muxdata(MuxData *md,
                                      const struct mux_params *with_tuneparms,
                                      const gchar *mux) {
  /* try to locate the currently parsed transmitter in the hash which is
   * assumed to contain all the previously fetched and parsed location-based
   * transmitters, and fill in its mux_params structure with what we've parsed.
   */
  GArray *const transmitters = mux_data_get_transmitters_for_mux(md, mux);
  if (!transmitters) {
    return;
  }

  for_each_mux_param(transmitters, with_tuneparms, set_new_tune_params,
                     &with_tuneparms->tune_parms);
}

static void tune_params_set_dvb_mod(struct tune_params *tuneparms,
                                    enum fe_delivery_system dvb_type,
                                    enum fe_modulation mod) {
  tuneparms->dvb_type = dvb_type;
  tuneparms->mod = mod;
}

static void reset_parse_buf(struct tuneparams_parser_ctx *ctx) {
  mux_params_clear(&ctx->parse_buf);
  memset(&ctx->parse_buf, 0, sizeof(ctx->parse_buf));
  g_clear_pointer(&ctx->parse_buf_mux, g_free);
  /* tune_params are set to DVB-T/64QAM by default, and overridden in
   * tuneparams_characters() only if the data clearly shows that this is a
   * DVB-T2 transmission */
  tune_params_set_dvb_mod(&ctx->parse_buf.tune_parms, SYS_DVBT, QAM_64);
}

static void tuneparams_characters(void *ctx, const xmlChar *ch, int len) {
  struct tuneparams_parser_ctx *const my_ctx = ctx;
  if (len <= 0)
    return;

  const gsize siz = (gsize)len;
  const char *const chch = (const char *)ch;
  struct tune_params *const tune_parms = &my_ctx->parse_buf.tune_parms;
  switch (my_ctx->cur_column) {
  case 1:
    tune_parms->freq_khz = (guint)(g_ascii_strtod(chch, NULL) * 1000.0);
    break;
  case 2: {
    /* all MUX-8 transmissions use 7MHz channels, the rest uses 8MHz. this is
     * not the best way of determining this but getting a definite answer would
     * require querying the site for each located transmitter, which doesn't
     * sound too nice for the webadmin's PoV. */
    tune_parms->bw_mhz = strcmp(chch, "MUX-8") == 0 ? 7 : 8;

    gchar *const mux_id = g_strndup(chch, siz);
    if (sanitize_mux_id(mux_id, siz)) {
      tune_params_set_dvb_mod(tune_parms, SYS_DVBT2, QAM_256);
    }
    my_ctx->parse_buf_mux = mux_id;
    break;
  }
  case 3:
    my_ctx->parse_buf.name = g_strndup(chch, siz);
    break;
  case 6:
    if (strstr(chch, "DVB-T2")) {
      tune_params_set_dvb_mod(tune_parms, SYS_DVBT2, QAM_256);
    }
    break;
  default:
    break;
  }
}

static void tuneparams_start_element(void *ctx, const xmlChar *name,
                                     const xmlChar **atts) {
  struct tuneparams_parser_ctx *const my_ctx = ctx;
  if (strcmp((const char *)name, "table") == 0 &&
      is_attr(atts, "border", "1") && is_attr(atts, "class", "tabelka")) {
    my_ctx->in_table = true;
    my_ctx->cur_row = 0;
  }

  if (my_ctx->in_table) {
    if (strcmp((const char *)name, "tr") == 0) {
      my_ctx->cur_column = 0;
      if (my_ctx->cur_row >= 1)
        my_ctx->sax->characters = tuneparams_characters;
    }
  }
}

static void tuneparams_end_element(void *ctx, const xmlChar *name) {
  struct tuneparams_parser_ctx *const my_ctx = ctx;
  if (strcmp((const char *)name, "table") == 0 && my_ctx->in_table) {
    my_ctx->in_table = false;
  }
  if (my_ctx->in_table) {
    if (strcmp((const char *)name, "td") == 0) {
      my_ctx->cur_column++;
    } else if (strcmp((const char *)name, "tr") == 0) {
      if (my_ctx->cur_row >= 1 && my_ctx->cur_column == 7 &&
          my_ctx->parse_buf_mux) {
        add_tuneparams_to_muxdata(my_ctx->muxdata, &my_ctx->parse_buf,
                                  my_ctx->parse_buf_mux);
      }
      reset_parse_buf(my_ctx);
      my_ctx->cur_row++;
      my_ctx->cur_column = -1;
    }
  }
}

void parse_tune_params_to_mux_params(MuxData *muxdata, const char *html,
                                     int size) {
  htmlSAXHandler sax;
  memset(&sax, 0, sizeof(sax));
  sax.startElement = tuneparams_start_element;
  sax.endElement = tuneparams_end_element;

  struct tuneparams_parser_ctx my_ctx;
  memset(&my_ctx, 0, sizeof(my_ctx));
  reset_parse_buf(&my_ctx);
  my_ctx.sax = &sax;
  my_ctx.muxdata = muxdata;

  htmlParserCtxtPtr ctxt = htmlNewParserCtxt();
  htmlSAXHandlerPtr oldsax = ctxt->sax;
  void *oldctx = ctxt->userData;
  ctxt->sax = &sax;
  ctxt->userData = &my_ctx;

  htmlCtxtReadMemory(ctxt, html, size, NULL, "CP1250", 0);

  ctxt->sax = oldsax;
  ctxt->userData = oldctx;
  htmlFreeParserCtxt(ctxt);
}
