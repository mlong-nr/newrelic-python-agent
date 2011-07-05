/* ------------------------------------------------------------------------- */

/* (C) Copyright 2010-2011 New Relic Inc. All rights reserved. */

/* ------------------------------------------------------------------------- */

#include "py_function_trace.h"

#include "py_utilities.h"

#include "globals.h"

#include "web_transaction.h"

#include "structmember.h"

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTrace_new(PyTypeObject *type, PyObject *args,
                                     PyObject *kwds)
{
    NRFunctionTraceObject *self;

    /*
     * Allocate the transaction object and initialise it as per
     * normal.
     */

    self = (NRFunctionTraceObject *)type->tp_alloc(type, 0);

    if (!self)
        return NULL;

    self->parent_transaction = NULL;
    self->transaction_trace = NULL;
    self->saved_trace_node = NULL;

    self->interesting = 1;

    return (PyObject *)self;
}

/* ------------------------------------------------------------------------- */

static int NRFunctionTrace_init(NRFunctionTraceObject *self, PyObject *args,
                                PyObject *kwds)
{
    NRTransactionObject *transaction = NULL;

    PyObject *name = NULL;
    PyObject *scope = Py_None;
    PyObject *interesting = Py_True;

    static char *kwlist[] = { "transaction", "name", "scope",
                              "interesting", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!O|OO!:FunctionTrace",
                                     kwlist, &NRTransaction_Type, &transaction,
                                     &name, &scope, &PyBool_Type,
                                     &interesting)) {
        return -1;
    }

    if (!PyString_Check(name) && !PyUnicode_Check(name)) {
        PyErr_Format(PyExc_TypeError, "expected string or Unicode for "
                     "name, found type '%s'", name->ob_type->tp_name);
        return -1;
    }

    if (!PyString_Check(scope) && !PyUnicode_Check(scope) &&
        scope != Py_None) {
        PyErr_Format(PyExc_TypeError, "scope argument must be string, "
                     "Unicode, or None, found type '%s'",
                     scope->ob_type->tp_name);
        return -1;
    }

    /*
     * Validate that this method hasn't been called previously.
     */

    if (self->parent_transaction) {
        PyErr_SetString(PyExc_TypeError, "trace already initialized");
        return -1;
    }

    /*
     * Validate that the parent transaction has been started.
     */

    if (transaction->transaction_state != NR_TRANSACTION_STATE_RUNNING) {
        PyErr_SetString(PyExc_RuntimeError, "transaction not active");
        return -1;
    }

    /*
     * Keep reference to parent transaction to ensure that it
     * is not destroyed before any trace created against it.
     */

    Py_INCREF(transaction);
    self->parent_transaction = transaction;

    /*
     * Don't need to create the inner agent transaction trace
     * node when executing against a dummy transaction.
     */

    if (transaction->transaction) {
        PyObject *name_as_bytes = NULL;
        PyObject *scope_as_bytes = NULL;

        const char *name_as_char = NULL;
        const char *scope_as_char = NULL;

        if (PyUnicode_Check(name)) {
            name_as_bytes = PyUnicode_AsUTF8String(name);
            name_as_char = PyString_AsString(name_as_bytes);
        }
        else {
            Py_INCREF(name);
            name_as_bytes = name;
            name_as_char = PyString_AsString(name);
        }

        if (scope == Py_None) {
            scope_as_bytes = PyString_FromString("Function");
            scope_as_char = PyString_AsString(scope_as_bytes);
        }
        else if (PyUnicode_Check(scope)) {
            scope_as_bytes = PyUnicode_AsUTF8String(scope);
            scope_as_char = PyString_AsString(scope_as_bytes);
        }
        else {
            Py_INCREF(scope);
            scope_as_bytes = scope;
            scope_as_char = PyString_AsString(scope);
        }

        self->transaction_trace =
                nr_web_transaction__allocate_function_node(
                transaction->transaction, name_as_char, NULL,
                scope_as_char);

        Py_DECREF(name_as_bytes);
        Py_DECREF(scope_as_bytes);

        if (interesting == Py_True)
            self->interesting = 1;
        else
            self->interesting = 0;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */

static void NRFunctionTrace_dealloc(NRFunctionTraceObject *self)
{
    Py_XDECREF(self->parent_transaction);

    Py_TYPE(self)->tp_free(self);
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTrace_enter(NRFunctionTraceObject *self,
                                        PyObject *args)
{
    if (!self->transaction_trace) {
        Py_INCREF(self);
        return (PyObject *)self;
    }

    nr_node_header__record_starttime_and_push_current(
            (nr_node_header *)self->transaction_trace,
            &self->saved_trace_node);

    Py_INCREF(self);
    return (PyObject *)self;
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTrace_exit(NRFunctionTraceObject *self,
                                       PyObject *args)
{
    nr_web_transaction *transaction;
    nr_transaction_node *transaction_trace;

    transaction_trace = self->transaction_trace;

    if (!transaction_trace) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    nr_node_header__record_stoptime_and_pop_current(
            (nr_node_header *)transaction_trace, &self->saved_trace_node);

    transaction = self->parent_transaction->transaction;

#if 0
    /*
     * XXX Not current needed. Leave as place marker in case
     * that changes.
     */

    nr__generate_function_metrics_for_node_1(transaction_trace, transaction);
#endif

    if (!nr_node_header__delete_if_not_slow_enough(
            (nr_node_header *)transaction_trace, self->interesting,
            transaction)) {
        nr_web_transaction__convert_from_stack_based(transaction_trace,
                transaction);
    }

    self->saved_trace_node = NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

/* ------------------------------------------------------------------------- */

static PyMethodDef NRFunctionTrace_methods[] = {
    { "__enter__",  (PyCFunction)NRFunctionTrace_enter,  METH_NOARGS, 0 },
    { "__exit__",   (PyCFunction)NRFunctionTrace_exit,   METH_VARARGS, 0 },
    { NULL, NULL }
};

static PyGetSetDef NRFunctionTrace_getset[] = {
    { NULL },
};

PyTypeObject NRFunctionTrace_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_newrelic.FunctionTrace", /*tp_name*/
    sizeof(NRFunctionTraceObject), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)NRFunctionTrace_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
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
    NRFunctionTrace_methods, /*tp_methods*/
    0,                      /*tp_members*/
    NRFunctionTrace_getset, /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    (initproc)NRFunctionTrace_init, /*tp_init*/
    0,                      /*tp_alloc*/
    NRFunctionTrace_new,    /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_new(PyTypeObject *type, PyObject *args,
                                           PyObject *kwds)
{
    NRFunctionTraceWrapperObject *self;

    self = (NRFunctionTraceWrapperObject *)type->tp_alloc(type, 0);

    if (!self)
        return NULL;

    self->dict = NULL;
    self->descr_object = NULL;
    self->self_object = NULL;
    self->next_object = NULL;
    self->last_object = NULL;
    self->name = NULL;
    self->scope = NULL;
    self->interesting = 1;

    return (PyObject *)self;
}

/* ------------------------------------------------------------------------- */

static int NRFunctionTraceWrapper_init(NRFunctionTraceWrapperObject *self,
                                       PyObject *args, PyObject *kwds)
{
    PyObject *wrapped_object = NULL;

    PyObject *name = Py_None;
    PyObject *scope = Py_None;
    PyObject *interesting = Py_True;

    PyObject *object = NULL;

    static char *kwlist[] = { "wrapped", "name", "scope",
                              "interesting", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOO!:FunctionTraceWrapper",
                                     kwlist, &wrapped_object, &name, &scope,
                                     &PyBool_Type, &interesting)) {
        return -1;
    }

    Py_INCREF(wrapped_object);

    Py_XDECREF(self->descr_object);
    Py_XDECREF(self->self_object);

    self->descr_object = NULL;
    self->self_object = NULL;

    Py_XDECREF(self->dict);
    Py_XDECREF(self->next_object);
    Py_XDECREF(self->last_object);

    self->next_object = wrapped_object;
    self->last_object = NULL;
    self->dict = NULL;

    object = PyObject_GetAttrString(wrapped_object, "__newrelic__");

    if (object) {
        Py_DECREF(object);

        object = PyObject_GetAttrString(wrapped_object, "__last_object__");

        if (object)
            self->last_object = object;
        else
            PyErr_Clear();
    }
    else
        PyErr_Clear();

    if (!self->last_object) {
        Py_INCREF(wrapped_object);
        self->last_object = wrapped_object;
    }

    object = PyObject_GetAttrString(self->last_object, "__dict__");

    if (object)
        self->dict = object;
    else
        PyErr_Clear();

    Py_INCREF(name);
    Py_XDECREF(self->name);
    self->name = name;

    Py_INCREF(scope);
    Py_XDECREF(self->scope);
    self->scope = scope;

    if (interesting == Py_True)
        self->interesting = 1;
    else
        self->interesting = 0;

    return 0;
}

/* ------------------------------------------------------------------------- */

static void NRFunctionTraceWrapper_dealloc(NRFunctionTraceWrapperObject *self)
{
    Py_XDECREF(self->dict);

    Py_XDECREF(self->descr_object);
    Py_XDECREF(self->self_object);

    Py_XDECREF(self->next_object);
    Py_XDECREF(self->last_object);

    Py_XDECREF(self->name);
    Py_XDECREF(self->scope);

    Py_TYPE(self)->tp_free(self);
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_call(
        NRFunctionTraceWrapperObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *wrapped_result = NULL;

    PyObject *current_transaction = NULL;
    PyObject *function_trace = NULL;

    PyObject *instance_method = NULL;
    PyObject *method_args = NULL;
    PyObject *method_result = NULL;

    PyObject *name = NULL;
    PyObject *scope = NULL;

    /*
     * If there is no current transaction then we can call
     * the wrapped function and return immediately.
     */

    current_transaction = NRTransaction_CurrentTransaction();

    if (!current_transaction) {
        if (self->descr_object)
            return PyObject_Call(self->descr_object, args, kwds);
        else
            return PyObject_Call(self->next_object, args, kwds);
    }

    /* Create function trace context manager. */

    if (self->name == Py_None) {
        name = NRUtilities_CallableName(self->next_object,
                                        (PyObject *)self, args, ":");
    }
    else if (PyString_Check(self->name) || PyUnicode_Check(self->name)) {
        name = self->name;
        Py_INCREF(name);
    }
    else {
        /*
         * Name if actually a callable function to provide the
         * name based on arguments supplied to wrapped function.
         */

        if (self->descr_object) {
            PyObject *newargs = NULL;

            int i = 0;

            /*
	     * Where calling via a descriptor object, we
	     * need to reconstruct the arguments such
	     * that the original object self reference
	     * is included once more.
             */

            newargs = PyTuple_New(PyTuple_Size(args)+1);

            Py_INCREF(self->self_object);
            PyTuple_SetItem(newargs, 0, self->self_object);

            for (i=0; i<PyTuple_Size(args); i++) {
                PyObject *item = NULL;

                item = PyTuple_GetItem(args, i);
                Py_INCREF(item);
                PyTuple_SetItem(newargs, i+1, item);
            }

            name = PyObject_Call(self->name, newargs, kwds);

            Py_DECREF(newargs);
        }
        else {
            name = PyObject_Call(self->name, args, kwds);
        }

        if (!name)
            return NULL;
    }

    if (self->scope == Py_None) {
        Py_INCREF(self->scope);
        scope = self->scope;
    }
    else if (PyString_Check(self->scope) || PyUnicode_Check(self->scope)) {
        Py_INCREF(self->scope);
        scope = self->scope;
    }
    else {
        /*
         * Scope if actually a callable function to provide the
         * scope based on arguments supplied to wrapped function.
         */

        scope = PyObject_Call(self->scope, args, kwds);

        if (!scope) {
            Py_DECREF(name);
            return NULL;
        }
    }

    function_trace = PyObject_CallFunctionObjArgs((PyObject *)
            &NRFunctionTrace_Type, current_transaction, name,
            scope, self->interesting ? Py_True : Py_False, NULL);

    Py_DECREF(name);
    Py_DECREF(scope);

    if (!function_trace)
        return NULL;

    /* Now call __enter__() on the context manager. */

    instance_method = PyObject_GetAttrString(function_trace, "__enter__");

    method_args = PyTuple_Pack(0);
    method_result = PyObject_Call(instance_method, method_args, NULL);

    if (!method_result)
        PyErr_WriteUnraisable(instance_method);
    else
        Py_DECREF(method_result);

    Py_DECREF(method_args);
    Py_DECREF(instance_method);

    /*
     * Now call the actual wrapped function with the original
     * position and keyword arguments.
     */

    if (self->descr_object)
        wrapped_result = PyObject_Call(self->descr_object, args, kwds);
    else
        wrapped_result = PyObject_Call(self->next_object, args, kwds);

    /*
     * Now call __exit__() on the context manager. If the call
     * of the wrapped function is successful then pass all None
     * objects, else pass exception details.
     */

    instance_method = PyObject_GetAttrString(function_trace, "__exit__");

    if (wrapped_result) {
        method_args = PyTuple_Pack(3, Py_None, Py_None, Py_None);
        method_result = PyObject_Call(instance_method, method_args, NULL);

        if (!method_result)
            PyErr_WriteUnraisable(instance_method);
        else
            Py_DECREF(method_result);

        Py_DECREF(method_args);
        Py_DECREF(instance_method);
    }
    else {
        PyObject *type = NULL;
        PyObject *value = NULL;
        PyObject *traceback = NULL;

        PyErr_Fetch(&type, &value, &traceback);

        if (!value) {
            value = Py_None;
            Py_INCREF(value);
        }

        if (!traceback) {
            traceback = Py_None;
            Py_INCREF(traceback);
        }

        PyErr_NormalizeException(&type, &value, &traceback);

        method_args = PyTuple_Pack(3, type, value, traceback);
        method_result = PyObject_Call(instance_method, method_args, NULL);

        if (!method_result)
            PyErr_WriteUnraisable(instance_method);
        else
            Py_DECREF(method_result);

        Py_DECREF(method_args);
        Py_DECREF(instance_method);

        PyErr_Restore(type, value, traceback);
    }

    Py_DECREF(function_trace);

    return wrapped_result;
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_next(
        NRFunctionTraceWrapperObject *self, void *closure)
{
    if (!self->next_object) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(self->next_object);
    return self->next_object;
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_last(
        NRFunctionTraceWrapperObject *self, void *closure)
{
    if (!self->last_object) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(self->last_object);
    return self->last_object;
}
 
/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_marker(
        NRFunctionTraceWrapperObject *self, void *closure)
{
    Py_INCREF(Py_None);
    return Py_None;
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_module(
        NRFunctionTraceWrapperObject *self)
{
    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return NULL;
    }

    return PyObject_GetAttrString(self->last_object, "__module__");
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_name(
        NRFunctionTraceWrapperObject *self)
{
    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return NULL;
    }

    return PyObject_GetAttrString(self->last_object, "__name__");
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_doc(
        NRFunctionTraceWrapperObject *self)
{
    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return NULL;
    }

    return PyObject_GetAttrString(self->last_object, "__doc__");
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_get_dict(
        NRFunctionTraceWrapperObject *self)
{
    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return NULL;
    }

    if (self->dict) {
        Py_INCREF(self->dict);
        return self->dict;
    }

    return PyObject_GetAttrString(self->last_object, "__dict__");
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_getattro(
        NRFunctionTraceWrapperObject *self, PyObject *name)
{
    PyObject *object = NULL;

    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return NULL;
    }

    object = PyObject_GenericGetAttr((PyObject *)self, name);

    if (object)
        return object;

    PyErr_Clear();

    return PyObject_GetAttr(self->last_object, name);
}

/* ------------------------------------------------------------------------- */

static int NRFunctionTraceWrapper_setattro(
        NRFunctionTraceWrapperObject *self, PyObject *name, PyObject *value)
{
    if (!self->last_object) {
      PyErr_SetString(PyExc_ValueError,
              "object wrapper has not been initialised");
      return -1;
    }

    return PyObject_SetAttr(self->last_object, name, value);
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceWrapper_descr_get(
        NRFunctionTraceWrapperObject *self, PyObject *object, PyObject *type)
{
    PyObject *method = NULL;

    if (object == Py_None) {
        Py_INCREF(self);
        return (PyObject *)self;
    }

    method = PyObject_GetAttrString(self->next_object, "__get__");

    if (method) {
        PyObject *descr = NULL;

        NRFunctionTraceWrapperObject *result;

        descr = PyObject_CallFunctionObjArgs(method, object, type, NULL);

        /*
         * We are circumventing new/init here for object but
         * easier than duplicating all the code to create a
         * special descriptor version of wrapper.
         */

        result = (NRFunctionTraceWrapperObject *)
                (&NRFunctionTraceWrapper_Type)->tp_alloc(
                &NRFunctionTraceWrapper_Type, 0);

        if (!result)
            return NULL;

        Py_XINCREF(self->dict);
        result->dict = self->dict;

        Py_XINCREF(descr);
        result->descr_object = descr;

        Py_XINCREF(object);
        result->self_object = object;

        Py_XINCREF(self->next_object);
        result->next_object = self->next_object;

        Py_XINCREF(self->last_object);
        result->last_object = self->last_object;

        Py_XINCREF(self->name);
        result->name = self->name;

        Py_XINCREF(self->scope);
        result->scope = self->scope;

        result->interesting = self->interesting;

        Py_DECREF(descr);
        Py_DECREF(method);

        return (PyObject *)result;
    }
    else {
        PyErr_Clear();

        Py_INCREF(self);
        return (PyObject *)self;
    }

}

/* ------------------------------------------------------------------------- */

static PyGetSetDef NRFunctionTraceWrapper_getset[] = {
    { "__next_object__",    (getter)NRFunctionTraceWrapper_get_next,
                            NULL, 0 },
    { "__last_object__",    (getter)NRFunctionTraceWrapper_get_last,
                            NULL, 0 },
    { "__newrelic__",       (getter)NRFunctionTraceWrapper_get_marker,
                            NULL, 0 },
    { "__module__",         (getter)NRFunctionTraceWrapper_get_module,
                            NULL, 0 },
    { "__name__",           (getter)NRFunctionTraceWrapper_get_name,
                            NULL, 0 },
    { "__doc__",            (getter)NRFunctionTraceWrapper_get_doc,
                            NULL, 0 },
    { "__dict__",           (getter)NRFunctionTraceWrapper_get_dict,
                            NULL, 0 },
    { NULL },
};

PyTypeObject NRFunctionTraceWrapper_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_newrelic.FunctionTraceWrapper", /*tp_name*/
    sizeof(NRFunctionTraceWrapperObject), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)NRFunctionTraceWrapper_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    (ternaryfunc)NRFunctionTraceWrapper_call, /*tp_call*/
    0,                      /*tp_str*/
    (getattrofunc)NRFunctionTraceWrapper_getattro, /*tp_getattro*/
    (setattrofunc)NRFunctionTraceWrapper_setattro, /*tp_setattro*/
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
    NRFunctionTraceWrapper_getset, /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    (descrgetfunc)NRFunctionTraceWrapper_descr_get, /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    offsetof(NRFunctionTraceWrapperObject, dict), /*tp_dictoffset*/
    (initproc)NRFunctionTraceWrapper_init, /*tp_init*/
    0,                      /*tp_alloc*/
    NRFunctionTraceWrapper_new, /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceDecorator_new(PyTypeObject *type,
                                              PyObject *args, PyObject *kwds)
{
    NRFunctionTraceDecoratorObject *self;

    self = (NRFunctionTraceDecoratorObject *)type->tp_alloc(type, 0);

    if (!self)
        return NULL;

    self->name = NULL;
    self->scope = NULL;
    self->interesting = 1;

    return (PyObject *)self;
}

/* ------------------------------------------------------------------------- */

static int NRFunctionTraceDecorator_init(NRFunctionTraceDecoratorObject *self,
                                         PyObject *args, PyObject *kwds)
{
    PyObject *name = Py_None;
    PyObject *scope = Py_None;
    PyObject *interesting = Py_True;

    static char *kwlist[] = { "name", "scope", "interesting", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOO!:FunctionTraceDecorator",
                                     kwlist, &name, &scope, &PyBool_Type,
                                     &interesting)) {
        return -1;
    }

    Py_INCREF(name);
    Py_XDECREF(self->name);
    self->name = name;

    Py_INCREF(scope);
    Py_XDECREF(self->scope);
    self->scope = scope;

    if (interesting == Py_True)
        self->interesting = 1;
    else
        self->interesting = 0;

    return 0;
}

/* ------------------------------------------------------------------------- */

static void NRFunctionTraceDecorator_dealloc(
        NRFunctionTraceDecoratorObject *self)
{
    Py_XDECREF(self->name);
    Py_XDECREF(self->scope);

    Py_TYPE(self)->tp_free(self);
}

/* ------------------------------------------------------------------------- */

static PyObject *NRFunctionTraceDecorator_call(
        NRFunctionTraceDecoratorObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *wrapped_object = NULL;

    static char *kwlist[] = { "wrapped", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:FunctionTraceDecorator",
                                     kwlist, &wrapped_object)) {
        return NULL;
    }

    return PyObject_CallFunctionObjArgs(
            (PyObject *)&NRFunctionTraceWrapper_Type, wrapped_object,
            self->name, self->scope, self->interesting ? Py_True : Py_False,
            NULL);
}

/* ------------------------------------------------------------------------- */

PyTypeObject NRFunctionTraceDecorator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_newrelic.FunctionTraceDecorator", /*tp_name*/
    sizeof(NRFunctionTraceDecoratorObject), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)NRFunctionTraceDecorator_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    (ternaryfunc)NRFunctionTraceDecorator_call, /*tp_call*/
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
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    (initproc)NRFunctionTraceDecorator_init, /*tp_init*/
    0,                      /*tp_alloc*/
    NRFunctionTraceDecorator_new, /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

/* ------------------------------------------------------------------------- */

/*
 * vim: set cino=>2,e0,n0,f0,{2,}0,^0,\:2,=2,p2,t2,c1,+2,(2,u2,)20,*30,g2,h2 ts=8
 */
