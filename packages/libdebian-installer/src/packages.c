/*
 * packages.c
 *
 * Copyright (C) 2003 Bastian Blank <waldi@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 */

#include <config.h>

#include <debian-installer/packages_internal.h>

#include <debian-installer/log.h>
#include <debian-installer/package_internal.h>
#include <debian-installer/slist_internal.h>
#include <debian-installer/string.h>

#include <ctype.h>
#include <limits.h>

/**
 * Allocate di_packages
 */
di_packages *di_packages_alloc (void)
{
  di_packages *ret;

  ret = di_new0 (di_packages, 1);
  ret->table = di_hash_table_new_full (di_rstring_hash, di_rstring_equal, NULL, internal_di_package_destroy_func);

  return ret;
}

/**
 * Allocate di_packages_allocator
 */
di_packages_allocator *di_packages_allocator_alloc (void)
{
  di_packages_allocator *ret;

  ret = internal_di_packages_allocator_alloc ();
  ret->package_mem_chunk = di_mem_chunk_new (sizeof (di_package), 16384);

  return ret;
}

/**
 * @internal
 * Partially allocate di_packages_allocator
 */
di_packages_allocator *internal_di_packages_allocator_alloc (void)
{
  di_packages_allocator *ret;

  ret = di_new0 (di_packages_allocator, 1);
  ret->package_dependency_mem_chunk = di_mem_chunk_new (sizeof (di_package_dependency), 4096);
  ret->slist_node_mem_chunk = di_mem_chunk_new (sizeof (di_slist_node), 4096);

  return ret;
}

/**
 * Free di_packages
 */
void di_packages_free (di_packages *packages)
{
  if (!packages)
    return;
  di_hash_table_destroy (packages->table);
  di_free (packages);
}

/**
 * Free di_packages_allocator
 */
void di_packages_allocator_free (di_packages_allocator *allocator)
{
  di_mem_chunk_destroy (allocator->package_mem_chunk);
  di_mem_chunk_destroy (allocator->package_dependency_mem_chunk);
  di_mem_chunk_destroy (allocator->slist_node_mem_chunk);
  di_free (allocator);
}

/**
 * append a package.
 *
 * @param packages a di_packages
 */
void di_packages_append_package (di_packages *packages, di_package *package, di_packages_allocator *allocator)
{
  di_package *tmp;

  tmp = di_packages_get_package (packages, package->package, 0);

  if (!tmp)
    di_slist_append_chunk (&packages->list, package, allocator->slist_node_mem_chunk);

  di_hash_table_insert (packages->table, &package->key, package);
}

/**
 * get a named package.
 *
 * @param packages a di_packages
 * @param name the name of the package
 * @param n size of the name or 0
 *
 * @return the package or NULL
 */
di_package *di_packages_get_package (di_packages *packages, const char *name, size_t n)
{
  di_rstring key;
  size_t size;

  if (n)
    size = n;
  else
    size = strlen (name);

  /* i know that is bad, but i know it is not written by the lookup */
  key.string = (char *) name;
  key.size = size;

  return di_hash_table_lookup (packages->table, &key);
}

/**
 * get a named package.
 * creates a new one if non-existant.
 *
 * @param packages a di_packages
 * @param name the name of the package
 * @param n size of the name
 *
 * @return the package
 */
di_package *di_packages_get_package_new (di_packages *packages, di_packages_allocator *allocator, char *name, size_t n)
{
  di_package *ret = di_packages_get_package (packages, name, n);

  if (!ret)
  {
    ret = di_package_alloc (allocator);
    ret->key.string = di_stradup (name, n);
    ret->key.size = n;

    di_hash_table_insert (packages->table, &ret->key, ret);
  }

  return ret;
}

