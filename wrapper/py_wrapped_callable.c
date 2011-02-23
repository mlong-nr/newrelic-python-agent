/* ------------------------------------------------------------------------- */

/* (C) Copyright 2010-2011 New Relic Inc. All rights reserved. */

/* ------------------------------------------------------------------------- */

#include "py_wrapped_callable.h"

#include "py_utilities.h"

/* ------------------------------------------------------------------------- */

static PyObject *NRWrappedCallable_new(PyTypeObject *type, PyObject *args,
                                       PyObject *kwds)
{
    NRWrappedCallableObject *self;

    self = (NRWrappedCallableObject *)type->tp_alloc(type, 0);

    if (!self)
        return NULL;

    self->wrapped_object = NULL;

    self->function_type = 0;
    self->function_object = NULL;
    self->run_once = 0;

    return (PyObject *)self;
}

/* ------------------------------------------------------------------------- */

static void NRWrappedCallable_dealloc(NRWrappedCallableObject *self)
{
    Py_DECREF(self->wrapped_object);
    Py_XDECREF(self->function_object);

    Py_TYPE(self)->tp_free(self);
}

/* ------------------------------------------------------------------------- */

static PyObject *NRWrappedCallable_call(NRWrappedCallableObject *self,
                                        PyObject *args, PyObject *kwds)
{
    PyObject *result = NULL;

    if (self->function_type == NR_FUNCTION_TYPE_PRE_FUNCTION) {
        PyObject *function_result = NULL;

        function_result = PyObject_Call(self->function_object, args, kwds);

        if (!function_result)
            return NULL;

        Py_DECREF(function_result);
    }

    result = PyObject_Call(self->wrapped_object, args, kwds);

    /*
     * TODO If main call fails we need to remember it and clear
     * it before calling post function. Then need to restore
     * the error before returning of post function succeeds.
     * If post function fails, then need to return its error.
     */

    if (self->function_type == NR_FUNCTION_TYPE_POST_FUNCTION) {
        PyObject *function_result = NULL;

        function_result = PyObject_Call(self->function_object, args, kwds);

        if (!function_result)
            return NULL;

        Py_DECREF(function_result);
    }

    return result;
}
 
/* ------------------------------------------------------------------------- */

static PyObject *NRWrappedCallable_descr_get(PyObject *function,
                                             PyObject *object,
                                             PyObject *type)
{
    if (object == Py_None)
        object = NULL;

    return PyMethod_New(function, object, type);
}

/* ------------------------------------------------------------------------- */

#ifndef PyVarObject_HEAD_INIT
#define PyVarObject_HEAD_INIT(type, size) PyObject_HEAD_INIT(type) size,
#endif

PyTypeObject NRWrappedCallable_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_newrelic.WrappedCallable", /*tp_name*/
    sizeof(NRWrappedCallableObject), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)NRWrappedCallable_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    (ternaryfunc)NRWrappedCallable_call, /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    0,                      /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    0,                      /*tp_methods*/
    0,                      /*tp_members*/
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    NRWrappedCallable_descr_get, /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    0,                      /*tp_init*/
    0,                      /*tp_alloc*/
    NRWrappedCallable_new,  /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

/* ------------------------------------------------------------------------- */

PyObject *NRWrappedCallable_WrapPreFunction(const char *module_name,
                                            const char *class_name,
                                            const char *name,
                                            PyObject *function,
                                            int run_once)
{
    PyObject *callable_object = NULL;

    PyObject *parent_object = NULL;
    const char *attribute_name = NULL;

    NRWrappedCallableObject *wrapper_object = NULL;

    callable_object = NRUtilities_LookupCallable(module_name, class_name,
                                                 name, &parent_object,
                                                 &attribute_name);

    if (!callable_object)
        return NULL;

    wrapper_object = (NRWrappedCallableObject *)PyObject_CallObject(
            (PyObject *)&NRWrappedCallable_Type, NULL);

    wrapper_object->wrapped_object = callable_object;

    Py_INCREF(function);

    wrapper_object->function_type = NR_FUNCTION_TYPE_PRE_FUNCTION;
    wrapper_object->function_object = function;
    wrapper_object->run_once = run_once;

    if (PyModule_Check(parent_object)) {

        /*
	 * For a module, need to access the module dictionary
	 * and replace the attribute.
         */

        PyObject *dict = NULL;

        dict = PyModule_GetDict(parent_object);

        PyDict_SetItemString(dict, attribute_name, (PyObject *)wrapper_object);
    }
    else if (PyType_Check(parent_object) &&
             !(parent_object->ob_type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {

        /*
	 * For a builtin type of type defined in a C extension
	 * module, need to access the type dictionary directly
	 * and replace the attribute.
         */

        PyObject *dict = NULL;

        dict = ((PyTypeObject *)parent_object)->tp_dict;

        PyDict_SetItemString(dict, attribute_name,
                             (PyObject *)wrapper_object);
    }
    else {

        /*
         * For anything else, attempt to set it via the object
         * attribute interface.
         */

        if (PyObject_SetAttrString(parent_object, attribute_name,
                               (PyObject *)wrapper_object) == -1) {
            Py_DECREF(parent_object);
            Py_DECREF(wrapper_object);

            return NULL;
        }
    }

    Py_DECREF(parent_object);
    Py_DECREF(wrapper_object);

    Py_INCREF(callable_object);

    return callable_object;
}

/* ------------------------------------------------------------------------- */

/*
 * vim: et cino=>2,e0,n0,f0,{2,}0,^0,\:2,=2,p2,t2,c1,+2,(2,u2,)20,*30,g2,h2 ts=8
 */
