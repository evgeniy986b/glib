/*
 * Copyright © 2011 Canonical Limited
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "config.h"

#include "glib-init.h"
#include "glib-private.h"
#include "gmacros.h"
#include "gtypes.h"
#include "gutils.h"     /* for GDebugKey */
#include "gconstructor.h"
#include "gconstructorprivate.h"
#include "gmem.h"       /* for g_mem_gc_friendly */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Deliberately not checking HAVE_STDINT_H here: we officially require a
 * C99 toolchain, which implies <stdint.h>, int8_t and so on. If your
 * toolchain does not have this, now would be a good time to upgrade. */
#include <stdint.h>

/* This seems as good a place as any to make static assertions about platform
 * assumptions we make throughout GLib. */

/* Test that private macro G_SIGNEDNESS_OF() works as intended */
G_STATIC_ASSERT (G_SIGNEDNESS_OF (int) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (unsigned int) == 0);

/* We do not support 36-bit bytes or other historical curiosities. */
G_STATIC_ASSERT (CHAR_BIT == 8);

/* We assume that data pointers are the same size as function pointers... */
G_STATIC_ASSERT (sizeof (gpointer) == sizeof (GFunc));
G_STATIC_ASSERT (G_ALIGNOF (gpointer) == G_ALIGNOF (GFunc));
/* ... and that all function pointers are the same size. */
G_STATIC_ASSERT (sizeof (GFunc) == sizeof (GCompareDataFunc));
G_STATIC_ASSERT (G_ALIGNOF (GFunc) == G_ALIGNOF (GCompareDataFunc));

/* We assume that "small" enums (those where all values fit in INT32_MIN
 * to INT32_MAX) are exactly int-sized. In particular, we assume that if
 * an enum has no members that exceed the range of char/short, the
 * compiler will make it int-sized anyway, so adding a member later that
 * *does* exceed the range of char/short is not an ABI break. */
typedef enum {
    TEST_CHAR_0 = 0
} TestChar;
typedef enum {
    TEST_SHORT_0 = 0,
    TEST_SHORT_256 = 256
} TestShort;
typedef enum {
    TEST_INT32_MIN = G_MININT32,
    TEST_INT32_MAX = G_MAXINT32
} TestInt;
G_STATIC_ASSERT (sizeof (TestChar) == sizeof (int));
G_STATIC_ASSERT (sizeof (TestShort) == sizeof (int));
G_STATIC_ASSERT (sizeof (TestInt) == sizeof (int));
G_STATIC_ASSERT (G_ALIGNOF (TestChar) == G_ALIGNOF (int));
G_STATIC_ASSERT (G_ALIGNOF (TestShort) == G_ALIGNOF (int));
G_STATIC_ASSERT (G_ALIGNOF (TestInt) == G_ALIGNOF (int));

G_STATIC_ASSERT (sizeof (gchar) == 1);
G_STATIC_ASSERT (sizeof (guchar) == 1);

/* It is platform-dependent whether gchar is signed or unsigned, so there
 * is no assertion here for it */
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guchar) == 0);

G_STATIC_ASSERT (sizeof (gint8) * CHAR_BIT == 8);
G_STATIC_ASSERT (sizeof (guint8) * CHAR_BIT == 8);
G_STATIC_ASSERT (sizeof (gint16) * CHAR_BIT == 16);
G_STATIC_ASSERT (sizeof (guint16) * CHAR_BIT == 16);
G_STATIC_ASSERT (sizeof (gint32) * CHAR_BIT == 32);
G_STATIC_ASSERT (sizeof (guint32) * CHAR_BIT == 32);
G_STATIC_ASSERT (sizeof (gint64) * CHAR_BIT == 64);
G_STATIC_ASSERT (sizeof (guint64) * CHAR_BIT == 64);

G_STATIC_ASSERT (sizeof (void *) == GLIB_SIZEOF_VOID_P);
G_STATIC_ASSERT (sizeof (gintptr) == sizeof (void *));
G_STATIC_ASSERT (sizeof (guintptr) == sizeof (void *));