bool di_packages_resolve_dependencies_recurse (di_packages_resolve_dependencies_check *r, di_package *package, di_package *dependend_package)
{
  di_slist_node *node;
  di_package *best_provide;

  /* did we already check this package? */
  if (package->resolver & r->resolver)
    return package->resolver & (r->resolver << 1);

  package->resolver |= r->resolver;
  package->resolver |= (r->resolver << 1);

  switch (package->type)
  {
    case di_package_type_real_package:
      for (node = package->depends.head; node; node = node->next)
      {
        di_package_dependency *d = node->data;

        if (!r->check_real (r, package, d))
          goto error;
      }

      if (dependend_package)
        di_log (DI_LOG_LEVEL_DEBUG, "install %s, dependency from %s", package->package, dependend_package->package);

      if (r->allocator)
        di_slist_append_chunk (&r->install, package, r->allocator->slist_node_mem_chunk);
      else
        package->status_want = di_package_status_want_install;
      break;

    case di_package_type_virtual_package:

      if (dependend_package)
        di_log (DI_LOG_LEVEL_DEBUG, "search for package resolving %s, dependency from %s", package->package, dependend_package->package);

      for (node = package->depends.head; node; node = node->next)
      {
        di_package_dependency *d = node->data;

        if (d->type == di_package_dependency_type_reverse_provides)
          package->resolver &= ~(r->resolver << 2);
      }

      while (1)
      {
        best_provide = NULL;

        for (node = package->depends.head; node; node = node->next)
        {
          di_package_dependency *d = node->data;

          if (d->type == di_package_dependency_type_reverse_provides)
          {
            if (!(package->resolver & (r->resolver << 2)))
              best_provide = r->check_virtual (package, best_provide, d);
          }
        }

        if (best_provide)
        {
          if (di_packages_resolve_dependencies_recurse (r, best_provide, dependend_package))
            break;
          else
            package->resolver |= (r->resolver << 2);
        }
        else
          goto error;
      }

      break;

    case di_package_type_non_existent:
      if (!r->check_non_existant (r, package, NULL))
        goto error;
  }

  return true;

error:
  package->resolver &= ~(r->resolver << 1);
  return false;
}

bool di_packages_resolve_dependencies_check_real (di_packages_resolve_dependencies_check *r, di_package *package, di_package_dependency *d)
{
  /* it's a dependency */
  if (d->type == di_package_dependency_type_depends || d->type == di_package_dependency_type_pre_depends)
    return di_packages_resolve_dependencies_recurse (r, d->ptr, package);
#if 0
  else if (d->type == di_package_dependency_type_conflicts)
    switch (d->ptr->type)
    {
      di_slist_node *node;

      case di_package_type_real_package:
        if (d->ptr->status == di_package_status_unpacked ||
            d->ptr->status == di_package_status_installed)
          return false;
      case di_package_type_virtual_package:
        for (node = d->ptr->depends.head; node; node = node->next)
        {
          di_package_dependency *d = node->data;

          if (d->type == di_package_dependency_type_reverse_provides)
            if ((d->ptr->status == di_package_status_unpacked ||
                 d->ptr->status == di_package_status_installed) &&
                d->ptr != package)
              return false;
        }
        break;
      default:
        return false;
    }
#endif
  return true;
}

#if 1
bool di_packages_resolve_dependencies_check_real_dependency (di_packages_resolve_dependencies_check *r, di_package *package, di_package_dependency *d) __attribute__ ((alias("di_packages_resolve_dependencies_check_real")));
#else
bool di_packages_resolve_dependencies_check_real_dependency (di_packages_resolve_dependencies_check *r, di_package *package, di_package_dependency *d)
{
  if (d->type == di_package_dependency_type_depends || d->type == di_package_dependency_type_pre_depends)
    return di_packages_resolve_dependencies_recurse (r, d->ptr, package);
  return true;
}
#endif

di_package *di_packages_resolve_dependencies_check_virtual (di_package *package __attribute__ ((unused)), di_package *best, di_package_dependency *d)
{
  if (!best || best->priority < d->ptr->priority ||
      (d->ptr->status == di_package_status_installed && best->status != di_package_status_installed))
    return d->ptr;
  return best;
}

bool di_packages_resolve_dependencies_check_non_existant (di_packages_resolve_dependencies_check *r __attribute__ ((unused)), di_package *package, di_package_dependency *d __attribute__ ((unused)))
{
  di_log (DI_LOG_LEVEL_WARNING, "package %s doesn't exist", package->package);
#if 1
  /* Backward compatiblity */
  return true;
#else
  return false;
#endif
}

