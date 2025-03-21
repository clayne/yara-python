/*
Copyright (c) 2007-2022. The YARA Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
/* headers */

#define PY_SSIZE_T_CLEAN

#include <Python.h>
#include "structmember.h"

#if PY_VERSION_HEX >= 0x02060000
#include "bytesobject.h"
#include "structseq.h"
#elif PY_VERSION_HEX < 0x02060000
#define PyBytes_AsString PyString_AsString
#define PyBytes_Check PyString_Check
#define PyBytes_FromStringAndSize PyString_FromStringAndSize
#endif

#include <time.h>
#include <yara.h>

#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

#ifndef PyVarObject_HEAD_INIT
#define PyVarObject_HEAD_INIT(type, size) PyObject_HEAD_INIT(type) size,
#endif

#if PY_VERSION_HEX < 0x03020000
typedef long Py_hash_t;
#endif

#if PY_MAJOR_VERSION >= 3
#define PY_STRING(x) PyUnicode_DecodeUTF8(x, strlen(x), "ignore" )
#define PY_STRING_FORMAT(...) PyUnicode_FromFormat(__VA_ARGS__)
#define PY_STRING_TO_C(x) PyUnicode_AsUTF8(x)
#define PY_STRING_CHECK(x) PyUnicode_Check(x)
#else
#define PY_STRING(x) PyString_FromString(x)
#define PY_STRING_FORMAT(...) PyString_FromFormat(__VA_ARGS__)
#define PY_STRING_TO_C(x) PyString_AsString(x)
#define PY_STRING_CHECK(x) (PyString_Check(x) || PyUnicode_Check(x))
#endif

#if PY_VERSION_HEX < 0x03020000
#define PyDescr_NAME(x) (((PyDescrObject*)x)->d_name)
#endif

/* Module globals */

static PyObject* YaraError = NULL;
static PyObject* YaraSyntaxError = NULL;
static PyObject* YaraTimeoutError = NULL;
static PyObject* YaraWarningError = NULL;


#define YARA_DOC "\
This module allows you to apply YARA rules to files or strings.\n\
\n\
For complete documentation please visit:\n\
https://yara.readthedocs.io/en/stable/yarapython.html\n"

#if defined(_WIN32) || defined(__CYGWIN__)
#include <string.h>
#define strdup _strdup
#endif

// Match object

typedef struct
{
  PyObject_HEAD
  PyObject* rule;
  PyObject* ns;
  PyObject* tags;
  PyObject* meta;
  PyObject* strings;

} Match;

static PyMemberDef Match_members[] = {
  {
    "rule",
    T_OBJECT_EX,
    offsetof(Match, rule),
    READONLY,
    "Name of the matching rule"
  },
  {
    "namespace",
    T_OBJECT_EX,
    offsetof(Match, ns),
    READONLY,
    "Namespace of the matching rule"
  },
  {
    "tags",
    T_OBJECT_EX,
    offsetof(Match, tags),
    READONLY,
    "List of tags associated to the rule"
  },
  {
    "meta",
    T_OBJECT_EX,
    offsetof(Match, meta),
    READONLY,
    "Dictionary with metadata associated to the rule"
  },
  {
    "strings",
    T_OBJECT_EX,
    offsetof(Match, strings),
    READONLY,
    "Tuple with offsets and strings that matched the file"
  },
  { NULL } // End marker
};

static PyObject* Match_NEW(
    const char* rule,
    const char* ns,
    PyObject* tags,
    PyObject* meta,
    PyObject* strings);

static void Match_dealloc(
  PyObject* self);

static PyObject* Match_repr(
    PyObject* self);

static PyObject* Match_getattro(
    PyObject* self,
    PyObject* name);

static PyObject* Match_richcompare(
    PyObject* self,
    PyObject* other,
    int op);

static Py_hash_t Match_hash(
    PyObject* self);


static PyMethodDef Match_methods[] =
{
  { NULL },
};

static PyTypeObject Match_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "yara.Match",               /*tp_name*/
  sizeof(Match),              /*tp_basicsize*/
  0,                          /*tp_itemsize*/
  (destructor)Match_dealloc,  /*tp_dealloc*/
  0,                          /*tp_print*/
  0,                          /*tp_getattr*/
  0,                          /*tp_setattr*/
  0,                          /*tp_compare*/
  Match_repr,                 /*tp_repr*/
  0,                          /*tp_as_number*/
  0,                          /*tp_as_sequence*/
  0,                          /*tp_as_mapping*/
  Match_hash,                 /*tp_hash */
  0,                          /*tp_call*/
  0,                          /*tp_str*/
  Match_getattro,             /*tp_getattro*/
  0,                          /*tp_setattro*/
  0,                          /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "Match class",              /* tp_doc */
  0,                          /* tp_traverse */
  0,                          /* tp_clear */
  Match_richcompare,          /* tp_richcompare */
  0,                          /* tp_weaklistoffset */
  0,                          /* tp_iter */
  0,                          /* tp_iternext */
  Match_methods,              /* tp_methods */
  Match_members,              /* tp_members */
  0,                          /* tp_getset */
  0,                          /* tp_base */
  0,                          /* tp_dict */
  0,                          /* tp_descr_get */
  0,                          /* tp_descr_set */
  0,                          /* tp_dictoffset */
  0,                          /* tp_init */
  0,                          /* tp_alloc */
  0,                          /* tp_new */
};

// StringMatch object

typedef struct
{
  PyObject_HEAD
  PyObject* identifier;
  PyObject* instances;
  // This is not exposed directly because it contains flags that are internal
  // to yara (eg: STRING_FLAGS_FITS_IN_ATOM) along with modifiers
  // (eg: STRING_FLAGS_XOR).
  uint64_t flags;
} StringMatch;

static PyMemberDef StringMatch_members[] = {
  {
    "identifier",
    T_OBJECT_EX,
    offsetof(StringMatch, identifier),
    READONLY,
    "Name of the matching string"
  },
  {
    "instances",
    T_OBJECT_EX,
    offsetof(StringMatch, instances),
    READONLY,
    "StringMatchInstance objects of the matching string"
  },
  { NULL } // End marker
};

static PyObject* StringMatch_NEW(
    const char* identifier,
    uint64_t flags,
    PyObject* instance_list);

static void StringMatch_dealloc(
  PyObject* self);

static PyObject* StringMatch_repr(
    PyObject* self);

static PyObject* StringMatch_getattro(
    PyObject* self,
    PyObject* name);

static Py_hash_t StringMatch_hash(
    PyObject* self);

static PyObject* StringMatch_is_xor(
    PyObject* self,
    PyObject* args);


static PyMethodDef StringMatch_methods[] =
{
  {
    "is_xor",
    (PyCFunction) StringMatch_is_xor,
    METH_NOARGS,
    "Return true if a string has the xor modifier"
  },
  { NULL },
};

static PyTypeObject StringMatch_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "yara.StringMatch",               /*tp_name*/
  sizeof(StringMatch),              /*tp_basicsize*/
  0,                                /*tp_itemsize*/
  (destructor)StringMatch_dealloc,  /*tp_dealloc*/
  0,                                /*tp_print*/
  0,                                /*tp_getattr*/
  0,                                /*tp_setattr*/
  0,                                /*tp_compare*/
  StringMatch_repr,                 /*tp_repr*/
  0,                                /*tp_as_number*/
  0,                                /*tp_as_sequence*/
  0,                                /*tp_as_mapping*/
  StringMatch_hash,                 /*tp_hash */
  0,                                /*tp_call*/
  0,                                /*tp_str*/
  StringMatch_getattro,             /*tp_getattro*/
  0,                                /*tp_setattro*/
  0,                                /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "StringMatch class",              /* tp_doc */
  0,                                /* tp_traverse */
  0,                                /* tp_clear */
  0,                                /* tp_richcompare */ // XXX: Implement richcompare?
  0,                                /* tp_weaklistoffset */
  0,                                /* tp_iter */
  0,                                /* tp_iternext */
  StringMatch_methods,              /* tp_methods */
  StringMatch_members,              /* tp_members */
  0,                                /* tp_getset */
  0,                                /* tp_base */
  0,                                /* tp_dict */
  0,                                /* tp_descr_get */
  0,                                /* tp_descr_set */
  0,                                /* tp_dictoffset */
  0,                                /* tp_init */
  0,                                /* tp_alloc */
  0,                                /* tp_new */
};

// StringMatchInstance object

typedef struct
{
  PyObject_HEAD
  PyObject* offset;
  PyObject* matched_data;
  PyObject* matched_length;
  PyObject* xor_key;
} StringMatchInstance;

static PyMemberDef StringMatchInstance_members[] = {
  {
    "offset",
    T_OBJECT_EX,
    offsetof(StringMatchInstance, offset),
    READONLY,
    "Offset of the matched data"
  },
  {
    "matched_data",
    T_OBJECT_EX,
    offsetof(StringMatchInstance, matched_data),
    READONLY,
    "Matched data"
  },
  {
    "matched_length",
    T_OBJECT_EX,
    offsetof(StringMatchInstance, matched_length),
    READONLY,
    "Length of matched data"
  },
  {
    "xor_key",
    T_OBJECT_EX,
    offsetof(StringMatchInstance, xor_key),
    READONLY,
    "XOR key found for xor strings"
  },
  { NULL } // End marker
};

static PyObject* StringMatchInstance_NEW(
    uint64_t offset,
    PyObject* matched_data,
    int32_t match_length,
    uint8_t xor_key);

static void StringMatchInstance_dealloc(
  PyObject* self);

static PyObject* StringMatchInstance_repr(
    PyObject* self);

static PyObject* StringMatchInstance_getattro(
    PyObject* self,
    PyObject* name);

static Py_hash_t StringMatchInstance_hash(
    PyObject* self);

static PyObject* StringMatchInstance_plaintext(
    PyObject* self,
    PyObject* args);


static PyMethodDef StringMatchInstance_methods[] =
{
  {
    "plaintext",
    (PyCFunction) StringMatchInstance_plaintext,
    METH_NOARGS,
    "Return matched data after xor key applied."
  },
  { NULL },
};

