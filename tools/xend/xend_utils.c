/******************************************************************************
 * xend_utils.c
 * 
 * Copyright (c) 2004, K A Fraser
 */

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <xc.h>
#include <asm-xeno/control_if.h>

/* NB. The following should be kept in sync with the kernel's evtchn driver. */
#define EVTCHN_DEV_NAME  "/dev/xen/evtchn"
#define EVTCHN_DEV_MAJOR 10
#define EVTCHN_DEV_MINOR 200
#define PORT_NORMAL     0x0000   /* A standard event notification.      */ 
#define PORT_DISCONNECT 0x8000   /* A port-disconnect notification.     */
#define PORTIDX_MASK    0x7fff   /* Strip subtype to obtain port index. */
#define EVTCHN_RESET _IO('E', 1) /* Clear notification buffer. Clear errors. */

/* Size of a machine page frame. */
#define PAGE_SIZE 4096


/*
 * *********************** NOTIFIER ***********************
 */

typedef struct {
    PyObject_HEAD;
    int evtchn_fd;
} xu_notifier_object;

static PyObject *xu_notifier_read(PyObject *self, PyObject *args)
{
    xu_notifier_object *xun = (xu_notifier_object *)self;
    u16 v;
    int bytes;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;
    
    while ( (bytes = read(xun->evtchn_fd, &v, sizeof(v))) == -1 )
    {
        if ( errno == EINTR )
            continue;
        if ( errno == EAGAIN )
            goto none;
        return PyErr_SetFromErrno(PyExc_IOError);
    }
    
    if ( bytes == sizeof(v) )
        return Py_BuildValue("(i,i)", v&PORTIDX_MASK, v&~PORTIDX_MASK);

 none:
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_notifier_clear(PyObject *self, PyObject *args)
{
    xu_notifier_object *xun = (xu_notifier_object *)self;
    u16 v;
    int idx, type;

    if ( !PyArg_ParseTuple(args, "ii", &idx, &type) )
        return NULL;
    
    v = (u16)idx | (u16)type;

    (void)write(xun->evtchn_fd, &v, sizeof(v));

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_notifier_fileno(PyObject *self, PyObject *args)
{
    xu_notifier_object *xun = (xu_notifier_object *)self;
    return PyInt_FromLong(xun->evtchn_fd);
}

static PyMethodDef xu_notifier_methods[] = {
    { "read",
      (PyCFunction)xu_notifier_read,
      METH_VARARGS,
      "Read a (@port, @type) pair.\n" },

    { "clear", 
      (PyCFunction)xu_notifier_clear,
      METH_VARARGS,
      "Clear a (@port, @type) pair.\n" },

    { "fileno", 
      (PyCFunction)xu_notifier_fileno,
      METH_VARARGS,
      "Return the file descriptor for the notification channel.\n" },

    { NULL, NULL, 0, NULL }
};

staticforward PyTypeObject xu_notifier_type;

static PyObject *xu_notifier_new(PyObject *self, PyObject *args)
{
    xu_notifier_object *xun;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    xun = PyObject_New(xu_notifier_object, &xu_notifier_type);

    xun->evtchn_fd = open(EVTCHN_DEV_NAME, O_NONBLOCK|O_RDWR);
    if ( xun->evtchn_fd == -1 )
    {
        PyObject_Del((PyObject *)xun);
        return PyErr_SetFromErrno(PyExc_IOError);
    }

    return (PyObject *)xun;
}

static PyObject *xu_notifier_getattr(PyObject *obj, char *name)
{
    if ( strcmp(name, "DISCONNECT") == 0 )
        return PyInt_FromLong(PORT_DISCONNECT);
    if ( strcmp(name, "NORMAL") == 0 )
        return PyInt_FromLong(PORT_NORMAL);
    return Py_FindMethod(xu_notifier_methods, obj, name);
}

static void xu_notifier_dealloc(PyObject *self)
{
    xu_notifier_object *xun = (xu_notifier_object *)self;
    (void)close(xun->evtchn_fd);
    PyObject_Del(self);
}

static PyTypeObject xu_notifier_type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "notifier",
    sizeof(xu_notifier_object),
    0,
    xu_notifier_dealloc, /* tp_dealloc     */
    NULL,                /* tp_print       */
    xu_notifier_getattr, /* tp_getattr     */
    NULL,                /* tp_setattr     */
    NULL,                /* tp_compare     */
    NULL,                /* tp_repr        */
    NULL,                /* tp_as_number   */
    NULL,                /* tp_as_sequence */
    NULL,                /* tp_as_mapping  */
    NULL                 /* tp_hash        */
};



/*
 * *********************** MESSAGE ***********************
 */

typedef struct {
    PyObject_HEAD;
    control_msg_t msg;
} xu_message_object;

static PyObject *xu_message_append_payload(PyObject *self, PyObject *args)
{
    xu_message_object *xum = (xu_message_object *)self;
    char *str;
    int len;

    if ( !PyArg_ParseTuple(args, "s#", &str, &len) )
        return NULL;

    if ( (len + xum->msg.length) > sizeof(xum->msg.msg) )
    {
        PyErr_SetString(PyExc_RuntimeError, "out of space in control message");
        return NULL;
    }

    memcpy(&xum->msg.msg[xum->msg.length], str, len);
    xum->msg.length += len;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_message_get_payload(PyObject *self, PyObject *args)
{
    xu_message_object *xum = (xu_message_object *)self;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    return PyString_FromStringAndSize(xum->msg.msg, xum->msg.length);
}

static PyObject *xu_message_set_header(PyObject *self, 
                                       PyObject *args, 
                                       PyObject *kwds)
{
    xu_message_object *xum = (xu_message_object *)self;
    int type = -1, subtype = -1, id = -1;

    static char *kwd_list[] = { "type", "subtype", "id", NULL };

    if ( !PyArg_ParseTupleAndKeywords(args, kwds, "|iii", kwd_list,
                                      &type, &subtype, &id) )
        return NULL;

    if ( type != -1 )
        xum->msg.type = (u8)type;
    if ( subtype != -1 )
        xum->msg.subtype = (u8)subtype;
    if ( id != -1 )
        xum->msg.id = (u8)id;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_message_get_header(PyObject *self, PyObject *args)
{
    xu_message_object *xum = (xu_message_object *)self;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    return Py_BuildValue("{s:i,s:i,s:i}",
                         "type",    xum->msg.type,
                         "subtype", xum->msg.subtype,
                         "id",      xum->msg.id);
}

static PyMethodDef xu_message_methods[] = {
    { "append_payload", 
      (PyCFunction)xu_message_append_payload,
      METH_VARARGS,
      "Append @str to the message payload.\n" },

    { "get_payload",
      (PyCFunction)xu_message_get_payload,
      METH_VARARGS,
      "Return the message payload in string form.\n" },

    { "set_header",
      (PyCFunction)xu_message_set_header,
      METH_VARARGS | METH_KEYWORDS,
      "Accepts keywords @type, @subtype, and @id.\n" },

    { "get_header",
      (PyCFunction)xu_message_get_header,
      METH_VARARGS,
      "Returns a dictionary of values for @type, @subtype, and @id.\n" },

    { NULL, NULL, 0, NULL }
};

staticforward PyTypeObject xu_message_type;

static PyObject *xu_message_new(PyObject *self, PyObject *args)
{
    xu_message_object *xum;
    int type, subtype, id;

    if ( !PyArg_ParseTuple(args, "iii", &type, &subtype, &id) )
        return NULL;

    xum = PyObject_New(xu_message_object, &xu_message_type);

    xum->msg.type    = type;
    xum->msg.subtype = subtype;
    xum->msg.id      = id;
    xum->msg.length  = 0;

    return (PyObject *)xum;
}

static PyObject *xu_message_getattr(PyObject *obj, char *name)
{
    xu_message_object *xum;
    if ( strcmp(name, "MAX_PAYLOAD") == 0 )
        return PyInt_FromLong(sizeof(xum->msg.msg));
    return Py_FindMethod(xu_message_methods, obj, name);
}

static void xu_message_dealloc(PyObject *self)
{
    PyObject_Del(self);
}

static PyTypeObject xu_message_type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "message",
    sizeof(xu_message_object),
    0,
    xu_message_dealloc,   /* tp_dealloc     */
    NULL,                /* tp_print       */
    xu_message_getattr,   /* tp_getattr     */
    NULL,                /* tp_setattr     */
    NULL,                /* tp_compare     */
    NULL,                /* tp_repr        */
    NULL,                /* tp_as_number   */
    NULL,                /* tp_as_sequence */
    NULL,                /* tp_as_mapping  */
    NULL                 /* tp_hash        */
};



/*
 * *********************** PORT ***********************
 */

static control_if_t *map_control_interface(int fd, unsigned long pfn)
{
    char *vaddr = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
                       MAP_SHARED, fd, pfn * PAGE_SIZE);
    if ( vaddr == MAP_FAILED )
        return NULL;
    return (control_if_t *)(vaddr + 2048);
}
static void unmap_control_interface(int fd, control_if_t *c)
{
    char *vaddr = (char *)c - 2048;
    (void)munmap(vaddr, PAGE_SIZE);
}

typedef struct {
    PyObject_HEAD;
    int mem_fd;
    int xc_handle;
    u64 remote_dom;
    int local_port, remote_port;
    control_if_t    *interface;
    CONTROL_RING_IDX tx_req_cons, tx_resp_prod;
    CONTROL_RING_IDX rx_req_prod, rx_resp_cons;
} xu_port_object;

static PyObject *port_error;

static PyObject *xu_port_notify(PyObject *self, PyObject *args)
{
    xu_port_object *xup = (xu_port_object *)self;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    (void)xc_evtchn_send(xup->xc_handle, xup->local_port);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_port_read_request(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    xu_message_object *xum;
    CONTROL_RING_IDX   c = xup->tx_req_cons;
    control_if_t      *cif = xup->interface;
    control_msg_t     *cmsg;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( (c == cif->tx_req_prod) || 
         ((c - xup->tx_resp_prod) == CONTROL_RING_SIZE) )
    {
        PyErr_SetString(port_error, "no request to read");
        return NULL;
    }

    cmsg = &cif->tx_ring[MASK_CONTROL_IDX(c)];
    xum = PyObject_New(xu_message_object, &xu_message_type);
    memcpy(&xum->msg, cmsg, sizeof(*cmsg));
    if ( xum->msg.length > sizeof(xum->msg.msg) )
        xum->msg.length = sizeof(xum->msg.msg);
    xup->tx_req_cons++;
    return (PyObject *)xum;
}

static PyObject *xu_port_write_request(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    xu_message_object *xum;
    CONTROL_RING_IDX   p = xup->rx_req_prod;
    control_if_t      *cif = xup->interface;
    control_msg_t     *cmsg;

    if ( !PyArg_ParseTuple(args, "O", (PyObject **)&xum) )
        return NULL;

    if ( !PyObject_TypeCheck((PyObject *)xum, &xu_message_type) )
    {
        PyErr_SetString(PyExc_TypeError, "expected a xend_utils.message");
        return NULL;        
    }

    if ( ((p - xup->rx_resp_cons) == CONTROL_RING_SIZE) )
    {
        PyErr_SetString(port_error, "no space to write request");
        return NULL;
    }

    cmsg = &cif->rx_ring[MASK_CONTROL_IDX(p)];
    memcpy(cmsg, &xum->msg, sizeof(*cmsg));

    xup->rx_req_prod = cif->rx_req_prod = p + 1;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_port_read_response(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    xu_message_object *xum;
    CONTROL_RING_IDX   c = xup->rx_resp_cons;
    control_if_t      *cif = xup->interface;
    control_msg_t     *cmsg;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( (c == cif->rx_resp_prod) || (c == xup->rx_req_prod) )
    {
        PyErr_SetString(port_error, "no response to read");
        return NULL;
    }

    cmsg = &cif->rx_ring[MASK_CONTROL_IDX(c)];
    xum = PyObject_New(xu_message_object, &xu_message_type);
    memcpy(&xum->msg, cmsg, sizeof(*cmsg));
    if ( xum->msg.length > sizeof(xum->msg.msg) )
        xum->msg.length = sizeof(xum->msg.msg);
    xup->rx_resp_cons++;
    return (PyObject *)xum;
}

static PyObject *xu_port_write_response(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    xu_message_object *xum;
    CONTROL_RING_IDX   p = xup->tx_resp_prod;
    control_if_t      *cif = xup->interface;
    control_msg_t     *cmsg;

    if ( !PyArg_ParseTuple(args, "O", (PyObject **)&xum) )
        return NULL;

    if ( !PyObject_TypeCheck((PyObject *)xum, &xu_message_type) )
    {
        PyErr_SetString(PyExc_TypeError, "expected a xend_utils.message");
        return NULL;        
    }

    if ( p == xup->tx_req_cons )
    {
        PyErr_SetString(port_error, "no space to write response");
        return NULL;
    }

    cmsg = &cif->tx_ring[MASK_CONTROL_IDX(p)];
    memcpy(cmsg, &xum->msg, sizeof(*cmsg));

    xup->tx_resp_prod = cif->tx_resp_prod = p + 1;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *xu_port_request_to_read(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    CONTROL_RING_IDX   c = xup->tx_req_cons;
    control_if_t      *cif = xup->interface;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( (c == cif->tx_req_prod) || 
         ((c - xup->tx_resp_prod) == CONTROL_RING_SIZE) )
        return PyInt_FromLong(0);

    return PyInt_FromLong(1);
}

static PyObject *xu_port_space_to_write_request(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    CONTROL_RING_IDX   p = xup->rx_req_prod;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( ((p - xup->rx_resp_cons) == CONTROL_RING_SIZE) )
        return PyInt_FromLong(0);

    return PyInt_FromLong(1);
}

static PyObject *xu_port_response_to_read(PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    CONTROL_RING_IDX   c = xup->rx_resp_cons;
    control_if_t      *cif = xup->interface;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( (c == cif->rx_resp_prod) || (c == xup->rx_req_prod) )
        return PyInt_FromLong(0);

    return PyInt_FromLong(1);
}

static PyObject *xu_port_space_to_write_response(
    PyObject *self, PyObject *args)
{
    xu_port_object    *xup = (xu_port_object *)self;
    CONTROL_RING_IDX   p = xup->tx_resp_prod;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( p == xup->tx_req_cons )
        return PyInt_FromLong(0);

    return PyInt_FromLong(1);
}

static PyMethodDef xu_port_methods[] = {
    { "notify",
      (PyCFunction)xu_port_notify,
      METH_VARARGS,
      "Send a notification to the remote end.\n" },

    { "read_request",
      (PyCFunction)xu_port_read_request,
      METH_VARARGS,
      "Read a request message from the control interface.\n" },

    { "write_request",
      (PyCFunction)xu_port_write_request,
      METH_VARARGS,
      "Write a request message to the control interface.\n" },

    { "read_response",
      (PyCFunction)xu_port_read_response,
      METH_VARARGS,
      "Read a response message from the control interface.\n" },

    { "write_response",
      (PyCFunction)xu_port_write_response,
      METH_VARARGS,
      "Write a response message to the control interface.\n" },

    { "request_to_read",
      (PyCFunction)xu_port_request_to_read,
      METH_VARARGS,
      "Returns TRUE if there is a request message to read.\n" },

    { "space_to_write_request",
      (PyCFunction)xu_port_space_to_write_request,
      METH_VARARGS,
      "Returns TRUE if there is space to write a request message.\n" },

    { "response_to_read",
      (PyCFunction)xu_port_response_to_read,
      METH_VARARGS,
      "Returns TRUE if there is a response message to read.\n" },

    { "space_to_write_response",
      (PyCFunction)xu_port_space_to_write_response,
      METH_VARARGS,
      "Returns TRUE if there is space to write a response message.\n" },

    { NULL, NULL, 0, NULL }
};

staticforward PyTypeObject xu_port_type;

static PyObject *xu_port_new(PyObject *self, PyObject *args)
{
    xu_port_object *xup;
    u64 dom;
    int port1, port2;
    xc_dominfo_t info;

    if ( !PyArg_ParseTuple(args, "L", &dom) )
        return NULL;

    xup = PyObject_New(xu_port_object, &xu_port_type);

    if ( (xup->mem_fd = open("/dev/mem", O_RDWR)) == -1 )
    {
        PyErr_SetString(port_error, "Could not open '/dev/mem'");
        goto fail1;
    }

    if ( (xup->xc_handle = xc_interface_open()) == -1 )
    {
        PyErr_SetString(port_error, "Could not open Xen control interface");
        goto fail2;
    }

    if ( xc_evtchn_open(xup->xc_handle, DOMID_SELF, dom, &port1, &port2) != 0 )
    {
        PyErr_SetString(port_error, "Could not open channel to domain");
        goto fail3;
    }

    if ( (xc_domain_getinfo(xup->xc_handle, dom, 1, &info) != 1) ||
         (info.domid != dom) )
    {
        PyErr_SetString(port_error, "Failed to obtain domain status");
        goto fail4;
    }

    xup->interface = 
        map_control_interface(xup->mem_fd, info.shared_info_frame);
    if ( xup->interface == NULL )
    {
        PyErr_SetString(port_error, "Failed to map domain control interface");
        goto fail4;
    }

    xup->tx_req_cons  = 0;
    xup->tx_resp_prod = 0;
    xup->rx_req_prod  = 0;
    xup->rx_resp_cons = 0;
    xup->remote_dom   = dom;
    xup->local_port   = port1;
    xup->remote_port  = port2;

    return (PyObject *)xup;

    
 fail4:
    (void)xc_evtchn_close(xup->xc_handle, DOMID_SELF, port1);
 fail3:
    (void)xc_interface_close(xup->xc_handle);
 fail2:
    (void)close(xup->mem_fd);
 fail1:
    PyObject_Del((PyObject *)xup);
    return NULL;        
}

static PyObject *xu_port_getattr(PyObject *obj, char *name)
{
    xu_port_object *xup = (xu_port_object *)obj;
    if ( strcmp(name, "local_port") == 0 )
        return PyInt_FromLong(xup->local_port);
    if ( strcmp(name, "remote_port") == 0 )
        return PyInt_FromLong(xup->remote_port);
    if ( strcmp(name, "remote_dom") == 0 )
        return PyLong_FromUnsignedLongLong(xup->remote_dom);
    return Py_FindMethod(xu_port_methods, obj, name);
}

static void xu_port_dealloc(PyObject *self)
{
    xu_port_object *xup = (xu_port_object *)self;
    unmap_control_interface(xup->mem_fd, xup->interface);
    (void)xc_evtchn_close(xup->xc_handle, DOMID_SELF, xup->local_port);
    (void)xc_interface_close(xup->xc_handle);
    (void)close(xup->mem_fd);
    PyObject_Del(self);
}

static PyTypeObject xu_port_type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "port",
    sizeof(xu_port_object),
    0,
    xu_port_dealloc,     /* tp_dealloc     */
    NULL,                /* tp_print       */
    xu_port_getattr,     /* tp_getattr     */
    NULL,                /* tp_setattr     */
    NULL,                /* tp_compare     */
    NULL,                /* tp_repr        */
    NULL,                /* tp_as_number   */
    NULL,                /* tp_as_sequence */
    NULL,                /* tp_as_mapping  */
    NULL                 /* tp_hash        */
};



/*
 * *********************** BUFFER ***********************
 */

#define BUFSZ 65536
#define MASK_BUF_IDX(_i) ((_i)&(BUFSZ-1))
typedef unsigned int BUF_IDX;

typedef struct {
    PyObject_HEAD;
    char        *buf;
    unsigned int prod, cons;
} xu_buffer_object;

static PyObject *__xu_buffer_peek(xu_buffer_object *xub, int max)
{
    PyObject *str1, *str2;
    int len1, len2, c = MASK_BUF_IDX(xub->cons);

    len1 = xub->prod - xub->cons;
    if ( len1 > (BUFSZ - c) ) /* clip to ring wrap */
        len1 = BUFSZ - c;
    if ( len1 > max )         /* clip to specified maximum */
        len1 = max;
    if ( len1 < 0 )           /* sanity */
        len1 = 0;

    if ( (str1 = PyString_FromStringAndSize(&xub->buf[c], len1)) == NULL )
        return NULL;

    if ( (len1 < (xub->prod - xub->cons)) && (len1 < max) )
    {
        len2 = max - len1;
        if ( len2 > MASK_BUF_IDX(xub->prod) )
            len2 = MASK_BUF_IDX(xub->prod);
        if ( len2 > 0 )
        {
            str2 = PyString_FromStringAndSize(&xub->buf[0], len2);
            if ( str2 == NULL )
                return NULL;
            PyString_ConcatAndDel(&str1, str2);
            if ( str1 == NULL )
                return NULL;
        }
    }

    return str1;
}

static PyObject *xu_buffer_peek(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;
    int max = 1024;

    if ( !PyArg_ParseTuple(args, "|i", &max) )
        return NULL;
    
    return __xu_buffer_peek(xub, max);
}

static PyObject *xu_buffer_read(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;
    PyObject *str;
    int max = 1024;

    if ( !PyArg_ParseTuple(args, "|i", &max) )
        return NULL;

    if ( (str = __xu_buffer_peek(xub, max)) != NULL )
        xub->cons += PyString_Size(str);

    return str;
}

static PyObject *xu_buffer_discard(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;
    int max, len;

    if ( !PyArg_ParseTuple(args, "i", &max) )
        return NULL;

    len = xub->prod - xub->cons;
    if ( len > max )
        len = max;
    if ( len < 0 )
        len = 0;

    xub->cons += len;

    return PyInt_FromLong(len);
}

static PyObject *xu_buffer_write(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;
    char *str;
    int len, len1, len2;

    if ( !PyArg_ParseTuple(args, "s#", &str, &len) )
        return NULL;

    len1 = len;
    if ( len1 > (BUFSZ - MASK_BUF_IDX(xub->prod)) )
        len1 = BUFSZ - MASK_BUF_IDX(xub->prod);
    if ( len1 > (BUFSZ - (xub->prod - xub->cons)) )
        len1 = BUFSZ - (xub->prod - xub->cons);

    if ( len1 == 0 )
        return PyInt_FromLong(0);

    memcpy(&xub->buf[MASK_BUF_IDX(xub->prod)], &str[0], len1);
    xub->prod += len1;

    if ( len1 < len )
    {
        len2 = len - len1;
        if ( len2 > (BUFSZ - MASK_BUF_IDX(xub->prod)) )
            len2 = BUFSZ - MASK_BUF_IDX(xub->prod);
        if ( len2 > (BUFSZ - (xub->prod - xub->cons)) )
            len2 = BUFSZ - (xub->prod - xub->cons);
        if ( len2 != 0 )
        {
            memcpy(&xub->buf[MASK_BUF_IDX(xub->prod)], &str[len1], len2);
            xub->prod += len2;
            return PyInt_FromLong(len1 + len2);
        }
    }

    return PyInt_FromLong(len1);
}

static PyObject *xu_buffer_empty(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( xub->cons == xub->prod )
        return PyInt_FromLong(1);

    return PyInt_FromLong(0);
}

static PyObject *xu_buffer_full(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    if ( (xub->prod - xub->cons) == BUFSZ )
        return PyInt_FromLong(1);

    return PyInt_FromLong(0);
}

static PyMethodDef xu_buffer_methods[] = {
    { "peek", 
      (PyCFunction)xu_buffer_peek,
      METH_VARARGS,
      "Peek up to @max bytes from the buffer. Returns a string.\n" },

    { "read", 
      (PyCFunction)xu_buffer_read,
      METH_VARARGS,
      "Read up to @max bytes from the buffer. Returns a string.\n" },

    { "discard", 
      (PyCFunction)xu_buffer_discard,
      METH_VARARGS,
      "Discard up to @max bytes from the buffer. Returns number of bytes.\n" },

    { "write", 
      (PyCFunction)xu_buffer_write,
      METH_VARARGS,
      "Write @string into buffer. Return number of bytes written.\n" },

    { "empty", 
      (PyCFunction)xu_buffer_empty,
      METH_VARARGS,
      "Return TRUE if the buffer is empty.\n" },

    { "full", 
      (PyCFunction)xu_buffer_full,
      METH_VARARGS,
      "Return TRUE if the buffer is full.\n" },

    { NULL, NULL, 0, NULL }
};

staticforward PyTypeObject xu_buffer_type;

static PyObject *xu_buffer_new(PyObject *self, PyObject *args)
{
    xu_buffer_object *xub;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    xub = PyObject_New(xu_buffer_object, &xu_buffer_type);

    if ( (xub->buf = malloc(BUFSZ)) == NULL )
    {
        PyObject_Del((PyObject *)xub);
        return NULL;
    }

    xub->prod = xub->cons = 0;

    return (PyObject *)xub;
}

static PyObject *xu_buffer_getattr(PyObject *obj, char *name)
{
    return Py_FindMethod(xu_buffer_methods, obj, name);
}

static void xu_buffer_dealloc(PyObject *self)
{
    xu_buffer_object *xub = (xu_buffer_object *)self;
    free(xub->buf);
    PyObject_Del(self);
}

static PyTypeObject xu_buffer_type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "buffer",
    sizeof(xu_buffer_object),
    0,
    xu_buffer_dealloc,   /* tp_dealloc     */
    NULL,                /* tp_print       */
    xu_buffer_getattr,   /* tp_getattr     */
    NULL,                /* tp_setattr     */
    NULL,                /* tp_compare     */
    NULL,                /* tp_repr        */
    NULL,                /* tp_as_number   */
    NULL,                /* tp_as_sequence */
    NULL,                /* tp_as_mapping  */
    NULL                 /* tp_hash        */
};



/*
 * *********************** MODULE WRAPPER ***********************
 */

static void handle_child_death(int dummy)
{
    while ( waitpid(-1, NULL, WNOHANG) > 0 )
        continue;
}

static PyObject *xu_autoreap(PyObject *self, PyObject *args)
{
    struct sigaction sa;

    if ( !PyArg_ParseTuple(args, "") )
        return NULL;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_child_death;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    (void)sigaction(SIGCHLD, &sa, NULL);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef xu_methods[] = {
    { "notifier", xu_notifier_new, METH_VARARGS, 
      "Create a new notifier." },
    { "message", xu_message_new, METH_VARARGS, 
      "Create a new communications message." },
    { "port", xu_port_new, METH_VARARGS, 
      "Create a new communications port." },
    { "buffer", xu_buffer_new, METH_VARARGS, 
      "Create a new ring buffer." },
    { "autoreap", xu_autoreap, METH_VARARGS,
      "Ensure that zombie children are automatically reaped by the OS." },
    { NULL, NULL, 0, NULL }
};

PyMODINIT_FUNC initxend_utils(void)
{
    PyObject *m, *d;

    m = Py_InitModule("xend_utils", xu_methods);

    d = PyModule_GetDict(m);
    port_error = PyErr_NewException("xend_utils.PortError", NULL, NULL);
    PyDict_SetItemString(d, "PortError", port_error);
}
