/*
 * Copyright © 2010  Behdad Esfahbod
 * Copyright © 2011,2012  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Garret Rieger, Rod Sheeter
 */

#include <stdio.h>

#include "output-options.hh"
#include "face-options.hh"
#include "batch.hh"
#include "main-font-text.hh"

#include <hb-subset.h>

/*
 * Command line interface to the harfbuzz font subsetter.
 */

struct subset_main_t : option_parser_t, face_options_t, output_options_t<false>
{
  subset_main_t ()
  : input (hb_subset_input_create_or_fail ())
  {}
  ~subset_main_t ()
  {
    hb_subset_input_destroy (input);
  }

  int operator () (int argc, char **argv)
  {
    add_options ();
    parse (&argc, &argv);

    hb_face_t *new_face = nullptr;
    for (unsigned i = 0; i < num_iterations; i++)
    {
      hb_face_destroy (new_face);
      new_face = hb_subset_or_fail (face, input);
    }

    bool success = new_face;
    if (success)
    {
      hb_blob_t *result = hb_face_reference_blob (new_face);
      write_file (output_file, result);
      hb_blob_destroy (result);
    }

    hb_face_destroy (new_face);

    return success ? 0 : 1;
  }

  bool
  write_file (const char *output_file, hb_blob_t *blob)
  {
    assert (out_fp);

    unsigned int size;
    const char* data = hb_blob_get_data (blob, &size);

    while (size)
    {
      size_t ret = fwrite (data, 1, size, out_fp);
      size -= ret;
      data += ret;
      if (size && ferror (out_fp))
        fail (false, "Failed to write output: %s", strerror (errno));
    }

    return true;
  }

  void add_options ();

  public:
  void post_parse (GError **error);

  protected:
  static gboolean
  collect_rest (const char *name,
		const char *arg,
		gpointer    data,
		GError    **error);

  public:

  unsigned num_iterations = 1;
  hb_subset_input_t *input = nullptr;

  /* Internal, ouch. */
  bool all_unicodes = false;
  GString *glyph_names = nullptr;
};

static gboolean
parse_gids (const char *name G_GNUC_UNUSED,
	    const char *arg,
	    gpointer    data,
	    GError    **error)
{
  subset_main_t *subset_main = (subset_main_t *) data;
  hb_set_t *gids = hb_subset_input_glyph_set (subset_main->input);

  char *s = (char *) arg;
  char *p;

  while (s && *s)
  {
    while (*s && strchr (", ", *s))
      s++;
    if (!*s)
      break;

    errno = 0;
    hb_codepoint_t start_code = strtoul (s, &p, 10);
    if (s[0] == '-' || errno || s == p)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		   "Failed parsing glyph-index at: '%s'", s);
      return false;
    }

    if (p && p[0] == '-') // ranges
    {
      s = ++p;
      hb_codepoint_t end_code = strtoul (s, &p, 10);
      if (s[0] == '-' || errno || s == p)
      {
	g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		     "Failed parsing glyph-index at: '%s'", s);
	return false;
      }

      if (end_code < start_code)
      {
	g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		     "Invalid gid-index range %u-%u", start_code, end_code);
	return false;
      }
      hb_set_add_range (gids, start_code, end_code);
    }
    else
    {
      hb_set_add (gids, start_code);
    }
    s = p;
  }

  return true;
}

static gboolean
parse_glyphs (const char *name G_GNUC_UNUSED,
	      const char *arg,
	      gpointer    data,
	      GError    **error G_GNUC_UNUSED)
{
  subset_main_t *subset_main = (subset_main_t *) data;

  if (!subset_main->glyph_names)
    subset_main->glyph_names = g_string_new (nullptr);
  else
    g_string_append_c (subset_main->glyph_names, ' ');

  g_string_append (subset_main->glyph_names, arg);

  return true;
}

static gboolean
parse_text (const char *name G_GNUC_UNUSED,
	    const char *arg,
	    gpointer    data,
	    GError    **error G_GNUC_UNUSED)
{
  subset_main_t *subset_main = (subset_main_t *) data;

  if (0 == strcmp (arg, "*"))
  {
    subset_main->all_unicodes = true;
    return true;
  }

  hb_set_t *codepoints = hb_subset_input_unicode_set (subset_main->input);
  for (gchar *c = (gchar *) arg;
       *c;
       c = g_utf8_find_next_char(c, nullptr))
  {
    gunichar cp = g_utf8_get_char(c);
    hb_set_add (codepoints, cp);
  }
  return true;
}