static PyTypeObject StringMatchInstance_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "yara.StringMatchInstance",               /*tp_name*/
  sizeof(StringMatchInstance),              /*tp_basicsize*/
  0,                                        /*tp_itemsize*/
  (destructor)StringMatchInstance_dealloc,  /*tp_dealloc*/
  0,                                        /*tp_print*/
  0,                                        /*tp_getattr*/
  0,                                        /*tp_setattr*/
  0,                                        /*tp_compare*/
  StringMatchInstance_repr,                 /*tp_repr*/
  0,                                        /*tp_as_number*/
  0,                                        /*tp_as_sequence*/
  0,                                        /*tp_as_mapping*/
  StringMatchInstance_hash,                 /*tp_hash */
  0,                                        /*tp_call*/
  0,                                        /*tp_str*/
  StringMatchInstance_getattro,             /*tp_getattro*/
  0,                                        /*tp_setattro*/
  0,                                        /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "StringMatchInstance class",              /* tp_doc */
  0,                                        /* tp_traverse */
  0,                                        /* tp_clear */
  0,                                        /* tp_richcompare */ // XXX: Implement richcompare?
  0,                                        /* tp_weaklistoffset */
  0,                                        /* tp_iter */
  0,                                        /* tp_iternext */
  StringMatchInstance_methods,              /* tp_methods */
  StringMatchInstance_members,              /* tp_members */
  0,                                        /* tp_getset */
  0,                                        /* tp_base */
  0,                                        /* tp_dict */
  0,                                        /* tp_descr_get */
  0,                                        /* tp_descr_set */
  0,                                        /* tp_dictoffset */
  0,                                        /* tp_init */
  0,                                        /* tp_alloc */
  0,                                        /* tp_new */
};

// Rule object

typedef struct
{
  PyObject_HEAD
  PyObject* identifier;
  PyObject* tags;
  PyObject* meta;
  PyObject* global;
  PyObject* private;
} Rule;

static void Rule_dealloc(
    PyObject* self);

static PyObject* Rule_getattro(
    PyObject* self,
    PyObject* name);

static PyMemberDef Rule_members[] = {
  {
    "is_global",
    T_OBJECT_EX,
    offsetof(Rule, global),
    READONLY,
    "Rule is global"
  },
  {
    "is_private",
    T_OBJECT_EX,
    offsetof(Rule, private),
    READONLY,
    "Rule is private"
  },
  {
    "identifier",
    T_OBJECT_EX,
    offsetof(Rule, identifier),
    READONLY,
    "Name of the rule"
  },
  {
    "tags",
    T_OBJECT_EX,
    offsetof(Rule, tags),
    READONLY,
    "Tags for the rule"
  },
  {
    "meta",
    T_OBJECT_EX,
    offsetof(Rule, meta),
    READONLY,
    "Meta for the rule"
  },
  { NULL } // End marker
};

static PyMethodDef Rule_methods[] =
{
  { NULL, NULL }
};

static PyTypeObject Rule_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "yara.Rule",                /*tp_name*/
  sizeof(Rule),               /*tp_basicsize*/
  0,                          /*tp_itemsize*/
  (destructor) Rule_dealloc,  /*tp_dealloc*/
  0,                          /*tp_print*/
  0,                          /*tp_getattr*/
  0,                          /*tp_setattr*/
  0,                          /*tp_compare*/
  0,                          /*tp_repr*/
  0,                          /*tp_as_number*/
  0,                          /*tp_as_sequence*/
  0,                          /*tp_as_mapping*/
  0,                          /*tp_hash */
  0,                          /*tp_call*/
  0,                          /*tp_str*/
  Rule_getattro,              /*tp_getattro*/
  0,                          /*tp_setattro*/
  0,                          /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "Rule class",               /* tp_doc */
  0,                          /* tp_traverse */
  0,                          /* tp_clear */
  0,                          /* tp_richcompare */
  0,                          /* tp_weaklistoffset */
  0,                          /* tp_iter */
  0,                          /* tp_iternext */
  Rule_methods,               /* tp_methods */
  Rule_members,               /* tp_members */
  0,                          /* tp_getset */
  0,                          /* tp_base */
  0,                          /* tp_dict */
  0,                          /* tp_descr_get */
  0,                          /* tp_descr_set */
  0,                          /* tp_dictoffset */
  0,                          /* tp_init */
  0,                          /* tp_alloc */
  0,                          /* tp_new */
};


// Rules object

typedef struct
{
  PyObject_HEAD
  PyObject* externals;
  PyObject* warnings;
  YR_RULES* rules;
  YR_RULE* iter_current_rule;
} Rules;


static Rules* Rules_NEW(void);

static void Rules_dealloc(
    PyObject* self);

static PyObject* Rules_match(
    PyObject* self,
    PyObject* args,
    PyObject* keywords);

static PyObject* Rules_save(
    PyObject* self,
    PyObject* args,
    PyObject* keywords);

static PyObject* Rules_profiling_info(
    PyObject* self,
    PyObject* args);

static PyObject* Rules_getattro(
    PyObject* self,
    PyObject* name);

static PyObject* Rules_next(
    PyObject* self);

static PyMemberDef Rules_members[] = {
  {
    "warnings",
    T_OBJECT_EX,
    offsetof(Rules, warnings),
    READONLY,
    "List of compiler warnings"
  },
  { NULL } // End marker
};

static PyMethodDef Rules_methods[] =
{
  {
    "match",
    (PyCFunction) Rules_match,
    METH_VARARGS | METH_KEYWORDS
  },
  {
    "save",
    (PyCFunction) Rules_save,
    METH_VARARGS | METH_KEYWORDS
  },
  {
    "profiling_info",
    (PyCFunction) Rules_profiling_info,
    METH_NOARGS
  },
  {
    NULL,
    NULL
  }
};

static PyTypeObject Rules_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "yara.Rules",               /*tp_name*/
  sizeof(Rules),              /*tp_basicsize*/
  0,                          /*tp_itemsize*/
  (destructor) Rules_dealloc, /*tp_dealloc*/
  0,                          /*tp_print*/
  0,                          /*tp_getattr*/
  0,                          /*tp_setattr*/
  0,                          /*tp_compare*/
  0,                          /*tp_repr*/
  0,                          /*tp_as_number*/
  0,                          /*tp_as_sequence*/
  0,                          /*tp_as_mapping*/
  0,                          /*tp_hash */
  0,                          /*tp_call*/
  0,                          /*tp_str*/
  Rules_getattro,             /*tp_getattro*/
  0,                          /*tp_setattro*/
  0,                          /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "Rules class",              /* tp_doc */
  0,                          /* tp_traverse */
  0,                          /* tp_clear */
  0,                          /* tp_richcompare */
  0,                          /* tp_weaklistoffset */
  PyObject_SelfIter,          /* tp_iter */
  (iternextfunc) Rules_next,  /* tp_iternext */
  Rules_methods,              /* tp_methods */
  Rules_members,              /* tp_members */
  0,                          /* tp_getset */
  0,                          /* tp_base */
  0,                          /* tp_dict */
  0,                          /* tp_descr_get */
  0,                          /* tp_descr_set */
  0,                          /* tp_dictoffset */
  0,                          /* tp_init */
  0,                          /* tp_alloc */
  0,                          /* tp_new */
};

typedef struct _CALLBACK_DATA
{
  PyObject* matches;
  PyObject* callback;
  PyObject* modules_data;
  PyObject* modules_callback;
  PyObject* warnings_callback;
  PyObject* console_callback;
  int which;
  bool allow_duplicate_metadata;

} CALLBACK_DATA;

static PyStructSequence_Field RuleString_Fields[] = {
  {"namespace", "Namespace of the rule"},
  {"rule", "Identifier of the rule"},
  {"string", "Identifier of the string"},
  {NULL}
};

static PyStructSequence_Desc RuleString_Desc = {
  "RuleString",
  "Named tuple tying together rule identifier and string identifier",
  RuleString_Fields,
  (sizeof(RuleString_Fields) / sizeof(RuleString_Fields[0])) - 1
};

static PyTypeObject RuleString_Type = {0};

// Forward declarations for handling module data.
PyObject* convert_structure_to_python(
    YR_OBJECT_STRUCTURE* structure);


PyObject* convert_array_to_python(
    YR_OBJECT_ARRAY* array);


PyObject* convert_dictionary_to_python(
    YR_OBJECT_DICTIONARY* dictionary);


PyObject* convert_object_to_python(
    YR_OBJECT* object)
{
  PyObject* result = NULL;

  if (object == NULL)
    return NULL;

  switch(object->type)
  {
    case OBJECT_TYPE_INTEGER:
      if (object->value.i != YR_UNDEFINED)
        result = Py_BuildValue("l", object->value.i);
      break;

    case OBJECT_TYPE_STRING:
      if (object->value.ss != NULL)
        result = PyBytes_FromStringAndSize(
            object->value.ss->c_string,
            object->value.ss->length);
      break;

    case OBJECT_TYPE_STRUCTURE:
      result = convert_structure_to_python(object_as_structure(object));
      break;

    case OBJECT_TYPE_ARRAY:
      result = convert_array_to_python(object_as_array(object));
      break;

    case OBJECT_TYPE_FUNCTION:
      // Do nothing with functions...
      break;

    case OBJECT_TYPE_DICTIONARY:
      result = convert_dictionary_to_python(object_as_dictionary(object));
      break;

    case OBJECT_TYPE_FLOAT:
      if (!isnan(object->value.d))
        result = Py_BuildValue("d", object->value.d);
      break;

    default:
      break;
  }

  return result;
}


PyObject* convert_structure_to_python(
    YR_OBJECT_STRUCTURE* structure)
{
  YR_STRUCTURE_MEMBER* member;

  PyObject* py_object;
  PyObject* py_dict = PyDict_New();

  if (py_dict == NULL)
    return py_dict;

  member = structure->members;

  while (member != NULL)
  {
    py_object = convert_object_to_python(member->object);

    if (py_object != NULL)
    {
      PyDict_SetItemString(py_dict, member->object->identifier, py_object);
      Py_DECREF(py_object);
    }

    member =member->next;
  }

  return py_dict;
}


PyObject* convert_array_to_python(
    YR_OBJECT_ARRAY* array)
{
  PyObject* py_object;
  PyObject* py_list = PyList_New(0);

  if (py_list == NULL)
    return py_list;

  // If there is nothing in the list, return an empty Python list
  if (array->items == NULL)
    return py_list;

  for (int i = 0; i < array->items->length; i++)
  {
    py_object = convert_object_to_python(array->items->objects[i]);

    if (py_object != NULL)
    {
      PyList_Append(py_list, py_object);
      Py_DECREF(py_object);
    }
  }

  return py_list;
}


