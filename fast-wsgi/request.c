#include "server.h"
#include "request.h"
#include "llhttp.h"
#include "constants.h"

static void set_header(PyObject* headers, PyObject* key, const char* value, size_t length) {
    printf("setting header\n");
    PyObject* item = PyUnicode_FromStringAndSize(value, length);
    PyDict_SetItem(headers, key, item);
    Py_DECREF(key);
    Py_DECREF(item);
}

int on_message_begin(llhttp_t* parser) {
    printf("on message begin\n");
    Request* request = (Request*)parser->data;
    request->headers = PyDict_New();
    return 0;
};

int on_url(llhttp_t* parser, const char* url, size_t length) {
    printf("on url\n");
    Request* request = (Request*)parser->data;
    PyObject* header = Py_BuildValue("s", "PATH_INFO");

    Py_INCREF(header);
    set_header(request->headers, header, url, length);
    Py_DECREF(header);
    return 0;
};

int on_body(llhttp_t* parser, const char* body, size_t length) {
    printf("on body\n");
    Request* request = (Request*)parser->data;
    PyObject* header = Py_BuildValue("s", "body");
    Py_INCREF(header);
    set_header(request->headers, header, body, length);
    Py_DECREF(header);
    return 0;
};

int on_header_field(llhttp_t* parser, const char* header, size_t length) {
    printf("on header field\n");
    PyObject* HTTP_ = PyUnicode_FromString("HTTP_");

    char upperHeader[length];
    for (size_t i = 0; i < length; i++) {
        char current = header[i];
        if (current == '-') {
            upperHeader[i] = '_';
        }
        else {
            upperHeader[i] = toupper(current);
        }
    }
    PyObject* uppercaseHeader = PyUnicode_FromStringAndSize(upperHeader, length);

    current_header = PyUnicode_Concat(HTTP_, uppercaseHeader);
    Py_INCREF(current_header);
    Py_DECREF(uppercaseHeader);
    Py_DECREF(HTTP_);

    return 0;
};

int on_header_value(llhttp_t* parser, const char* value, size_t length) {
    printf("on header value\n");
    Request* request = (Request*)parser->data;
    set_header(request->headers, current_header, value, length);
    Py_DECREF(current_header);
    return 0;
};

PyObject* start_response_call(PyObject* self, PyObject* args, PyObject* kwargs)
{
    StartResponse* response = (StartResponse*)self;
    if (!PyArg_UnpackTuple(args, "start_response", 2, 3, &response->status, &response->headers, &response->exc_info))
        return NULL;

    if (&response->status == NULL) {
        printf("NULL status\n");
        return NULL;
    }
    if (&response->headers == NULL) {
        printf("NULL response_headers\n");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyTypeObject StartResponse_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "start_response",
    sizeof(StartResponse),
    0,
    (destructor)PyObject_FREE,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    start_response_call
};

int on_message_complete(llhttp_t* parser) {
    printf("on message complete\n");
    Request* request = (Request*)parser->data;
    build_wsgi_environ(parser);

    StartResponse* start_response = PyObject_NEW(StartResponse, &StartResponse_Type);

    PyObject* wsgi_response = PyObject_CallFunctionObjArgs(
        wsgi_app, request->headers, start_response, NULL
    );

    build_response(wsgi_response, start_response);

    Py_DECREF(start_response);
    Py_DECREF(wsgi_response);
    return 0;
};

void build_response(PyObject* wsgi_response, StartResponse* response) {
    // TODO: This method needs fixing...

    PyObject* iter = PyObject_GetIter(wsgi_response);
    PyObject* result = PyIter_Next(iter);

    char* buf = "HTTP/1.1";
    asprintf(&buf, "%s %s", buf, "200 OK\r\n");
    printf("Starting adding headers\n");
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(response->headers); ++i) {
        printf("In loop\n");
        // Seg fault here
        PyObject* tuple = PyList_GET_ITEM(response->headers, i);
        printf("Got tuple...\n");
        PyObject* field = PyTuple_GET_ITEM(tuple, 0);
        printf("got field\n");
        PyObject* value = PyTuple_GET_ITEM(tuple, 1);
        printf("Extracted Tuple\n");
        asprintf(&buf, "%s%s", buf, "\r\n");
        printf("Added return to buffer\n");
        asprintf(&buf, "%s%s", buf, PyBytes_AS_STRING(field));
        printf("Added field to buffer\n");
        asprintf(&buf, "%s%s", buf, PyBytes_AS_STRING(value));
        printf("Added to buffer\n");
    }
    printf("Finished loop\n");
    asprintf(&buf, "%sContent-Length: %ld\r\n", buf, strlen(PyBytes_AS_STRING(result)));
    asprintf(&buf, "%s\r\n%s", buf, PyBytes_AS_STRING(result));

    response_buf.base = buf;
    response_buf.len = strlen(buf);
}

void build_wsgi_environ(llhttp_t* parser) {
    Request* request = (Request*)parser->data;
    const char* method = llhttp_method_name(parser->method);
    PyObject* protocol = parser->http_minor == 1 ? HTTP_1_1 : HTTP_1_0;

    // Find a better way to set these
    // https://www.python.org/dev/peps/pep-3333/#specification-details
    PyObject* headers = request->headers;
    PyDict_SetItem(headers, REQUEST_METHOD, PyUnicode_FromString(method));
    PyDict_SetItem(headers, SCRIPT_NAME, empty_string);
    PyDict_SetItem(headers, SERVER_NAME, server_host);
    PyDict_SetItem(headers, SERVER_PORT, server_port);
    PyDict_SetItem(headers, SERVER_PROTOCOL, protocol);
    PyDict_SetItem(headers, wsgi_version, version);
    PyDict_SetItem(headers, wsgi_url_scheme, http_scheme);
    PyDict_SetItem(headers, wsgi_errors, PySys_GetObject("stderr"));
    PyDict_SetItem(headers, wsgi_run_once, Py_False);
    PyDict_SetItem(headers, wsgi_multithread, Py_False);
    PyDict_SetItem(headers, wsgi_multiprocess, Py_True);

    Py_DECREF(protocol);
}

void configure_parser_settings() {
    llhttp_settings_init(&parser_settings);
    parser_settings.on_url = on_url;
    parser_settings.on_body = on_body;
    parser_settings.on_header_field = on_header_field;
    parser_settings.on_header_value = on_header_value;
    parser_settings.on_message_begin = on_message_begin;
    parser_settings.on_message_complete = on_message_complete;
}