static gboolean
parse_unicodes (const char *name G_GNUC_UNUSED,
		const char *arg,
		gpointer    data,
		GError    **error)
{
  subset_main_t *subset_main = (subset_main_t *) data;

  if (0 == strcmp (arg, "*"))
  {
    subset_main->all_unicodes = true;
    return true;
  }

  // XXX TODO Ranges
  hb_set_t *codepoints = hb_subset_input_unicode_set (subset_main->input);
  {
    char *s = (char *) arg;
    char *p;

    while (s && *s)
    {
#define DELIMITERS "<+>{},;&#\\xXuUnNiI\n\t\v\f\r "

      while (*s && strchr (DELIMITERS, *s))
	s++;
      if (!*s)
	break;

      errno = 0;
      hb_codepoint_t u = strtoul (s, &p, 16);
      if (errno || s == p)
      {
	g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		     "Failed parsing Unicode value at: '%s'", s);
	return false;
      }
      hb_set_add (codepoints, u);

      s = p;
    }
  }
  return true;
}

static gboolean
parse_nameids (const char *name,
	       const char *arg,
	       gpointer    data,
	       GError    **error)
{
  subset_main_t *subset_main = (subset_main_t *) data;
  hb_set_t *name_ids = hb_subset_input_nameid_set (subset_main->input);

  char last_name_char = name[strlen (name) - 1];

  if (last_name_char != '+' && last_name_char != '-')
    hb_set_clear (name_ids);

  if (0 == strcmp (arg, "*"))
  {
    if (last_name_char == '-')
      hb_set_del_range (name_ids, 0, 0x7FFF);
    else
      hb_set_add_range (name_ids, 0, 0x7FFF);
    return true;
  }

  char *s = (char *) arg;
  char *p;

  while (s && *s)
  {
    while (*s && strchr (", ", *s))
      s++;
    if (!*s)
      break;

    errno = 0;
    hb_codepoint_t u = strtoul (s, &p, 10);
    if (errno || s == p)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		   "Failed parsing nameID value at: '%s'", s);
      return false;
    }

    if (last_name_char != '-')
    {
      hb_set_add (name_ids, u);
    } else {
      hb_set_del (name_ids, u);
    }

    s = p;
  }

  return true;
}

static gboolean
parse_name_languages (const char *name,
		      const char *arg,
		      gpointer    data,
		      GError    **error)
{
  subset_main_t *subset_main = (subset_main_t *) data;
  hb_set_t *name_languages = hb_subset_input_namelangid_set (subset_main->input);

  char last_name_char = name[strlen (name) - 1];

  if (last_name_char != '+' && last_name_char != '-')
    hb_set_clear (name_languages);

  if (0 == strcmp (arg, "*"))
  {
    if (last_name_char == '-')
      hb_set_del_range (name_languages, 0, 0x5FFF);
    else
      hb_set_add_range (name_languages, 0, 0x5FFF);
    return true;
  }

  char *s = (char *) arg;
  char *p;

  while (s && *s)
  {
    while (*s && strchr (", ", *s))
      s++;
    if (!*s)
      break;

    errno = 0;
    hb_codepoint_t u = strtoul (s, &p, 10);
    if (errno || s == p)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		   "Failed parsing name-language code at: '%s'", s);
      return false;
    }

    if (last_name_char != '-')
    {
      hb_set_add (name_languages, u);
    } else {
      hb_set_del (name_languages, u);
    }

    s = p;
  }

  return true;
}

template <hb_subset_flags_t flag>
static gboolean
set_flag (const char *name,
	  const char *arg,
	  gpointer    data,
	  GError    **error G_GNUC_UNUSED)
{
  subset_main_t *subset_main = (subset_main_t *) data;

  hb_subset_input_set_flags (subset_main->input,
			     hb_subset_input_get_flags (subset_main->input) | flag);

  return true;
}

static gboolean
parse_layout_features (const char *name,
		       const char *arg,
		       gpointer    data,
		       GError    **error G_GNUC_UNUSED)
{
  subset_main_t *subset_main = (subset_main_t *) data;
  hb_set_t *layout_features = hb_subset_input_layout_features_set (subset_main->input);

  char last_name_char = name[strlen (name) - 1];

  if (last_name_char != '+' && last_name_char != '-')
    hb_set_clear (layout_features);

  if (0 == strcmp (arg, "*"))
  {
    if (last_name_char == '-')
    {
      hb_set_clear (layout_features);
      hb_subset_input_set_flags (subset_main->input,
				 hb_subset_input_get_flags (subset_main->input) & ~HB_SUBSET_FLAGS_RETAIN_ALL_FEATURES);
    } else {
      hb_subset_input_set_flags (subset_main->input,
				 hb_subset_input_get_flags (subset_main->input) | HB_SUBSET_FLAGS_RETAIN_ALL_FEATURES);
    }
    return true;
  }

  char *s = strtok((char *) arg, ", ");
  while (s)
  {
    if (strlen (s) > 4) // table tags are at most 4 bytes
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Failed parsing table tag value at: '%s'", s);
      return false;
    }

    hb_tag_t tag = hb_tag_from_string (s, strlen (s));

    if (last_name_char != '-')
      hb_set_add (layout_features, tag);
    else
      hb_set_del (layout_features, tag);

    s = strtok(nullptr, ", ");
  }

  return true;
}