PyObject* convert_dictionary_to_python(
    YR_OBJECT_DICTIONARY* dictionary)
{
  PyObject* py_object;
  PyObject* py_dict = PyDict_New();

  if (py_dict == NULL)
    return py_dict;

  // If there is nothing in the YARA dictionary, return an empty Python dict
  if (dictionary->items == NULL)
    return py_dict;

  for (int i = 0; i < dictionary->items->used; i++)
  {
    py_object = convert_object_to_python(dictionary->items->objects[i].obj);

    if (py_object != NULL)
    {
      PyDict_SetItemString(
          py_dict,
          dictionary->items->objects[i].key->c_string,
          py_object);

      Py_DECREF(py_object);
    }
  }

  return py_dict;
}


static int handle_import_module(
    YR_MODULE_IMPORT* module_import,
    CALLBACK_DATA* data)
{
  if (data->modules_data == NULL)
    return CALLBACK_CONTINUE;

  PyGILState_STATE gil_state = PyGILState_Ensure();

  PyObject* module_data = PyDict_GetItemString(
      data->modules_data,
      module_import->module_name);

  #if PY_MAJOR_VERSION >= 3
  if (module_data != NULL && PyBytes_Check(module_data))
  #else
  if (module_data != NULL && PyString_Check(module_data))
  #endif
  {
    Py_ssize_t data_size;

    #if PY_MAJOR_VERSION >= 3
    PyBytes_AsStringAndSize(
        module_data,
        (char**) &module_import->module_data,
        &data_size);
    #else
    PyString_AsStringAndSize(
        module_data,
        (char**) &module_import->module_data,
        &data_size);
    #endif

    module_import->module_data_size = data_size;
  }

  PyGILState_Release(gil_state);

  return CALLBACK_CONTINUE;
}


static int handle_module_imported(
    void* message_data,
    CALLBACK_DATA* data)
{
  if (data->modules_callback == NULL)
    return CALLBACK_CONTINUE;

  PyGILState_STATE gil_state = PyGILState_Ensure();

  PyObject* module_info_dict = convert_structure_to_python(
      object_as_structure(message_data));

  if (module_info_dict == NULL)
  {
    PyGILState_Release(gil_state);
    return CALLBACK_CONTINUE;
  }

  PyObject* object = PY_STRING(object_as_structure(message_data)->identifier);
  PyDict_SetItemString(module_info_dict, "module", object);
  Py_DECREF(object);

  Py_INCREF(data->modules_callback);

  PyObject* callback_result = PyObject_CallFunctionObjArgs(
      data->modules_callback,
      module_info_dict,
      NULL);

  int result = CALLBACK_CONTINUE;

  if (callback_result != NULL)
  {
    #if PY_MAJOR_VERSION >= 3
    if (PyLong_Check(callback_result))
    #else
    if (PyLong_Check(callback_result) || PyInt_Check(callback_result))
    #endif
    {
      result = (int) PyLong_AsLong(callback_result);
    }
  }
  else
  {
    result = CALLBACK_ERROR;
  }

  Py_XDECREF(callback_result);
  Py_DECREF(module_info_dict);
  Py_DECREF(data->modules_callback);

  PyGILState_Release(gil_state);

  return result;
}


static int handle_console_log(
    void* message_data,
    CALLBACK_DATA* data)
{
  PyGILState_STATE gil_state = PyGILState_Ensure();
  int result = CALLBACK_CONTINUE;

  if (data->console_callback == NULL)
  {
    // If the user does not specify a console callback we dump to stdout.
    // If we want to support 3.2 and newer only we can use
    // https://docs.python.org/3/c-api/sys.html?highlight=stdout#c.PySys_FormatStdout
    // instead of this call with the limit.
    PySys_WriteStdout("%.1000s\n", (char*) message_data);
  }
  else
  {
    PyObject* log_string = PY_STRING((char*) message_data);
    Py_INCREF(data->console_callback);

    PyObject* callback_result = PyObject_CallFunctionObjArgs(
        data->console_callback,
        log_string,
        NULL);

    if (callback_result != NULL)
    {
      #if PY_MAJOR_VERSION >= 3
      if (PyLong_Check(callback_result))
      #else
      if (PyLong_Check(callback_result) || PyInt_Check(callback_result))
      #endif
      {
        result = (int) PyLong_AsLong(callback_result);
      }
    }
    else
    {
      result = CALLBACK_ERROR;
    }

    Py_DECREF(log_string);
    Py_XDECREF(callback_result);
    Py_DECREF(data->console_callback);
  }

  PyGILState_Release(gil_state);

  return result;
}


static int handle_too_many_matches(
    YR_SCAN_CONTEXT* context,
    YR_STRING* string,
    CALLBACK_DATA* data)
{
  PyGILState_STATE gil_state = PyGILState_Ensure();

  PyObject* warning_type = NULL;
  PyObject* string_identifier = NULL;
  PyObject* rule_identifier = NULL;
  PyObject* namespace_identifier = NULL;
  PyObject* rule_string = NULL;
  YR_RULE* rule = NULL;

  int result = CALLBACK_CONTINUE;

  if (data->warnings_callback == NULL)
  {
    char message[200];

    snprintf(
        message,
        sizeof(message),
        "too many matches for string %s in rule \"%s\"",
        string->identifier,
        context->rules->rules_table[string->rule_idx].identifier);

    if (PyErr_WarnEx(PyExc_RuntimeWarning, message, 1) == -1)
      result = CALLBACK_ERROR;
  }
  else
  {
    Py_INCREF(data->warnings_callback);

    string_identifier = PY_STRING(string->identifier);

    if (string_identifier == NULL)
    {
      result = CALLBACK_ERROR;
      goto _exit;
    }

    rule = &context->rules->rules_table[string->rule_idx];
    rule_identifier = PY_STRING(rule->identifier);

    if (rule_identifier == NULL)
    {
      result = CALLBACK_ERROR;
      goto _exit;
    }

    namespace_identifier = PY_STRING(rule->ns->name);

    if (namespace_identifier == NULL)
    {
      result = CALLBACK_ERROR;
      goto _exit;
    }

    rule_string = PyStructSequence_New(&RuleString_Type);

    if (rule_string == NULL)
    {
      result = CALLBACK_ERROR;
      goto _exit;
    }

    PyStructSequence_SET_ITEM(rule_string, 0, namespace_identifier);
    PyStructSequence_SET_ITEM(rule_string, 1, rule_identifier);
    PyStructSequence_SET_ITEM(rule_string, 2, string_identifier);

    // PyStructSequenece steals the reference so we NULL these
    // so that Py_XDECREF() can be used in _exit label
    namespace_identifier = NULL;
    rule_identifier = NULL;
    string_identifier = NULL;

    warning_type = PyLong_FromLong(CALLBACK_MSG_TOO_MANY_MATCHES);

    if (warning_type == NULL)
    {
      result = CALLBACK_ERROR;
      goto _exit;
    }

    PyObject* callback_result = PyObject_CallFunctionObjArgs(
        data->warnings_callback,
        warning_type,
        rule_string,
        NULL);

    if (callback_result != NULL)
    {
      #if PY_MAJOR_VERSION >= 3
      if (PyLong_Check(callback_result))
      #else
      if (PyLong_Check(callback_result) || PyInt_Check(callback_result))
      #endif
      {
        result = (int) PyLong_AsLong(callback_result);
      }
    }
    else
    {
      result = CALLBACK_ERROR;
    }

    Py_XDECREF(callback_result);
  }

_exit:

  Py_XDECREF(namespace_identifier);
  Py_XDECREF(rule_identifier);
  Py_XDECREF(string_identifier);
  Py_XDECREF(rule_string);
  Py_XDECREF(warning_type);
  Py_XDECREF(data->warnings_callback);

  PyGILState_Release(gil_state);

  return result;
}


#define CALLBACK_MATCHES 0x01
#define CALLBACK_NON_MATCHES 0x02
#define CALLBACK_ALL CALLBACK_MATCHES | CALLBACK_NON_MATCHES