G_STATIC_ASSERT (sizeof (short) == sizeof (gshort));
G_STATIC_ASSERT (G_MINSHORT == SHRT_MIN);
G_STATIC_ASSERT (G_MAXSHORT == SHRT_MAX);
G_STATIC_ASSERT (sizeof (unsigned short) == sizeof (gushort));
G_STATIC_ASSERT (G_MAXUSHORT == USHRT_MAX);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gshort) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gushort) == 0);

G_STATIC_ASSERT (sizeof (int) == sizeof (gint));
G_STATIC_ASSERT (G_MININT == INT_MIN);
G_STATIC_ASSERT (G_MAXINT == INT_MAX);
G_STATIC_ASSERT (sizeof (unsigned int) == sizeof (guint));
G_STATIC_ASSERT (G_MAXUINT == UINT_MAX);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gint) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guint) == 0);

G_STATIC_ASSERT (sizeof (long) == GLIB_SIZEOF_LONG);
G_STATIC_ASSERT (sizeof (long) == sizeof (glong));
G_STATIC_ASSERT (G_MINLONG == LONG_MIN);
G_STATIC_ASSERT (G_MAXLONG == LONG_MAX);
G_STATIC_ASSERT (sizeof (unsigned long) == sizeof (gulong));
G_STATIC_ASSERT (G_MAXULONG == ULONG_MAX);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (glong) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gulong) == 0);

G_STATIC_ASSERT (G_HAVE_GINT64 == 1);

G_STATIC_ASSERT (sizeof (size_t) == GLIB_SIZEOF_SIZE_T);
/* Not a typo: ssize_t is POSIX, not Standard C, but if it exists then
 * it's the same size as size_t. */
G_STATIC_ASSERT (sizeof (size_t) == GLIB_SIZEOF_SSIZE_T);
G_STATIC_ASSERT (sizeof (gsize) == GLIB_SIZEOF_SSIZE_T);
G_STATIC_ASSERT (sizeof (gsize) == sizeof (size_t));
G_STATIC_ASSERT (G_MAXSIZE == SIZE_MAX);
/* Again this is size_t not ssize_t, because ssize_t is POSIX, not C99 */
G_STATIC_ASSERT (sizeof (gssize) == sizeof (size_t));
G_STATIC_ASSERT (G_ALIGNOF (gsize) == G_ALIGNOF (size_t));
G_STATIC_ASSERT (G_ALIGNOF (gssize) == G_ALIGNOF (size_t));
/* We assume that GSIZE_TO_POINTER is reversible by GPOINTER_TO_SIZE
 * without losing information.
 * However, we do not assume that GPOINTER_TO_SIZE can store an arbitrary
 * pointer in a gsize (known to be false on CHERI). */
G_STATIC_ASSERT (sizeof (size_t) <= sizeof (void *));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (size_t) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gsize) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gssize) == 1);

/* Standard C does not guarantee that size_t is the same as uintptr_t,
 * but GLib currently assumes they are the same: see
 * <https://gitlab.gnome.org/GNOME/glib/-/issues/2842>.
 *
 * To enable working on bringup for new architectures these assertions
 * can be disabled with -DG_ENABLE_EXPERIMENTAL_ABI_COMPILATION.
 *
 * FIXME: remove these assertions once the API/ABI has stabilized. */
#ifndef G_ENABLE_EXPERIMENTAL_ABI_COMPILATION
G_STATIC_ASSERT (sizeof (size_t) == sizeof (uintptr_t));
G_STATIC_ASSERT (G_ALIGNOF (size_t) == G_ALIGNOF (uintptr_t));
#endif

/* goffset is always 64-bit, even if off_t is only 32-bit
 * (compiling without large-file-support on 32-bit) */
G_STATIC_ASSERT (sizeof (goffset) == sizeof (gint64));
G_STATIC_ASSERT (G_ALIGNOF (goffset) == G_ALIGNOF (gint64));
/* goffset is always signed */
G_STATIC_ASSERT (G_SIGNEDNESS_OF (goffset) == 1);

