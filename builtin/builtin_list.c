/*
 * apps/builtin/builtin_list.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The builtin application table.  The table entries are appended to
 * builtin_list.h by the registry step (one .bdat entry per registered
 * application); the prototypes come from builtin_proto.h (one .pdat
 * entry per application).  This stub only wires the two generated
 * headers into the g_builtins table that exec_builtin() walks.
 */

#include <nuttx/config.h>

#include <stddef.h>

#include <nuttx/lib/builtin.h>

#include "builtin_proto.h"

const struct builtin_s g_builtins[] =
{
#include "builtin_list.h"
  { NULL, 0, 0, NULL }
};

const int g_builtin_count =
  sizeof(g_builtins) / sizeof(g_builtins[0]);