static gboolean
parse_drop_tables (const char *name,
		   const char *arg,
		   gpointer    data,
		   GError    **error)
{
  subset_main_t *subset_main = (subset_main_t *) data;
  hb_set_t *drop_tables = hb_subset_input_drop_tables_set (subset_main->input);

  char last_name_char = name[strlen (name) - 1];

  if (last_name_char != '+' && last_name_char != '-')
    hb_set_clear (drop_tables);

  char *s = strtok((char *) arg, ", ");
  while (s)
  {
    if (strlen (s) > 4) // Table tags are at most 4 bytes.
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		   "Failed parsing table tag value at: '%s'", s);
      return false;
    }

    hb_tag_t tag = hb_tag_from_string (s, strlen (s));

    if (last_name_char != '-')
      hb_set_add (drop_tables, tag);
    else
      hb_set_del (drop_tables, tag);

    s = strtok(nullptr, ", ");
  }

  return true;
}

template <GOptionArgFunc line_parser, bool allow_comments=true>
static gboolean
parse_file_for (const char *name,
		const char *arg,
		gpointer    data,
		GError    **error)
{
  FILE *fp = nullptr;
  if (0 != strcmp (arg, "-"))
    fp = fopen (arg, "r");
  else
    fp = stdin;

  if (!fp)
  {
    g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
		 "Failed opening file `%s': %s",
		 arg, strerror (errno));
    return false;
  }

  GString *gs = g_string_new (nullptr);
  do
  {
    g_string_set_size (gs, 0);
    char buf[BUFSIZ];
    while (fgets (buf, sizeof (buf), fp))
    {
      unsigned bytes = strlen (buf);
      if (bytes && buf[bytes - 1] == '\n')
      {
	bytes--;
	g_string_append_len (gs, buf, bytes);
	break;
      }
      g_string_append_len (gs, buf, bytes);
    }
    if (ferror (fp))
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
		   "Failed reading file `%s': %s",
		   arg, strerror (errno));
      return false;
    }
    g_string_append_c (gs, '\0');

    if (allow_comments)
    {
      char *comment = strchr (gs->str, '#');
      if (comment)
        *comment = '\0';
    }

    line_parser (name, gs->str, data, error);

    if (*error)
      break;
  }
  while (!feof (fp));

  g_string_free (gs, false);

  return true;
}

gboolean
subset_main_t::collect_rest (const char *name,
			     const char *arg,
			     gpointer    data,
			     GError    **error)
{
  subset_main_t *thiz = (subset_main_t *) data;

  if (!thiz->font_file)
  {
    thiz->font_file = g_strdup (arg);
    return true;
  }

  parse_text (name, arg, data, error);
  return true;
}