G_STATIC_ASSERT (sizeof (gfloat) == sizeof (float));
G_STATIC_ASSERT (G_ALIGNOF (gfloat) == G_ALIGNOF (float));
G_STATIC_ASSERT (sizeof (gdouble) == sizeof (double));
G_STATIC_ASSERT (G_ALIGNOF (gdouble) == G_ALIGNOF (double));

G_STATIC_ASSERT (sizeof (gintptr) == sizeof (intptr_t));
G_STATIC_ASSERT (sizeof (guintptr) == sizeof (uintptr_t));
G_STATIC_ASSERT (G_ALIGNOF (gintptr) == G_ALIGNOF (intptr_t));
G_STATIC_ASSERT (G_ALIGNOF (guintptr) == G_ALIGNOF (uintptr_t));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gintptr) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guintptr) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (intptr_t) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (uintptr_t) == 0);

G_STATIC_ASSERT (sizeof (gint8) == sizeof (int8_t));
G_STATIC_ASSERT (sizeof (guint8) == sizeof (uint8_t));
G_STATIC_ASSERT (G_ALIGNOF (gint8) == G_ALIGNOF (int8_t));
G_STATIC_ASSERT (G_ALIGNOF (guint8) == G_ALIGNOF (uint8_t));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gint8) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guint8) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (int8_t) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (uint8_t) == 0);

G_STATIC_ASSERT (sizeof (gint16) == sizeof (int16_t));
G_STATIC_ASSERT (sizeof (guint16) == sizeof (uint16_t));
G_STATIC_ASSERT (G_ALIGNOF (gint16) == G_ALIGNOF (int16_t));
G_STATIC_ASSERT (G_ALIGNOF (guint16) == G_ALIGNOF (uint16_t));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (int16_t) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (uint16_t) == 0);

G_STATIC_ASSERT (sizeof (gint32) == sizeof (int32_t));
G_STATIC_ASSERT (sizeof (guint32) == sizeof (uint32_t));
G_STATIC_ASSERT (G_ALIGNOF (gint32) == G_ALIGNOF (int32_t));
G_STATIC_ASSERT (G_ALIGNOF (guint32) == G_ALIGNOF (uint32_t));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gint32) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guint32) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (int32_t) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (uint32_t) == 0);

G_STATIC_ASSERT (sizeof (gint64) == sizeof (int64_t));
G_STATIC_ASSERT (sizeof (guint64) == sizeof (uint64_t));
G_STATIC_ASSERT (G_ALIGNOF (gint64) == G_ALIGNOF (int64_t));
G_STATIC_ASSERT (G_ALIGNOF (guint64) == G_ALIGNOF (uint64_t));
G_STATIC_ASSERT (G_SIGNEDNESS_OF (gint64) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (guint64) == 0);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (int64_t) == 1);
G_STATIC_ASSERT (G_SIGNEDNESS_OF (uint64_t) == 0);

/* C11 §6.7, item 3 allows us to rely on this being allowed */
typedef struct Foo Foo;
typedef struct Foo Foo;

/**
 * g_mem_gc_friendly:
 *
 * This variable is %TRUE if the `G_DEBUG` environment variable
 * includes the key `gc-friendly`.
 */
gboolean g_mem_gc_friendly = FALSE;

GLogLevelFlags g_log_msg_prefix = G_LOG_LEVEL_ERROR | G_LOG_LEVEL_WARNING |
                                  G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_DEBUG;
GLogLevelFlags g_log_always_fatal = G_LOG_FATAL_MASK;

static gboolean
debug_key_matches (const gchar *key,
                   const gchar *token,
                   size_t       length)
{
  /* may not call GLib functions: see note in g_parse_debug_string() */
  for (; length; length--, key++, token++)
    {
      char k = (*key   == '_') ? '-' : tolower (*key  );
      char t = (*token == '_') ? '-' : tolower (*token);

      if (k != t)
        return FALSE;
    }

  return *key == '\0';
}