bool di_packages_resolve_dependencies_check_non_existant_quiet (di_packages_resolve_dependencies_check *r __attribute__ ((unused)), di_package *package __attribute__ ((unused)), di_package_dependency *d __attribute__ ((unused)))
{
  di_log (DI_LOG_LEVEL_DEBUG, "package %s doesn't exist", package->package);
#if 1
  /* Backward compatiblity */
  return true;
#else
  return false;
#endif
}

bool di_packages_resolve_dependencies_check_non_existant_permissive (di_packages_resolve_dependencies_check *r __attribute__ ((unused)), di_package *package __attribute__ ((unused)), di_package_dependency *d __attribute__ ((unused)))
{
  di_log (DI_LOG_LEVEL_DEBUG, "package %s doesn't exist (ignored)", package->package);
  return true;
}

static void resolve_dependencies_marker_reset (void *key __attribute__ ((unused)), void *value, void *user_data __attribute__ ((unused)))
{
  di_package *p = value;
  p->resolver = 0;
}

void di_packages_resolve_dependencies_marker (di_packages *packages)
{
  if (!packages->resolver)
    packages->resolver = 1;
  else if (packages->resolver > (INT_MAX >> 2))
  {
    di_hash_table_foreach (packages->table, resolve_dependencies_marker_reset, NULL);
    packages->resolver = 1;
  }
  else
    packages->resolver <<= 3;

}

di_slist *di_packages_resolve_dependencies_special (di_packages *packages, di_slist *list, di_packages_resolve_dependencies_check *s)
{
  di_slist *install = di_slist_alloc ();
  di_slist_node *node;

  di_packages_resolve_dependencies_marker (packages);

  s->resolver = packages->resolver;

  for (node = list->head; node; node = node->next)
  {
    di_package *p = node->data;
    if (di_packages_resolve_dependencies_recurse (s, p, NULL))
      internal_di_slist_append_list (install, &s->install);
  }

  return install;
}

di_slist *di_packages_resolve_dependencies (di_packages *packages, di_slist *list, di_packages_allocator *allocator)
{
  struct di_packages_resolve_dependencies_check s =
  {
    di_packages_resolve_dependencies_check_real,
    di_packages_resolve_dependencies_check_virtual,
    di_packages_resolve_dependencies_check_non_existant,
    { NULL, NULL },
    allocator,
    0
  };

  return di_packages_resolve_dependencies_special (packages, list, &s);
}

di_slist *di_packages_resolve_dependencies_array_special (di_packages *packages, di_package **array, di_packages_resolve_dependencies_check *s)
{
  di_slist *install = di_slist_alloc ();

  di_packages_resolve_dependencies_marker (packages);

  s->resolver = packages->resolver;

  while (*array)
    if (di_packages_resolve_dependencies_recurse (s, *array++, NULL))
      internal_di_slist_append_list (install, &s->install);

  return install;
}

di_slist *di_packages_resolve_dependencies_array (di_packages *packages, di_package **array, di_packages_allocator *allocator)
{
  struct di_packages_resolve_dependencies_check s =
  {
    di_packages_resolve_dependencies_check_real,
    di_packages_resolve_dependencies_check_virtual,
    di_packages_resolve_dependencies_check_non_existant,
    { NULL, NULL },
    allocator,
    0
  };

  return di_packages_resolve_dependencies_array_special (packages, array, &s);
}

void di_packages_resolve_dependencies_mark_special (di_packages *packages, di_packages_resolve_dependencies_check *s)
{
  di_slist_node *node;

  di_packages_resolve_dependencies_marker (packages);

  s->resolver = packages->resolver;

  for (node = packages->list.head; node; node = node->next)
  {
    di_package *package = node->data;
    if (!(package->resolver & packages->resolver) && package->status_want == di_package_status_want_install)
      di_packages_resolve_dependencies_recurse (s, package, NULL);
  }
}

void di_packages_resolve_dependencies_mark (di_packages *packages)
{
  struct di_packages_resolve_dependencies_check s =
  {
    di_packages_resolve_dependencies_check_real,
    di_packages_resolve_dependencies_check_virtual,
    di_packages_resolve_dependencies_check_non_existant_quiet,
    { NULL, NULL },
    NULL,
    0
  };

  di_packages_resolve_dependencies_mark_special (packages, &s);
}