void
subset_main_t::add_options ()
{
  face_options_t::add_options (this);

  GOptionEntry glyphset_entries[] =
  {
    {"gids",		0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_gids,  "Specify glyph IDs or ranges to include in the subset", "list of glyph indices/ranges"},
    {"gids-file",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_file_for<parse_gids>,  "Specify file to read glyph IDs or ranges from", "filename"},
    {"glyphs",		0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_glyphs,  "Specify glyph names to include in the subset", "list of glyph names"},
    {"glyphs-file",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_file_for<parse_glyphs>,  "Specify file to read glyph names fromt", "filename"},
    {"text",		0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_text,  "Specify text to include in the subset", "string"},
    {"text-file",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_file_for<parse_text, false>,  "Specify file to read text from", "filename"},
    {"unicodes",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_unicodes,  "Specify Unicode codepoints or ranges to include in the subset", "list of hex numbers/ranges"},
    {"unicodes-file",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_file_for<parse_unicodes>,  "Specify file to read Unicode codepoints or ranges from", "filename"},
    {nullptr}
  };
  add_group (glyphset_entries,
	     "subset-glyphset",
	     "Subset glyph-set option:",
	     "Subsetting glyph-set options",
	     this);

  GOptionEntry other_entries[] =
  {
    {"name-IDs",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_nameids,  "Subset specified nameids", "list of int numbers"},
    {"name-IDs-",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_nameids,  "Subset specified nameids", "list of int numbers"},
    {"name-IDs+",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_nameids,  "Subset specified nameids", "list of int numbers"},
    {"name-languages",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_name_languages,  "Subset nameRecords with specified language IDs", "list of int numbers"},
    {"name-languages-",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_name_languages,  "Subset nameRecords with specified language IDs", "list of int numbers"},
    {"name-languages+",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_name_languages,  "Subset nameRecords with specified language IDs", "list of int numbers"},
    {"layout-features",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_layout_features,  "Specify set of layout feature tags that will be preserved", "list of string table tags."},
    {"layout-features+",0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_layout_features,  "Specify set of layout feature tags that will be preserved", "list of string table tags."},
    {"layout-features-",0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_layout_features,  "Specify set of layout feature tags that will be preserved", "list of string table tags."},
    {"drop-tables",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_drop_tables,  "Drop the specified tables.", "list of string table tags."},
    {"drop-tables+",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_drop_tables,  "Drop the specified tables.", "list of string table tags."},
    {"drop-tables-",	0, 0, G_OPTION_ARG_CALLBACK, (gpointer) &parse_drop_tables,  "Drop the specified tables.", "list of string table tags."},
    {nullptr}
  };
  add_group (other_entries,
	     "subset-other",
	     "Subset other option:",
	     "Subsetting other options",
	     this);

  GOptionEntry flag_entries[] =
  {
    {"no-hinting",	0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_NO_HINTING>,   "Whether to drop hints",   nullptr},
    {"retain-gids",	0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_RETAIN_GIDS>,   "If set don't renumber glyph ids in the subset.",   nullptr},
    {"desubroutinize",	0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_DESUBROUTINIZE>,   "Remove CFF/CFF2 use of subroutines",   nullptr},
    {"name-legacy", 0,	G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_NAME_LEGACY>,   "Keep legacy (non-Unicode) 'name' table entries",   nullptr},
    {"set-overlaps-flag",	0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_SET_OVERLAPS_FLAG>,
     "Set the overlaps flag on each glyph.",   nullptr},
    {"notdef-outline", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_NOTDEF_OUTLINE>,   "Keep the outline of \'.notdef\' glyph",   nullptr},
    {"no-prune-unicode-ranges",	0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_NO_PRUNE_UNICODE_RANGES>,   "Don't change the 'OS/2 ulUnicodeRange*' bits.",   nullptr},
    {"glyph-names", 0,	G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer) &set_flag<HB_SUBSET_FLAGS_GLYPH_NAMES>,   "Keep PS glyph names in TT-flavored fonts. ",   nullptr},
    {nullptr}
  };
  add_group (flag_entries,
	     "subset-flags",
	     "Subset boolean option:",
	     "Subsetting boolean options",
	     this);

  GOptionEntry app_entries[] =
  {
    {"num-iterations",	'n', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
     &this->num_iterations,
     "Run subsetter N times (default: 1)", "N"},
    {nullptr}
  };
  add_group (app_entries,
	     "subset-app",
	     "Subset app option:",
	     "Subsetting application options",
	     this);

  output_options_t::add_options (this);

  GOptionEntry entries[] =
  {
    {G_OPTION_REMAINING,	0, G_OPTION_FLAG_IN_MAIN,
			      G_OPTION_ARG_CALLBACK,	(gpointer) &collect_rest,	nullptr,	"[FONT-FILE] [TEXT]"},
    {nullptr}
  };
  add_main_group (entries, this);
  option_parser_t::add_options ();
}

void
subset_main_t::post_parse (GError **error)
{
  /* This WILL get called multiple times. Oh well... */

  if (all_unicodes)
  {
    hb_set_t *codepoints = hb_subset_input_unicode_set (input);
    hb_face_collect_unicodes (face, codepoints);
    all_unicodes = false;
  }

  if (glyph_names)
  {
    char *p = glyph_names->str;
    char *p_end = p + glyph_names->len;

    hb_set_t *gids = hb_subset_input_glyph_set (input);

    hb_font_t *font = hb_font_create (face);
    while (p < p_end)
    {
      while (p < p_end && (*p == ' ' || *p == ','))
	p++;

      char *end = p;
      while (end < p_end && *end != ' ' && *end != ',')
	end++;
      *end = '\0';

      if (p < end)
      {
        hb_codepoint_t gid;
	if (!hb_font_get_glyph_from_name (font, p, -1, &gid))
	{
	  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		       "Failed parsing glyph name: '%s'", p);
	  return;
	}

	hb_set_add (gids, gid);
      }

      p = end + 1;
    }
    hb_font_destroy (font);

    g_string_free (glyph_names, false);
    glyph_names = nullptr;
  }
}

int
main (int argc, char **argv)
{
  return batch_main<subset_main_t, true> (argc, argv);
}
