/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010  Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "pygi-private.h"

#include <girepository.h>

/* Copied from glib */
static void
canonicalize_key (gchar *key)
{
    gchar *p;

    for (p = key; *p != 0; p++)
    {
        gchar c = *p;

        if (c != '-' &&
            (c < '0' || c > '9') &&
            (c < 'A' || c > 'Z') &&
            (c < 'a' || c > 'z'))
                *p = '-';
    }
}

static GIPropertyInfo *
_pygi_lookup_property_from_g_type (GType g_type, const gchar *attr_name)
{
    GIRepository *repository;
    GIBaseInfo *info;
    gssize n_infos;
    gssize i;
    GType parent;

    repository = g_irepository_get_default();
    info = g_irepository_find_by_gtype (repository, g_type);
    if (info == NULL) {
        return NULL;
    }

    n_infos = g_object_info_get_n_properties ( (GIObjectInfo *) info);
    for (i = 0; i < n_infos; i++) {
        GIPropertyInfo *property_info;

        property_info = g_object_info_get_property ( (GIObjectInfo *) info, i);
        g_assert (info != NULL);

        if (strcmp (attr_name, g_base_info_get_name (property_info)) == 0) {
            g_base_info_unref (info);
            return property_info;
        }

        g_base_info_unref (property_info);
    }

    g_base_info_unref (info);

    parent = g_type_parent (g_type);
    if (parent > 0)
        return _pygi_lookup_property_from_g_type (parent, attr_name);

    return NULL;
}

PyObject *
pygi_get_property_value_real (PyGObject *instance,
                              const gchar *attr_name)
{
    GType g_type;
    GIPropertyInfo *property_info = NULL;
    char *property_name = g_strdup (attr_name);
    GParamSpec *pspec = NULL;
    GValue value = { 0, };
    GArgument arg = { 0, };
    PyObject *py_value = NULL;
    GITypeInfo *type_info = NULL;
    GITransfer transfer;

    canonicalize_key (property_name);

    g_type = pyg_type_from_object ((PyObject *)instance);
    property_info = _pygi_lookup_property_from_g_type (g_type, property_name);

    if (property_info == NULL)
        goto out;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (instance->obj),
                                          attr_name);
    if (pspec == NULL)
        goto out;

    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (instance->obj, attr_name, &value);

    type_info = g_property_info_get_type (property_info);
    transfer = g_property_info_get_ownership_transfer (property_info);

    // FIXME: Lots of types still unhandled
    if (g_type_info_is_pointer (type_info)) {
        arg.v_pointer = g_value_peek_pointer (&value);
    } else {
        GITypeTag type_tag = g_type_info_get_tag (type_info);
        switch (type_tag) {
            case GI_TYPE_TAG_INT32:
                arg.v_int = g_value_get_int (&value);
                break;
            case GI_TYPE_TAG_UINT32:
                arg.v_uint = g_value_get_uint (&value);
                break;
            case GI_TYPE_TAG_BOOLEAN:
                arg.v_boolean = g_value_get_boolean (&value);
                break;
            case GI_TYPE_TAG_INTERFACE:
                arg.v_pointer = g_value_get_object (&value);
                break;
            default:
                PyErr_Format (PyExc_NotImplementedError,
                              "Retrieving properties of type %s is not implemented",
                              g_type_tag_to_string (g_type_info_get_tag (type_info)));
                goto out;
        }
    }

    py_value = _pygi_argument_to_object (&arg, type_info, transfer);

out:
    g_free (property_name);
    if (property_info != NULL)
        g_base_info_unref (property_info);
    if (type_info != NULL)
        g_base_info_unref (type_info);

    return py_value;
}

gint
pygi_set_property_value_real (PyGObject *instance,
                              const gchar *attr_name,
                              PyObject *py_value)
{
    GType g_type;
    GIPropertyInfo *property_info = NULL;
    char *property_name = g_strdup (attr_name);
    GITypeInfo *type_info = NULL;
    GITypeTag type_tag;
    GITransfer transfer;
    GValue value = { 0, };
    GArgument arg = { 0, };
    GParamSpec *pspec = NULL;
    gint ret_value = -1;

    canonicalize_key (property_name);

    g_type = pyg_type_from_object ((PyObject *)instance);
    property_info = _pygi_lookup_property_from_g_type (g_type, property_name);

    if (property_info == NULL)
        goto out;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (instance->obj),
                                          attr_name);
    if (pspec == NULL)
        goto out;

    if (! (pspec->flags & G_PARAM_WRITABLE))
        goto out;

    type_info = g_property_info_get_type (property_info);
    transfer = g_property_info_get_ownership_transfer (property_info);
    arg = _pygi_argument_from_object (py_value, type_info, transfer);

    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));

    // FIXME: Lots of types still unhandled
    type_tag = g_type_info_get_tag (type_info);
    switch (type_tag) {
        case GI_TYPE_TAG_GHASH:
            g_value_set_boxed (&value, arg.v_pointer);
            break;
        case GI_TYPE_TAG_GLIST:
            g_value_set_pointer (&value, arg.v_pointer);
            break;
        case GI_TYPE_TAG_INT32:
            g_value_set_int (&value, arg.v_int);
            break;
        default:
            PyErr_Format (PyExc_NotImplementedError,
                          "Setting properties of type %s is not implemented",
                          g_type_tag_to_string (g_type_info_get_tag (type_info)));
            goto out;
    }

    g_object_set_property (instance->obj, attr_name, &value);

    ret_value = 0;

out:
    g_free (property_name);
    if (property_info != NULL)
        g_base_info_unref (property_info);
    if (type_info != NULL)
        g_base_info_unref (type_info);

    return ret_value;
}
