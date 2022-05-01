#include <glib.h>
#include <stdio.h>
#include <locale.h>

#include "../deser.h"
#include "../parser.h"

static void to_stdout(const guint8 *data, gssize len, void *ctx) {
  (void)ctx;
  const size_t real_len = len < 0 ? strlen((const char *)data) : (size_t)len;
  fwrite(data, real_len, 1, stdout);
}

static gboolean read_whole_file(const char *name, gchar **contents,
                                gsize *siz) {
  GError *error = 0;
  if (g_file_get_contents(name, contents, siz, &error)) {
    return TRUE;
  } else {
    g_printerr("Failed to read %s : %s\n", name, error->message);
    g_error_free(error);
    return FALSE;
  }
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  if (argc != 3) {
    g_printerr("Usage : %s nadajniki.php dvb-t.php\n", argv[0]);
    return 1;
  }

  gchar *content;
  gsize len;
  if (!read_whole_file(argv[1], &content, &len)) {
    return 1;
  }

  MuxData *const parsed = parse_mux_params_from_html(content, (int)len);
  g_free(content);

  if (!read_whole_file(argv[2], &content, &len)) {
    return 1;
  }

  parse_tune_params_to_mux_params(parsed, content, len);
  g_free(content);

  serialize_muxdata_hash(parsed, to_stdout, 0);
  fputc('\n', stdout);

  mux_data_destroy(parsed);
  return 0;
}