/* The GVariant documentation indirectly says that int is at least 32 bits
 * (by saying that b, y, n, q, i, u, h are promoted to int). On any
 * reasonable platform, int is in fact *exactly* 32 bits long, because
 * otherwise, {signed char, short, int} wouldn't be sufficient to provide
 * {int8_t, int16_t, int32_t}. */
G_STATIC_ASSERT (sizeof (int) == sizeof (gint32));

/**
 * g_parse_debug_string:
 * @string: (nullable): a list of debug options separated by colons, spaces, or
 * commas, or %NULL.
 * @keys: (array length=nkeys): pointer to an array of #GDebugKey which associate
 *     strings with bit flags.
 * @nkeys: the number of #GDebugKeys in the array.
 *
 * Parses a string containing debugging options
 * into a %guint containing bit flags. This is used
 * within GDK and GTK to parse the debug options passed on the
 * command line or through environment variables.
 *
 * If @string is equal to "all", all flags are set. Any flags
 * specified along with "all" in @string are inverted; thus,
 * "all,foo,bar" or "foo,bar,all" sets all flags except those
 * corresponding to "foo" and "bar".
 *
 * If @string is equal to "help", all the available keys in @keys
 * are printed out to standard error.
 *
 * Returns: the combined set of bit flags.
 */
guint
g_parse_debug_string  (const gchar     *string,
                       const GDebugKey *keys,
                       guint            nkeys)
{
  guint i;
  guint result = 0;

  if (string == NULL)
    return 0;

  /* this function is used during the initialisation of gmessages, gmem
   * and gslice, so it may not do anything that causes memory to be
   * allocated or risks messages being emitted.
   *
   * this means, more or less, that this code may not call anything
   * inside GLib.
   */

  if (!strcasecmp (string, "help"))
    {
      /* using stdio directly for the reason stated above */
      fprintf (stderr, "Supported debug values:");
      for (i = 0; i < nkeys; i++)
        fprintf (stderr, " %s", keys[i].key);
      fprintf (stderr, " all help\n");
    }
  else
    {
      const gchar *p = string;
      const gchar *q;
      gboolean invert = FALSE;

      while (*p)
       {
         q = strpbrk (p, ":;, \t");
         if (!q)
           q = p + strlen (p);

         if (debug_key_matches ("all", p, q - p))
           {
             invert = TRUE;
           }
         else
           {
             for (i = 0; i < nkeys; i++)
               if (debug_key_matches (keys[i].key, p, q - p))
                 result |= keys[i].value;
           }

         p = q;
         if (*p)
           p++;
       }

      if (invert)
        {
          guint all_flags = 0;

          for (i = 0; i < nkeys; i++)
            all_flags |= keys[i].value;

          result = all_flags & (~result);
        }
    }

  return result;
}

static guint
g_parse_debug_envvar (const gchar     *envvar,
                      const GDebugKey *keys,
                      gint             n_keys,
                      guint            default_value)
{
  const gchar *value;

#ifdef OS_WIN32
  /* "fatal-warnings,fatal-criticals,all,help" is pretty short */
  gchar buffer[100];

  if (GetEnvironmentVariable (envvar, buffer, 100) < 100)
    value = buffer;
  else
    return 0;
#else
  value = getenv (envvar);
#endif

  if (value == NULL)
    return default_value;

  return g_parse_debug_string (value, keys, n_keys);
}

static void
g_messages_prefixed_init (void)
{
  const GDebugKey keys[] = {
    { "error", G_LOG_LEVEL_ERROR },
    { "critical", G_LOG_LEVEL_CRITICAL },
    { "warning", G_LOG_LEVEL_WARNING },
    { "message", G_LOG_LEVEL_MESSAGE },
    { "info", G_LOG_LEVEL_INFO },
    { "debug", G_LOG_LEVEL_DEBUG }
  };

  g_log_msg_prefix = g_parse_debug_envvar ("G_MESSAGES_PREFIXED", keys, G_N_ELEMENTS (keys), g_log_msg_prefix);
}