int yara_callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  YR_STRING* string;
  YR_MATCH* m;
  YR_META* meta;
  YR_RULE* rule;

  const char* tag;

  PyObject* tag_list = NULL;
  PyObject* string_instance_list = NULL;
  PyObject* string_list = NULL;
  PyObject* meta_list = NULL;
  PyObject* string_match_instance = NULL;
  PyObject* match;
  PyObject* callback_dict;
  PyObject* object;
  PyObject* matches = ((CALLBACK_DATA*) user_data)->matches;
  PyObject* callback = ((CALLBACK_DATA*) user_data)->callback;
  PyObject* callback_result;

  int which = ((CALLBACK_DATA*) user_data)->which;

  switch(message)
  {
  case CALLBACK_MSG_IMPORT_MODULE:
    return handle_import_module(message_data, user_data);

  case CALLBACK_MSG_MODULE_IMPORTED:
    return handle_module_imported(message_data, user_data);

  case CALLBACK_MSG_TOO_MANY_MATCHES:
    return handle_too_many_matches(context, message_data, user_data);

  case CALLBACK_MSG_SCAN_FINISHED:
    return CALLBACK_CONTINUE;

  case CALLBACK_MSG_RULE_NOT_MATCHING:
    // In cases where the rule doesn't match and the user didn't provided a
    // callback function or is not interested in getting notified about
    // non-matches, there's nothing more do to here, keep executing the function
    // if otherwise.

    if (callback == NULL ||
        (which & CALLBACK_NON_MATCHES) != CALLBACK_NON_MATCHES)
      return CALLBACK_CONTINUE;
    break;

  case CALLBACK_MSG_CONSOLE_LOG:
    return handle_console_log(message_data, user_data);
  }

  // At this point we have handled all the other cases of when this callback
  // can be called. The only things left are:
  //
  // 1. A matching rule.
  //
  // 2 A non-matching rule and the user has requested to see non-matching rules.
  //
  // In both cases, we need to create the data that will be either passed back
  // to the python callback or stored in the matches list.

  int result = CALLBACK_CONTINUE;

  rule = (YR_RULE*) message_data;

  PyGILState_STATE gil_state = PyGILState_Ensure();

  tag_list = PyList_New(0);
  string_list = PyList_New(0);
  meta_list = PyDict_New();

  if (tag_list == NULL || string_list == NULL || meta_list == NULL)
  {
    Py_XDECREF(tag_list);
    Py_XDECREF(string_list);
    Py_XDECREF(meta_list);
    PyGILState_Release(gil_state);

    return CALLBACK_ERROR;
  }

  yr_rule_tags_foreach(rule, tag)
  {
    object = PY_STRING(tag);
    PyList_Append(tag_list, object);
    Py_DECREF(object);
  }

  yr_rule_metas_foreach(rule, meta)
  {
    if (meta->type == META_TYPE_INTEGER)
      object = Py_BuildValue("i", meta->integer);
    else if (meta->type == META_TYPE_BOOLEAN)
      object = PyBool_FromLong((long) meta->integer);
    else
      object = PY_STRING(meta->string);

    if (((CALLBACK_DATA*) user_data)->allow_duplicate_metadata){
      // Check if we already have an array under this key
      PyObject* existing_item = PyDict_GetItemString(meta_list, meta->identifier);
      // Append object to existing list
      if (existing_item)
        PyList_Append(existing_item, object);
      else{
        //Otherwise, instantiate array and append object as first item
        PyObject* new_list = PyList_New(0);
        PyList_Append(new_list, object);
        PyDict_SetItemString(meta_list, meta->identifier, new_list);
        Py_DECREF(new_list);
      }
    }
    else{
      PyDict_SetItemString(meta_list, meta->identifier, object);
      Py_DECREF(object);
    }
  }

  yr_rule_strings_foreach(rule, string)
  {
    // If this string is not a match, skip it. We have to check for this here
    // and not rely on it in yr_string_matches_foreach macro because we need
    // to create the string match instance list before we make the items that
    // go in it.
    if (context->matches[string->idx].head == NULL)
      continue;

    string_instance_list = PyList_New(0);

    if (string_instance_list == NULL)
    {
        PyErr_Format(PyExc_TypeError, "out of memory");
        return CALLBACK_ERROR;
    }


    yr_string_matches_foreach(context, string, m)
    {
      object = PyBytes_FromStringAndSize((char*) m->data, m->data_length);

      string_match_instance = StringMatchInstance_NEW(
          m->base + m->offset,
          object,
          m->match_length,
          m->xor_key);

      if (string_match_instance == NULL)
      {
        Py_DECREF(object);
        PyErr_Format(PyExc_TypeError, "out of memory");
        return CALLBACK_ERROR;
      }

      PyList_Append(string_instance_list, string_match_instance);

      Py_DECREF(object);
      Py_DECREF(string_match_instance);
    }

    object = StringMatch_NEW(
        string->identifier,
        string->flags,
        string_instance_list);

    if (object == NULL)
    {
        PyErr_Format(PyExc_TypeError, "out of memory");
        return CALLBACK_ERROR;
    }


    Py_DECREF(string_instance_list);

    PyList_Append(string_list, object);
    Py_DECREF(object);
  }

  if (message == CALLBACK_MSG_RULE_MATCHING)
  {
    match = Match_NEW(
        rule->identifier,
        rule->ns->name,
        tag_list,
        meta_list,
        string_list);

    if (match != NULL)
    {
      PyList_Append(matches, match);
      Py_DECREF(match);
    }
    else
    {
      Py_DECREF(tag_list);
      Py_DECREF(string_list);
      Py_DECREF(meta_list);
      PyGILState_Release(gil_state);

      return CALLBACK_ERROR;
    }
  }

  if (callback != NULL &&
      ((message == CALLBACK_MSG_RULE_MATCHING && (which & CALLBACK_MATCHES)) ||
       (message == CALLBACK_MSG_RULE_NOT_MATCHING && (which & CALLBACK_NON_MATCHES))))
  {
    Py_INCREF(callback);

    callback_dict = PyDict_New();

    object = PyBool_FromLong(message == CALLBACK_MSG_RULE_MATCHING);
    PyDict_SetItemString(callback_dict, "matches", object);
    Py_DECREF(object);

    object = PY_STRING(rule->identifier);
    PyDict_SetItemString(callback_dict, "rule", object);
    Py_DECREF(object);

    object = PY_STRING(rule->ns->name);
    PyDict_SetItemString(callback_dict, "namespace", object);
    Py_DECREF(object);

    PyDict_SetItemString(callback_dict, "tags", tag_list);
    PyDict_SetItemString(callback_dict, "meta", meta_list);
    PyDict_SetItemString(callback_dict, "strings", string_list);

    callback_result = PyObject_CallFunctionObjArgs(
        callback,
        callback_dict,
        NULL);

    if (callback_result != NULL)
    {
      #if PY_MAJOR_VERSION >= 3
      if (PyLong_Check(callback_result))
      #else
      if (PyLong_Check(callback_result) || PyInt_Check(callback_result))
      #endif
      {
        result = (int) PyLong_AsLong(callback_result);
      }

      Py_DECREF(callback_result);
    }
    else
    {
      result = CALLBACK_ERROR;
    }

    Py_DECREF(callback_dict);
    Py_DECREF(callback);
  }

  Py_DECREF(tag_list);
  Py_DECREF(string_list);
  Py_DECREF(meta_list);
  PyGILState_Release(gil_state);

  return result;
}


/* YR_STREAM read method for "file-like objects" */

static size_t flo_read(
    void* ptr,
    size_t size,
    size_t count,
    void* user_data)
{
  size_t i;

  for (i = 0; i < count; i++)
  {
    PyGILState_STATE gil_state = PyGILState_Ensure();

    PyObject* bytes = PyObject_CallMethod(
        (PyObject*) user_data, "read", "n", (Py_ssize_t) size);

    if (bytes == NULL) 
    {
      PyGILState_Release(gil_state);
      return i;
    }

    Py_ssize_t len;
    char* buffer;

    int result = PyBytes_AsStringAndSize(bytes, &buffer, &len);

    if (result == -1 || (size_t) len < size)
    {
      Py_DECREF(bytes);
      PyGILState_Release(gil_state);
      return i;
    }

    memcpy((char*) ptr + i * size, buffer, size);

    Py_DECREF(bytes);
    PyGILState_Release(gil_state);
  }

  return count;
}


/* YR_STREAM write method for "file-like objects" */

static size_t flo_write(
    const void* ptr,
    size_t size,
    size_t count,
    void* user_data)
{
  size_t i;

  for (i = 0; i < count; i++)
  {
    PyGILState_STATE gil_state = PyGILState_Ensure();

    PyObject* result = PyObject_CallMethod(
    #if PY_MAJOR_VERSION >= 3
        (PyObject*) user_data, "write", "y#", (char*) ptr + i * size, size);
    #else
        (PyObject*) user_data, "write", "s#", (char*) ptr + i * size, size);
    #endif

    Py_XDECREF(result);
    PyGILState_Release(gil_state);

    if (result == NULL)
      return i;
  }

  return count;
}


PyObject* handle_error(
    int error,
    char* extra)
{
  switch(error)
  {
    case ERROR_COULD_NOT_ATTACH_TO_PROCESS:
      return PyErr_Format(
          YaraError,
          "access denied");
    case ERROR_INSUFFICIENT_MEMORY:
      return PyErr_NoMemory();
    case ERROR_COULD_NOT_OPEN_FILE:
      return PyErr_Format(
          YaraError,
          "could not open file \"%s\"",
          extra);
    case ERROR_COULD_NOT_MAP_FILE:
      return PyErr_Format(
          YaraError,
          "could not map file \"%s\" into memory",
          extra);
    case ERROR_INVALID_FILE:
      return PyErr_Format(
          YaraError,
          "invalid rules file \"%s\"",
          extra);
    case ERROR_CORRUPT_FILE:
      return PyErr_Format(
          YaraError,
          "corrupt rules file \"%s\"",
          extra);
    case ERROR_SCAN_TIMEOUT:
      return PyErr_Format(
          YaraTimeoutError,
          "scanning timed out");
    case ERROR_INVALID_EXTERNAL_VARIABLE_TYPE:
      return PyErr_Format(
          YaraError,
          "external variable \"%s\" was already defined with a different type",
          extra);
    case ERROR_UNSUPPORTED_FILE_VERSION:
      return PyErr_Format(
          YaraError,
          "rules file \"%s\" is incompatible with this version of YARA",
          extra);
    default:
      return PyErr_Format(
          YaraError,
          "internal error: %d",
          error);
  }
}


int process_compile_externals(
    PyObject* externals,
    YR_COMPILER* compiler)
{
  PyObject* key;
  PyObject* value;
  Py_ssize_t pos = 0;

  char* identifier = NULL;
  int result;

  while (PyDict_Next(externals, &pos, &key, &value))
  {
    if (!PY_STRING_CHECK(key)) {
      PyErr_Format(
          PyExc_TypeError,
          "keys of externals dict must be strings");

      return ERROR_INVALID_ARGUMENT;
    }
    identifier = PY_STRING_TO_C(key);

    if (PyBool_Check(value))
    {
      result = yr_compiler_define_boolean_variable(
          compiler,
          identifier,
          PyObject_IsTrue(value));
    }
#if PY_MAJOR_VERSION >= 3
    else if (PyLong_Check(value))
#else
    else if (PyLong_Check(value) || PyInt_Check(value))
#endif
    {
      result = yr_compiler_define_integer_variable(
          compiler,
          identifier,
          PyLong_AsLongLong(value));
    }
    else if (PyFloat_Check(value))
    {
      result = yr_compiler_define_float_variable(
          compiler,
          identifier,
          PyFloat_AsDouble(value));
    }
    else if (PY_STRING_CHECK(value))
    {
      char* str = PY_STRING_TO_C(value);

      if (str == NULL)
        return ERROR_INVALID_ARGUMENT;

      result = yr_compiler_define_string_variable(
          compiler, identifier, str);
    }
    else
    {
      PyErr_Format(
          PyExc_TypeError,
          "external values must be of type integer, float, boolean or string");

      return ERROR_INVALID_ARGUMENT;
    }

    if (result != ERROR_SUCCESS)
    {
      handle_error(result, identifier);
      return result;
    }
  }

  return ERROR_SUCCESS;
}