static void
g_debug_init (void)
{
  const GDebugKey keys[] = {
    { "gc-friendly", 1 },
    {"fatal-warnings",  G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL },
    {"fatal-criticals", G_LOG_LEVEL_CRITICAL }
  };
  GLogLevelFlags flags;

  flags = g_parse_debug_envvar ("G_DEBUG", keys, G_N_ELEMENTS (keys), 0);

  g_log_always_fatal |= flags & G_LOG_LEVEL_MASK;

  g_mem_gc_friendly = flags & 1;
}

void
glib_init (void)
{
  static gboolean glib_inited;

  if (glib_inited)
    return;

  glib_inited = TRUE;

  g_messages_prefixed_init ();
  g_debug_init ();
  g_quark_init ();
  g_error_init ();
}

#ifdef G_PLATFORM_WIN32

HMODULE glib_dll = NULL;
void glib_win32_init (void);

void
glib_win32_init (void)
{
  /* May be called more than once in static compilation mode */
  static gboolean win32_already_init = FALSE;
  if (!win32_already_init)
    {
      win32_already_init = TRUE;

      g_crash_handler_win32_init ();
#ifdef THREADS_WIN32
      g_thread_win32_init ();
#endif

      g_clock_win32_init ();
      glib_init ();
      /* must go after glib_init */
      g_console_win32_init ();
    }
}

static void
glib_win32_deinit (gboolean detach_thread)
{
#ifdef THREADS_WIN32
  if (detach_thread)
    g_thread_win32_process_detach ();
#endif
  g_crash_handler_win32_deinit ();
}

#ifndef GLIB_STATIC_COMPILATION

BOOL WINAPI DllMain (HINSTANCE hinstDLL,
                     DWORD     fdwReason,
                     LPVOID    lpvReserved);

BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
         DWORD     fdwReason,
         LPVOID    lpvReserved)
{
  switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
      glib_dll = hinstDLL;
      glib_win32_init ();
      break;

    case DLL_THREAD_DETACH:
#ifdef THREADS_WIN32
      g_thread_win32_thread_detach ();
#endif
      break;

    case DLL_PROCESS_DETACH:
      glib_win32_deinit (lpvReserved == NULL);
      break;

    default:
      /* do nothing */
      ;
    }

  return TRUE;
}

#else

#ifndef G_HAS_CONSTRUCTORS
#error static compilation on Windows requires constructor support
#endif

#ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
#pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(glib_priv_constructor)
#endif

static gboolean tls_callback_invoked;

G_DEFINE_CONSTRUCTOR (glib_priv_constructor)

static void
glib_priv_constructor (void)
{
  glib_win32_init ();

  if (!tls_callback_invoked)
    g_critical ("TLS callback not invoked");
}

#ifndef G_HAS_TLS_CALLBACKS
#error static compilation on Windows requires TLS callbacks support
#endif

G_DEFINE_TLS_CALLBACK (glib_priv_tls_callback)

static void NTAPI
glib_priv_tls_callback (LPVOID hinstance,
                        DWORD  reason,
                        LPVOID reserved)
{
  switch (reason)
    {
    case DLL_PROCESS_ATTACH:
      glib_dll = hinstance;
      tls_callback_invoked = TRUE;
      break;
    case DLL_THREAD_DETACH:
#ifdef THREADS_WIN32
      g_thread_win32_thread_detach ();
#endif
      break;
    case DLL_PROCESS_DETACH:
      glib_win32_deinit (reserved == NULL);
      break;

    default:
      break;
    }
}

#endif /* GLIB_STATIC_COMPILATION */

#elif defined(G_HAS_CONSTRUCTORS) /* && !G_PLATFORM_WIN32 */

#ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
#pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(glib_init_ctor)
#endif
G_DEFINE_CONSTRUCTOR(glib_init_ctor)

static void
glib_init_ctor (void)
{
  glib_init ();
}

#else /* !G_PLATFORM_WIN32 && !G_HAS_CONSTRUCTORS */
# error Your platform/compiler is missing constructor support
#endif /* G_PLATFORM_WIN32 */