int process_match_externals(
    PyObject* externals,
    YR_SCANNER* scanner)
{
  PyObject* key;
  PyObject* value;
  Py_ssize_t pos = 0;

  char* identifier = NULL;
  int result;

  while (PyDict_Next(externals, &pos, &key, &value))
  {
    if (!PY_STRING_CHECK(key)) {
      PyErr_Format(
          PyExc_TypeError,
          "keys of externals dict must be strings");

      return ERROR_INVALID_ARGUMENT;
    }
    identifier = PY_STRING_TO_C(key);

    if (PyBool_Check(value))
    {
      result = yr_scanner_define_boolean_variable(
          scanner,
          identifier,
          PyObject_IsTrue(value));
    }
#if PY_MAJOR_VERSION >= 3
    else if (PyLong_Check(value))
#else
    else if (PyLong_Check(value) || PyInt_Check(value))
#endif
    {
      result = yr_scanner_define_integer_variable(
          scanner,
          identifier,
          PyLong_AsLongLong(value));
    }
    else if (PyFloat_Check(value))
    {
      result = yr_scanner_define_float_variable(
          scanner,
          identifier,
          PyFloat_AsDouble(value));
    }
    else if (PY_STRING_CHECK(value))
    {
      char* str = PY_STRING_TO_C(value);

      if (str == NULL)
        return ERROR_INVALID_ARGUMENT;

      result = yr_scanner_define_string_variable(
          scanner, identifier, str);
    }
    else
    {
      PyErr_Format(
          PyExc_TypeError,
          "external values must be of type integer, float, boolean or string");

      return ERROR_INVALID_ARGUMENT;
    }

    // yr_scanner_define_xxx_variable returns ERROR_INVALID_ARGUMENT if the
    // variable wasn't previously defined in the compilation phase. Ignore
    // those errors because we don't want the "scan" method being aborted
    // because of the "externals" dictionary having more keys than those used
    // during compilation.

    if (result != ERROR_SUCCESS &&
        result != ERROR_INVALID_ARGUMENT)
    {
      handle_error(result, identifier);
      return result;
    }
  }

  return ERROR_SUCCESS;
}


static PyObject* Match_NEW(
    const char* rule,
    const char* ns,
    PyObject* tags,
    PyObject* meta,
    PyObject* strings)
{
  Match* object = PyObject_NEW(Match, &Match_Type);

  if (object != NULL)
  {
    object->rule = PY_STRING(rule);
    object->ns = PY_STRING(ns);
    object->tags = tags;
    object->meta = meta;
    object->strings = strings;

    Py_INCREF(tags);
    Py_INCREF(meta);
    Py_INCREF(strings);
  }

  return (PyObject*) object;
}


static void Match_dealloc(
    PyObject* self)
{
  Match* object = (Match*) self;

  Py_DECREF(object->rule);
  Py_DECREF(object->ns);
  Py_DECREF(object->tags);
  Py_DECREF(object->meta);
  Py_DECREF(object->strings);

  PyObject_Del(self);
}


static PyObject* Match_repr(
    PyObject* self)
{
  Match* object = (Match*) self;
  Py_INCREF(object->rule);
  return object->rule;
}


static PyObject* Match_getattro(
    PyObject* self,
    PyObject* name)
{
  return PyObject_GenericGetAttr(self, name);
}


static PyObject* Match_richcompare(
    PyObject* self,
    PyObject* other,
    int op)
{
  PyObject* result = NULL;

  Match* a = (Match*) self;
  Match* b = (Match*) other;

  if(PyObject_TypeCheck(other, &Match_Type))
  {
    switch(op)
    {
    case Py_EQ:
      if (PyObject_RichCompareBool(a->rule, b->rule, Py_EQ) &&
          PyObject_RichCompareBool(a->ns, b->ns, Py_EQ))
        result = Py_True;
      else
        result = Py_False;

      Py_INCREF(result);
      break;

    case Py_NE:
      if (PyObject_RichCompareBool(a->rule, b->rule, Py_NE) ||
          PyObject_RichCompareBool(a->ns, b->ns, Py_NE))
          result = Py_True;
      else
          result = Py_False;

      Py_INCREF(result);
      break;

    case Py_LT:
    case Py_LE:
    case Py_GT:
    case Py_GE:
      if (PyObject_RichCompareBool(a->rule, b->rule, Py_EQ))
        result = PyObject_RichCompare(a->ns, b->ns, op);
      else
        result = PyObject_RichCompare(a->rule, b->rule, op);

      break;
    }
  }
  else
  {
    result = PyErr_Format(
        PyExc_TypeError,
        "'Match' objects must be compared with objects of the same class");
  }

  return result;
}


static Py_hash_t Match_hash(
    PyObject* self)
{
  Match* match = (Match*) self;
  return PyObject_Hash(match->rule) + PyObject_Hash(match->ns);
}


////////////////////////////////////////////////////////////////////////////////


static PyObject* StringMatch_NEW(
    const char* identifier,
    uint64_t flags,
    PyObject* instance_list)
{
  StringMatch* object = PyObject_NEW(StringMatch, &StringMatch_Type);

  if (object != NULL)
  {
    object->identifier = PY_STRING(identifier);
    object->flags = flags;
    object->instances = instance_list;

    Py_INCREF(instance_list);
  }

  return (PyObject*) object;
}


static void StringMatch_dealloc(
    PyObject* self)
{
  StringMatch* object = (StringMatch*) self;

  Py_DECREF(object->identifier);
  Py_DECREF(object->instances);

  PyObject_Del(self);
}


static PyObject* StringMatch_repr(
    PyObject* self)
{
  StringMatch* object = (StringMatch*) self;
  Py_INCREF(object->identifier);
  return object->identifier;
}


static PyObject* StringMatch_getattro(
    PyObject* self,
    PyObject* name)
{
  return PyObject_GenericGetAttr(self, name);
}


// Hashing on just identifiers can be tricky as there can be duplicate
// identifiers between rules and there are anonymous strings too. Be careful
// when using this!
static Py_hash_t StringMatch_hash(
    PyObject* self)
{
  return PyObject_Hash(((StringMatch*) self)->identifier);
}


static PyObject* StringMatch_is_xor(
    PyObject* self,
    PyObject* args)
{
  if (((StringMatch*) self)->flags & STRING_FLAGS_XOR)
    Py_RETURN_TRUE;

  Py_RETURN_FALSE;
}


////////////////////////////////////////////////////////////////////////////////


static PyObject* StringMatchInstance_NEW(
    uint64_t offset,
    PyObject* matched_data,
    int32_t match_length,
    uint8_t xor_key)
{
  StringMatchInstance* object = PyObject_NEW(StringMatchInstance, &StringMatchInstance_Type);

  if (object != NULL)
  {
    object->offset = PyLong_FromLongLong(offset);
    object->matched_data = matched_data;
    object->matched_length = PyLong_FromLong(match_length);
    object->xor_key = PyLong_FromUnsignedLong((uint32_t) xor_key);

    Py_INCREF(matched_data);
  }

  return (PyObject*) object;
}


static void StringMatchInstance_dealloc(
    PyObject* self)
{
  StringMatchInstance* object = (StringMatchInstance*) self;

  Py_DECREF(object->offset);
  Py_DECREF(object->matched_data);
  Py_DECREF(object->matched_length);
  Py_DECREF(object->xor_key);

  PyObject_Del(self);
}


static PyObject* StringMatchInstance_repr(
    PyObject* self)
{
  StringMatchInstance* object = (StringMatchInstance*) self;
  return PyCodec_Decode(object->matched_data, "utf-8", "backslashreplace");
}


static PyObject* StringMatchInstance_getattro(
    PyObject* self,
    PyObject* name)
{
  return PyObject_GenericGetAttr(self, name);
}


static Py_hash_t StringMatchInstance_hash(
    PyObject* self)
{
  return PyObject_Hash(((StringMatchInstance*) self)->matched_data);
}


static PyObject* StringMatchInstance_plaintext(
    PyObject* self,
    PyObject* args)
{
  char* pb;
  Py_ssize_t length;

  StringMatchInstance* instance = (StringMatchInstance*) self;
  uint64_t xor_key = PyLong_AsUnsignedLongLong(instance->xor_key);
  if (xor_key == 0)
  {
      Py_INCREF(instance->matched_data);
      return instance->matched_data;
  }

  int result = PyBytes_AsStringAndSize(instance->matched_data, &pb, &length);
  if (result == -1)
    return NULL;

  // pb points to an internal buffer of the bytes object which we can not
  // modify. Allocate a new buffer, copy the contents over and do the xor, then
  // create a new bytes object to return.
  uint8_t* buf = (uint8_t*) calloc(length, sizeof(uint8_t));
  if (buf == NULL)
    return PyErr_Format(PyExc_TypeError, "Out of memory");

  memcpy(buf, pb, length);
  for (size_t i = 0; i < length; i++) {
    buf[i] = ((uint8_t) pb[i]) ^ xor_key;
  }

  PyObject* object = PyBytes_FromStringAndSize((char*) buf, length);
  free(buf);

  return object;
}


////////////////////////////////////////////////////////////////////////////////


static void Rule_dealloc(
    PyObject* self)
{
  Rule* object = (Rule*) self;
  Py_XDECREF(object->identifier);
  Py_XDECREF(object->tags);
  Py_XDECREF(object->meta);
  Py_XDECREF(object->global);
  Py_XDECREF(object->private);
  PyObject_Del(self);
}

static PyObject* Rule_getattro(
    PyObject* self,
    PyObject* name)
{
  return PyObject_GenericGetAttr(self, name);
}


static Rules* Rules_NEW(void)
{
  Rules* rules = PyObject_NEW(Rules, &Rules_Type);

  if (rules != NULL)
  {
    rules->rules = NULL;
    rules->externals = NULL;
    rules->warnings = NULL;
  }

  return rules;
}

static void Rules_dealloc(
    PyObject* self)
{
  Rules* object = (Rules*) self;

  Py_XDECREF(object->externals);
  Py_XDECREF(object->warnings);

  if (object->rules != NULL)
    yr_rules_destroy(object->rules);

  PyObject_Del(self);
}

static PyObject* Rules_next(
    PyObject* self)
{
  PyObject* tag_list;
  PyObject* object;
  PyObject* meta_list;

  YR_META* meta;
  const char* tag;

  Rule* rule;
  Rules* rules = (Rules *) self;

  // Generate new Rule object based upon iter_current_rule and increment
  // iter_current_rule.

  if (RULE_IS_NULL(rules->iter_current_rule))
  {
    rules->iter_current_rule = rules->rules->rules_table;
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  rule = PyObject_NEW(Rule, &Rule_Type);
  tag_list = PyList_New(0);
  meta_list = PyDict_New();

  if (rule != NULL && tag_list != NULL && meta_list != NULL)
  {
    yr_rule_tags_foreach(rules->iter_current_rule, tag)
    {
      object = PY_STRING(tag);
      PyList_Append(tag_list, object);
      Py_DECREF(object);
    }

    yr_rule_metas_foreach(rules->iter_current_rule, meta)
    {
      if (meta->type == META_TYPE_INTEGER)
        object = Py_BuildValue("i", meta->integer);
      else if (meta->type == META_TYPE_BOOLEAN)
        object = PyBool_FromLong((long) meta->integer);
      else
        object = PY_STRING(meta->string);

        PyDict_SetItemString(meta_list, meta->identifier, object);
        Py_DECREF(object);

    }

    rule->global = PyBool_FromLong(rules->iter_current_rule->flags & RULE_FLAGS_GLOBAL);
    rule->private = PyBool_FromLong(rules->iter_current_rule->flags & RULE_FLAGS_PRIVATE);
    rule->identifier = PY_STRING(rules->iter_current_rule->identifier);
    rule->tags = tag_list;
    rule->meta = meta_list;
    rules->iter_current_rule++;
    return (PyObject*) rule;
  }
  else
  {
    Py_XDECREF(tag_list);
    Py_XDECREF(meta_list);
    return PyErr_Format(PyExc_TypeError, "Out of memory");
  }
}

static PyObject* Rules_match(
    PyObject* self,
    PyObject* args,
    PyObject* keywords)
{
  static char* kwlist[] = {
      "filepath", "pid", "data", "externals",
      "callback", "fast", "timeout", "modules_data",
      "modules_callback", "which_callbacks", "warnings_callback",
      "console_callback", "allow_duplicate_metadata", NULL
      };

  char* filepath = NULL;
  Py_buffer data = {0};

  int pid = -1;
  int timeout = 0;
  int error = ERROR_SUCCESS;

  PyObject* externals = NULL;
  PyObject* fast = NULL;

  Rules* object = (Rules*) self;

  YR_SCANNER* scanner;
  CALLBACK_DATA callback_data;

  callback_data.matches = NULL;
  callback_data.callback = NULL;
  callback_data.modules_data = NULL;
  callback_data.modules_callback = NULL;
  callback_data.warnings_callback = NULL;
  callback_data.console_callback = NULL;
  callback_data.which = CALLBACK_ALL;
  callback_data.allow_duplicate_metadata = false;

  if (PyArg_ParseTupleAndKeywords(
        args,
        keywords,
        "|sis*OOOiOOiOOb",
        kwlist,
        &filepath,
        &pid,
        &data,
        &externals,
        &callback_data.callback,
        &fast,
        &timeout,
        &callback_data.modules_data,
        &callback_data.modules_callback,
        &callback_data.which,
        &callback_data.warnings_callback,
        &callback_data.console_callback,
        &callback_data.allow_duplicate_metadata))
  {
    if (filepath == NULL && data.buf == NULL && pid == -1)
    {
      return PyErr_Format(
          PyExc_TypeError,
          "match() takes at least one argument");
    }

    if (callback_data.callback != NULL)
    {
      if (!PyCallable_Check(callback_data.callback))
      {
        PyBuffer_Release(&data);
        return PyErr_Format(
            PyExc_TypeError,
            "'callback' must be callable");
      }
    }

    if (callback_data.modules_callback != NULL)
    {
      if (!PyCallable_Check(callback_data.modules_callback))
      {
        PyBuffer_Release(&data);
        return PyErr_Format(
            PyExc_TypeError,
            "'modules_callback' must be callable");
      }
    }

    if (callback_data.warnings_callback != NULL)
    {
      if (!PyCallable_Check(callback_data.warnings_callback))
      {
        PyBuffer_Release(&data);
        return PyErr_Format(
            PyExc_TypeError,
            "'warnings_callback' must be callable");
      }
    }

    if (callback_data.console_callback != NULL)
    {
      if (!PyCallable_Check(callback_data.console_callback))
      {
        PyBuffer_Release(&data);
        return PyErr_Format(
            PyExc_TypeError,
            "'console_callback' must be callable");
      }
    }

    if (callback_data.modules_data != NULL)
    {
      if (!PyDict_Check(callback_data.modules_data))
      {
        PyBuffer_Release(&data);
        return PyErr_Format(
            PyExc_TypeError,
            "'modules_data' must be a dictionary");
      }
    }

    if (callback_data.allow_duplicate_metadata == NULL)
      callback_data.allow_duplicate_metadata = false;

    if (yr_scanner_create(object->rules, &scanner) != 0)
    {
      return PyErr_Format(
          PyExc_Exception,
          "could not create scanner");
    }

    if (externals != NULL && externals != Py_None)
    {
      if (PyDict_Check(externals))
      {
        if (process_match_externals(externals, scanner) != ERROR_SUCCESS)
        {
          PyBuffer_Release(&data);
          yr_scanner_destroy(scanner);
          return NULL;
        }
      }
      else
      {
        PyBuffer_Release(&data);
        yr_scanner_destroy(scanner);
        return PyErr_Format(
            PyExc_TypeError,
            "'externals' must be a dictionary");
      }
    }

    if (fast != NULL && PyObject_IsTrue(fast) == 1)
    {
      yr_scanner_set_flags(scanner, SCAN_FLAGS_FAST_MODE);
    }

    yr_scanner_set_timeout(scanner, timeout);
    yr_scanner_set_callback(scanner, yara_callback, &callback_data);

    if (filepath != NULL)
    {
      callback_data.matches = PyList_New(0);

      Py_BEGIN_ALLOW_THREADS

      error = yr_scanner_scan_file(scanner, filepath);

      Py_END_ALLOW_THREADS
    }
    else if (data.buf != NULL)
    {
      callback_data.matches = PyList_New(0);

      Py_BEGIN_ALLOW_THREADS

      error = yr_scanner_scan_mem(
          scanner,
          (unsigned char*) data.buf,
          (size_t) data.len);

      Py_END_ALLOW_THREADS
    }
    else if (pid != -1)
    {
      callback_data.matches = PyList_New(0);

      Py_BEGIN_ALLOW_THREADS

      error = yr_scanner_scan_proc(scanner, pid);

      Py_END_ALLOW_THREADS
    }

    PyBuffer_Release(&data);
    yr_scanner_destroy(scanner);

    if (error != ERROR_SUCCESS)
    {
      Py_DECREF(callback_data.matches);

      if (error != ERROR_CALLBACK_ERROR)
      {
        if (filepath != NULL)
        {
          handle_error(error, filepath);
        }
        else if (pid != -1)
        {
          handle_error(error, "<proc>");
        }
        else
        {
          handle_error(error, "<data>");
        }

        #ifdef PROFILING_ENABLED
        PyObject* exception = PyErr_Occurred();

        if (exception != NULL && error == ERROR_SCAN_TIMEOUT)
        {
          PyObject_SetAttrString(
              exception,
              "profiling_info",
              Rules_profiling_info(self, NULL));
        }
        #endif
      }

      return NULL;
    }
  }

  return callback_data.matches;
}


static PyObject* Rules_save(
    PyObject* self,
    PyObject* args,
    PyObject* keywords)
{
  static char* kwlist[] = {
      "filepath", "file",  NULL
      };

  char* filepath = NULL;
  PyObject* file = NULL;
  Rules* rules = (Rules*) self;

  int error;

  if (!PyArg_ParseTupleAndKeywords(
      args,
      keywords,
      "|sO",
      kwlist,
      &filepath,
      &file))
  {
    return NULL;
  }

  if (filepath != NULL)
  {
    Py_BEGIN_ALLOW_THREADS
    error = yr_rules_save(rules->rules, filepath);
    Py_END_ALLOW_THREADS

    if (error != ERROR_SUCCESS)
      return handle_error(error, filepath);
  }
  else if (file != NULL && PyObject_HasAttrString(file, "write"))
  {
    YR_STREAM stream;

    stream.user_data = file;
    stream.write = flo_write;

    Py_BEGIN_ALLOW_THREADS;
    error = yr_rules_save_stream(rules->rules, &stream);
    Py_END_ALLOW_THREADS;

    if (error != ERROR_SUCCESS)
      return handle_error(error, "<file-like-object>");
  }
  else
  {
    return PyErr_Format(
      PyExc_TypeError,
      "load() expects either a file path or a file-like object");
  }

  Py_RETURN_NONE;
}


static PyObject* Rules_profiling_info(
    PyObject* self,
    PyObject* args)
{

#ifdef PROFILING_ENABLED
  PyObject* object;
  PyObject* result;

  YR_RULES* rules = ((Rules*) self)->rules;
  YR_RULE* rule;
  YR_STRING* string;

  char key[512];
  uint64_t clock_ticks;

  result = PyDict_New();

  yr_rules_foreach(rules, rule)
  {
    clock_ticks = rule->clock_ticks;

    yr_rule_strings_foreach(rule, string)
    {
      clock_ticks += string->clock_ticks;
    }

    snprintf(key, sizeof(key), "%s:%s", rule->ns->name, rule->identifier);

    object = PyLong_FromLongLong(clock_ticks);
    PyDict_SetItemString(result, key, object);
    Py_DECREF(object);
  }

  return result;
#else
  return PyErr_Format(YaraError, "libyara compiled without profiling support");
#endif
}


static PyObject* Rules_getattro(
    PyObject* self,
    PyObject* name)
{
  return PyObject_GenericGetAttr(self, name);
}


void raise_exception_on_error(
    int error_level,
    const char* file_name,
    int line_number,
    const YR_RULE* rule,
    const char* message,
    void* user_data)
{
  PyGILState_STATE gil_state = PyGILState_Ensure();

  if (error_level == YARA_ERROR_LEVEL_ERROR)
  {
    if (file_name != NULL)
      PyErr_Format(
          YaraSyntaxError,
          "%s(%d): %s",
          file_name,
          line_number,
          message);
    else
      PyErr_Format(
          YaraSyntaxError,
          "line %d: %s",
          line_number,
          message);
  }
  else
  {
    PyObject* warnings = (PyObject*)user_data;
    PyObject* warning_msg;
    if (file_name != NULL)
      warning_msg = PY_STRING_FORMAT(
          "%s(%d): %s",
          file_name,
          line_number,
          message);
    else
      warning_msg = PY_STRING_FORMAT(
          "line %d: %s",
          line_number,
          message);
    PyList_Append(warnings, warning_msg);
    Py_DECREF(warning_msg);
  }

  PyGILState_Release(gil_state);
}


////////////////////////////////////////////////////////////////////////////////

const char* yara_include_callback(
    const char* include_name,
    const char* calling_rule_filename,
    const char* calling_rule_namespace,
    void* user_data)
{
  PyObject* result;
  PyObject* callback = (PyObject*) user_data;
  PyObject* py_incl_name = NULL;
  PyObject* py_calling_fn = NULL;
  PyObject* py_calling_ns = NULL;
  PyObject* type = NULL;
  PyObject* value = NULL;
  PyObject* traceback = NULL;

  const char* cstring_result = NULL;

  PyGILState_STATE gil_state = PyGILState_Ensure();

  if (include_name != NULL)
  {
    py_incl_name = PY_STRING(include_name);
  }
  else //safeguard: should never happen for 'include_name'
  {
    py_incl_name = Py_None;
    Py_INCREF(py_incl_name);
  }

  if (calling_rule_filename != NULL)
  {
    py_calling_fn = PY_STRING(calling_rule_filename);
  }
  else
  {
    py_calling_fn = Py_None;
    Py_INCREF(py_calling_fn);
  }

  if (calling_rule_namespace != NULL)
  {
    py_calling_ns = PY_STRING(calling_rule_namespace);
  }
  else
  {
    py_calling_ns = Py_None;
    Py_INCREF(py_calling_ns);
  }

  PyErr_Fetch(&type, &value, &traceback);

  result = PyObject_CallFunctionObjArgs(
      callback,
      py_incl_name,
      py_calling_fn,
      py_calling_ns,
      NULL);

  PyErr_Restore(type, value, traceback);

  Py_DECREF(py_incl_name);
  Py_DECREF(py_calling_fn);
  Py_DECREF(py_calling_ns);

  if (result != NULL && result != Py_None && PY_STRING_CHECK(result))
  {
    //transferring string ownership to C code
    cstring_result = strdup(PY_STRING_TO_C(result));
  }
  else
  {
    if (PyErr_Occurred() == NULL)
    {
      PyErr_Format(PyExc_TypeError,
          "'include_callback' function must return a yara rules as an ascii "
          "or unicode string");
    }
  }

  Py_XDECREF(result);
  PyGILState_Release(gil_state);

  return cstring_result;
}

void yara_include_free(
    const char* result_ptr,
    void* user_data)
{
  if (result_ptr != NULL)
  {
    free((void*) result_ptr);
  }
}

////////////////////////////////////////////////////////////////////////////////

static PyObject* yara_set_config(
    PyObject* self,
    PyObject* args,
    PyObject* keywords)
{

  /*
   * It is recommended that this be kept up to date with the config
   * options present in yara/libyara.c yr_set_configuration(...) - ck
   */
  static char *kwlist[] = {
    "stack_size", "max_strings_per_rule", "max_match_data", NULL};

  unsigned int stack_size = 0;
  unsigned int max_strings_per_rule = 0;
  unsigned int max_match_data = 0;

  int error = 0;

  if (PyArg_ParseTupleAndKeywords(
        args,
        keywords,
        "|III",
        kwlist,
        &stack_size,
        &max_strings_per_rule,
  	&max_match_data))
  {
    if (stack_size != 0)
    {
      error = yr_set_configuration(
          YR_CONFIG_STACK_SIZE,
          &stack_size);

      if ( error != ERROR_SUCCESS)
        return handle_error(error, NULL);
    }

    if (max_strings_per_rule != 0)
    {
      error = yr_set_configuration(
          YR_CONFIG_MAX_STRINGS_PER_RULE,
				  &max_strings_per_rule);

      if (error != ERROR_SUCCESS)
        return handle_error(error, NULL);
    }

    if (max_match_data != 0)
    {
      error = yr_set_configuration(
          YR_CONFIG_MAX_MATCH_DATA,
				  &max_match_data);

      if (error != ERROR_SUCCESS)
        return handle_error(error, NULL);
    }
  }

  Py_RETURN_NONE;
}

static PyObject* yara_compile(
    PyObject* self,
    PyObject* args,
    PyObject* keywords)
{
  static char *kwlist[] = {
    "filepath", "source", "file", "filepaths", "sources",
    "includes", "externals", "error_on_warning", "strict_escape", "include_callback", NULL};

  YR_COMPILER* compiler;
  YR_RULES* yara_rules;
  FILE* fh;

  Rules* rules;

  PyObject* key;
  PyObject* value;
  PyObject* result = NULL;
  PyObject* file = NULL;
  PyObject* sources_dict = NULL;
  PyObject* filepaths_dict = NULL;
  PyObject* includes = NULL;
  PyObject* externals = NULL;
  PyObject* error_on_warning = NULL;
  PyObject* strict_escape = NULL;
  PyObject* include_callback = NULL;

  Py_ssize_t pos = 0;

  int fd;
  int error = 0;

  char* filepath = NULL;
  char* source = NULL;
  char* ns = NULL;
  PyObject* warnings = PyList_New(0);
  bool warning_error = false;

  if (PyArg_ParseTupleAndKeywords(
        args,
        keywords,
        "|ssOOOOOOOO",
        kwlist,
        &filepath,
        &source,
        &file,
        &filepaths_dict,
        &sources_dict,
        &includes,
        &externals,
        &error_on_warning,
        &strict_escape,
        &include_callback))
  {
    char num_args = 0;

    if (filepath != NULL)
      num_args++;

    if (source != NULL)
      num_args++;

    if (file != NULL)
      num_args++;

    if (filepaths_dict != NULL)
      num_args++;

    if (sources_dict != NULL)
      num_args++;

    if (num_args > 1)
      return PyErr_Format(
          PyExc_TypeError,
          "compile is receiving too many arguments");

    error = yr_compiler_create(&compiler);

    if (error != ERROR_SUCCESS)
      return handle_error(error, NULL);

    yr_compiler_set_callback(compiler, raise_exception_on_error, warnings);

    if (error_on_warning != NULL)
    {
      if (PyBool_Check(error_on_warning))
      {
        if (PyObject_IsTrue(error_on_warning) == 1)
        {
          warning_error = true;
        }
      }
      else
      {
        yr_compiler_destroy(compiler);
        return PyErr_Format(
            PyExc_TypeError,
            "'error_on_warning' param must be of boolean type");
      }
    }

    if (strict_escape != NULL)
    {
      if (PyBool_Check(strict_escape))
      {
        compiler->strict_escape = PyObject_IsTrue(strict_escape);
      }
      else
      {
        yr_compiler_destroy(compiler);
        return PyErr_Format(
            PyExc_TypeError,
            "'strict_escape' param must be of boolean type");
      }
    }

    if (includes != NULL)
    {
      if (PyBool_Check(includes))
      {
        // PyObject_IsTrue can return -1 in case of error
        if (PyObject_IsTrue(includes) == 0)
          yr_compiler_set_include_callback(compiler, NULL, NULL, NULL);
      }
      else
      {
        yr_compiler_destroy(compiler);
        return PyErr_Format(
            PyExc_TypeError,
            "'includes' param must be of boolean type");
      }
    }

    if (include_callback != NULL)
    {
      if (!PyCallable_Check(include_callback))
      {
        yr_compiler_destroy(compiler);
        return PyErr_Format(
            PyExc_TypeError,
            "'include_callback' must be callable");
      }

      yr_compiler_set_include_callback(
          compiler,
          yara_include_callback,
          yara_include_free,
          include_callback);
    }

    if (externals != NULL && externals != Py_None)
    {
      if (PyDict_Check(externals))
      {
        if (process_compile_externals(externals, compiler) != ERROR_SUCCESS)
        {
          yr_compiler_destroy(compiler);
          return NULL;
        }
      }
      else
      {
        yr_compiler_destroy(compiler);
        return PyErr_Format(
            PyExc_TypeError,
            "'externals' must be a dictionary");
      }
    }

    Py_XINCREF(include_callback);

    if (filepath != NULL)
    {
      fh = fopen(filepath, "r");

      if (fh != NULL)
      {
        Py_BEGIN_ALLOW_THREADS
        error = yr_compiler_add_file(compiler, fh, NULL, filepath);
        fclose(fh);
        Py_END_ALLOW_THREADS
      }
      else
      {
        result = PyErr_SetFromErrno(YaraError);
      }
    }
    else if (source != NULL)
    {
      Py_BEGIN_ALLOW_THREADS
      error = yr_compiler_add_string(compiler, source, NULL);
      Py_END_ALLOW_THREADS
    }
    else if (file != NULL)
    {
      fd = PyObject_AsFileDescriptor(file);

      if (fd != -1)
      {
        Py_BEGIN_ALLOW_THREADS
        fh = fdopen(dup(fd), "r");
        error = yr_compiler_add_file(compiler, fh, NULL, NULL);
        fclose(fh);
        Py_END_ALLOW_THREADS
      }
      else
      {
        result = PyErr_Format(
            PyExc_TypeError,
            "'file' is not a file object");
      }
    }
    else if (sources_dict != NULL)
    {
      if (PyDict_Check(sources_dict))
      {
        while (PyDict_Next(sources_dict, &pos, &key, &value))
        {
          source = PY_STRING_TO_C(value);
          ns = PY_STRING_TO_C(key);

          if (source != NULL && ns != NULL)
          {
            Py_BEGIN_ALLOW_THREADS
            error = yr_compiler_add_string(compiler, source, ns);
            Py_END_ALLOW_THREADS

            if (error > 0)
              break;
          }
          else
          {
            result = PyErr_Format(
                PyExc_TypeError,
                "keys and values of the 'sources' dictionary must be "
                "of string type");
            break;
          }
        }
      }
      else
      {
        result = PyErr_Format(
            PyExc_TypeError,
            "'sources' must be a dictionary");
      }
    }
    else if (filepaths_dict != NULL)
    {
      if (PyDict_Check(filepaths_dict))
      {
        while (PyDict_Next(filepaths_dict, &pos, &key, &value))
        {
          filepath = PY_STRING_TO_C(value);
          ns = PY_STRING_TO_C(key);

          if (filepath != NULL && ns != NULL)
          {
            fh = fopen(filepath, "r");

            if (fh != NULL)
            {
              Py_BEGIN_ALLOW_THREADS
              error = yr_compiler_add_file(compiler, fh, ns, filepath);
              fclose(fh);
              Py_END_ALLOW_THREADS

              if (error > 0)
                break;
            }
            else
            {
              result = PyErr_SetFromErrno(YaraError);
              break;
            }
          }
          else
          {
            result = PyErr_Format(
                PyExc_TypeError,
                "keys and values of the filepaths dictionary must be of "
                "string type");
            break;
          }
        }
      }
      else
      {
        result = PyErr_Format(
            PyExc_TypeError,
            "filepaths must be a dictionary");
      }
    }
    else
    {
      result = PyErr_Format(
          PyExc_TypeError,
          "compile() takes 1 argument");
    }

    if (warning_error && PyList_Size(warnings) > 0)
    {
      PyErr_SetObject(YaraWarningError, warnings);
    }

    if (PyErr_Occurred() == NULL)
    {
      rules = Rules_NEW();

      if (rules != NULL)
      {
        Py_BEGIN_ALLOW_THREADS
        error = yr_compiler_get_rules(compiler, &yara_rules);
        Py_END_ALLOW_THREADS

        if (error == ERROR_SUCCESS)
        {
          rules->rules = yara_rules;
          rules->iter_current_rule = rules->rules->rules_table;
          rules->warnings = warnings;

          if (externals != NULL && externals != Py_None)
            rules->externals = PyDict_Copy(externals);

          result = (PyObject*) rules;
        }
        else
        {
          Py_DECREF(rules);
          result = handle_error(error, NULL);
        }
      }
      else
      {
        result = handle_error(ERROR_INSUFFICIENT_MEMORY, NULL);
      }
    }

    yr_compiler_destroy(compiler);
    Py_XDECREF(include_callback);
  }

  return result;
}


static PyObject* yara_load(
    PyObject* self,
    PyObject* args,
    PyObject* keywords)
{
  static char* kwlist[] = {
      "filepath", "file",  NULL
      };

  YR_EXTERNAL_VARIABLE* external;

  Rules* rules = NULL;
  PyObject* file = NULL;
  char* filepath = NULL;

  int error;

  if (!PyArg_ParseTupleAndKeywords(
      args,
      keywords,
      "|sO",
      kwlist,
      &filepath,
      &file))
  {
    return NULL;
  }

  if (filepath != NULL)
  {
    rules = Rules_NEW();

    if (rules == NULL)
      return PyErr_NoMemory();

    Py_BEGIN_ALLOW_THREADS;
    error = yr_rules_load(filepath, &rules->rules);
    Py_END_ALLOW_THREADS;

    if (error != ERROR_SUCCESS)
    {
      Py_DECREF(rules);
      return handle_error(error, filepath);
    }
  }
  else if (file != NULL && PyObject_HasAttrString(file, "read"))
  {
    YR_STREAM stream;

    stream.user_data = file;
    stream.read = flo_read;

    rules = Rules_NEW();

    if (rules == NULL)
      return PyErr_NoMemory();

    Py_BEGIN_ALLOW_THREADS;
    error = yr_rules_load_stream(&stream, &rules->rules);
    Py_END_ALLOW_THREADS;

    if (error != ERROR_SUCCESS)
    {
      Py_DECREF(rules);
      return handle_error(error, "<file-like-object>");
    }
  }
  else
  {
    return PyErr_Format(
      PyExc_TypeError,
      "load() expects either a file path or a file-like object");
  }

  external = rules->rules->ext_vars_table;
  rules->iter_current_rule = rules->rules->rules_table;

  if (!EXTERNAL_VARIABLE_IS_NULL(external))
    rules->externals = PyDict_New();

  while (!EXTERNAL_VARIABLE_IS_NULL(external))
  {
    switch(external->type)
    {
      case EXTERNAL_VARIABLE_TYPE_BOOLEAN:
        PyDict_SetItemString(
            rules->externals,
            external->identifier,
            PyBool_FromLong((long) external->value.i));
        break;
      case EXTERNAL_VARIABLE_TYPE_INTEGER:
        PyDict_SetItemString(
            rules->externals,
            external->identifier,
            PyLong_FromLong((long) external->value.i));
        break;
      case EXTERNAL_VARIABLE_TYPE_FLOAT:
        PyDict_SetItemString(
            rules->externals,
            external->identifier,
            PyFloat_FromDouble(external->value.f));
        break;
      case EXTERNAL_VARIABLE_TYPE_STRING:
        PyDict_SetItemString(
            rules->externals,
            external->identifier,
            PY_STRING(external->value.s));
        break;
    }

    external++;
  }

  return (PyObject*) rules;
}


void finalize(void)
{
  yr_finalize();
}


static PyMethodDef yara_methods[] = {
  {
    "compile",
    (PyCFunction) yara_compile,
    METH_VARARGS | METH_KEYWORDS,
    "Compiles a YARA rules file and returns an instance of class Rules"
  },
  {
    "load",
    (PyCFunction) yara_load,
    METH_VARARGS | METH_KEYWORDS,
    "Loads a previously saved YARA rules file and returns an instance of class Rules"
  },
  {
    "set_config",
    (PyCFunction) yara_set_config,
    METH_VARARGS | METH_KEYWORDS,
    "Set a yara configuration variable (stack_size, max_strings_per_rule, or max_match_data)"
  },
  { NULL, NULL }
};

#if PY_MAJOR_VERSION >= 3
#define MOD_ERROR_VAL NULL
#define MOD_SUCCESS_VAL(val) val
#define MOD_INIT(name) PyMODINIT_FUNC PyInit_##name(void)
#define MOD_DEF(ob, name, doc, methods) \
      static struct PyModuleDef moduledef = { \
        PyModuleDef_HEAD_INIT, name, doc, -1, methods, }; \
      ob = PyModule_Create(&moduledef);
#else
#define MOD_ERROR_VAL
#define MOD_SUCCESS_VAL(val)
#define MOD_INIT(name) void init##name(void)
#define MOD_DEF(ob, name, doc, methods) \
      ob = Py_InitModule3(name, methods, doc);
#endif

static PyObject* YaraWarningError_getwarnings(PyObject *self, void* closure)
{
  PyObject *args = PyObject_GetAttrString(self, "args");

  if (!args) {
    return NULL;
  }

  PyObject* ret = PyTuple_GetItem(args, 0);
  Py_XINCREF(ret);
  Py_XDECREF(args);

  return ret;
}

static PyGetSetDef YaraWarningError_getsetters[] = {
  {"warnings", YaraWarningError_getwarnings, NULL, NULL, NULL},
  {NULL}
};


MOD_INIT(yara)
{
  PyObject* m;

  MOD_DEF(m, "yara", YARA_DOC, yara_methods)

  if (m == NULL)
    return MOD_ERROR_VAL;

  /* initialize module variables/constants */

  PyModule_AddIntConstant(m, "CALLBACK_CONTINUE", 0);
  PyModule_AddIntConstant(m, "CALLBACK_ABORT", 1);
  PyModule_AddIntConstant(m, "CALLBACK_MATCHES", CALLBACK_MATCHES);
  PyModule_AddIntConstant(m, "CALLBACK_NON_MATCHES", CALLBACK_NON_MATCHES);
  PyModule_AddIntConstant(m, "CALLBACK_ALL", CALLBACK_ALL);
  PyModule_AddIntConstant(m, "CALLBACK_TOO_MANY_MATCHES", CALLBACK_MSG_TOO_MANY_MATCHES);
  PyModule_AddStringConstant(m, "__version__", YR_VERSION);
  PyModule_AddStringConstant(m, "YARA_VERSION", YR_VERSION);
  PyModule_AddIntConstant(m, "YARA_VERSION_HEX", YR_VERSION_HEX);

#if PYTHON_API_VERSION >= 1007
  YaraError = PyErr_NewException("yara.Error", PyExc_Exception, NULL);
  YaraSyntaxError = PyErr_NewException("yara.SyntaxError", YaraError, NULL);
  YaraTimeoutError = PyErr_NewException("yara.TimeoutError", YaraError, NULL);
  YaraWarningError = PyErr_NewException("yara.WarningError", YaraError, NULL);

  PyTypeObject *YaraWarningError_type = (PyTypeObject *) YaraWarningError;
  PyObject* descr = PyDescr_NewGetSet(YaraWarningError_type, YaraWarningError_getsetters);

  if (PyDict_SetItem(YaraWarningError_type->tp_dict, PyDescr_NAME(descr), descr) < 0)
  {
    Py_DECREF(m);
    Py_DECREF(descr);
  }

  Py_DECREF(descr);
#else
  YaraError = Py_BuildValue("s", "yara.Error");
  YaraSyntaxError = Py_BuildValue("s", "yara.SyntaxError");
  YaraTimeoutError = Py_BuildValue("s", "yara.TimeoutError");
  YaraWarningError = Py_BuildValue("s", "yara.WarningError");
#endif

  if (PyType_Ready(&Rule_Type) < 0)
    return MOD_ERROR_VAL;

  if (PyType_Ready(&Rules_Type) < 0)
    return MOD_ERROR_VAL;

  if (PyType_Ready(&Match_Type) < 0)
    return MOD_ERROR_VAL;

  if (PyType_Ready(&StringMatch_Type) < 0)
    return MOD_ERROR_VAL;

  if (PyType_Ready(&StringMatchInstance_Type) < 0)
    return MOD_ERROR_VAL;

  PyStructSequence_InitType(&RuleString_Type, &RuleString_Desc);

  PyModule_AddObject(m, "Rule", (PyObject*) &Rule_Type);
  PyModule_AddObject(m, "Rules", (PyObject*) &Rules_Type);
  PyModule_AddObject(m, "Match",  (PyObject*) &Match_Type);
  PyModule_AddObject(m, "StringMatch",  (PyObject*) &StringMatch_Type);
  PyModule_AddObject(m, "StringMatchInstance",  (PyObject*) &StringMatchInstance_Type);

  PyModule_AddObject(m, "Error", YaraError);
  PyModule_AddObject(m, "SyntaxError", YaraSyntaxError);
  PyModule_AddObject(m, "TimeoutError", YaraTimeoutError);
  PyModule_AddObject(m, "WarningError", YaraWarningError);

  if (yr_initialize() != ERROR_SUCCESS)
  {
    PyErr_SetString(YaraError, "initialization error");
    return MOD_ERROR_VAL;
  }

  PyObject* module_names_list = PyList_New(0);

  if (module_names_list == NULL)
  {
    PyErr_SetString(YaraError, "module list error");
    return MOD_ERROR_VAL;
  }

  for (YR_MODULE* module = yr_modules_get_table(); module->name != NULL; module++)
  {
    PyObject* module_name = PY_STRING(module->name);
    if (module_name == NULL)
    {
      PyErr_SetString(YaraError, "module name error");
      return MOD_ERROR_VAL;
    }
    if (PyList_Append(module_names_list, module_name) < 0)
    {
      PyErr_SetString(YaraError, "module name error");
      return MOD_ERROR_VAL;
    }
  }
  PyModule_AddObject(m, "modules", module_names_list);

  Py_AtExit(finalize);

  return MOD_SUCCESS_VAL(m);
}
