/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MBED_HTTP_RESPONSE_BUILDER_
#define _MBED_HTTP_RESPONSE_BUILDER_

#include <string>
#include <map>
#include "http_parser.h"
#include "HttpParsedRequest.h"
#include "ClientConnection.h"

static const char* get_http_status_string(uint16_t _statusCode) {
    switch (_statusCode) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 421: return "Misdirected Request";
        case 422: return "Unprocessable Entity";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";
        default : return "Unknown";
    }
}

static const struct mapping_t {
    const char* key;
    const char* value;
} fileTypeMapping[]  = {
    {".gif", "image/gif"   },
    {".jpg", "image/jpeg"  },
    {".jpeg","image/jpeg"  },
    {".ico", "image/x-icon"},
    {".png", "image/png"   },
    {".zip", "image/zip"   },
    {".gz",  "image/gz"    },
    {".tar", "image/tar"   },
    {".txt", "plain/text"  },
    {".pdf", "application/pdf" },
    {".htm", "text/html; charset=utf-8"   },
    {"html","text/html; charset=utf-8"   },
    {".css", "text/css"    },
    {".js",  "text/javascript"}};

class HttpResponseBuilder {
public:
    HttpResponseBuilder(uint16_t statusCode, ClientConnection* clientConnection) : 
        _statusCode(statusCode), 
        _clientConnection(clientConnection),
        status_message(get_http_status_string(statusCode))
    {
    }

    void setStatusCode(uint16_t statusCode) {
        _statusCode = statusCode;
        status_message = get_http_status_string(statusCode);
    }

    /**
     * Set a header for the request
     * If the key already exists, it will be overwritten...
     */
    void set_header(string key, string value) {
        map<string, string>::iterator it = headers.find(key);

        if (it != headers.end()) {
            it->second = value;
        }
        else {
            headers.insert(headers.end(), pair<string, string>(key, value));
        }
    }

    nsapi_size_or_error_t sendHeader() 
    {
        _buffer.reserve(2048);

        _buffer = "HTTP/1.1 ";
        _buffer += to_string(_statusCode);
        _buffer += " ";
        _buffer += get_http_status_string(_statusCode);
        _buffer += "\r\n";

        typedef map<string, string>::iterator it_type;
        for(it_type it = headers.begin(); it != headers.end(); it++) {
            // line is KEY: VALUE\r\n
            _buffer += it->first;
            _buffer += ":";
            _buffer += it->second;
            _buffer += "\r\n";
        }
        _buffer += "\r\n";

        //printf(_buffer.c_str());

        // send header
        nsapi_size_or_error_t sent = _clientConnection->send(_buffer.c_str(),  _buffer.size());

        return sent;
    }

    nsapi_size_or_error_t sendHeaderAndFile(FileSystem *fs, string filename) {
        _buffer.reserve(2048);

        // open file and get filesize
        filename = "/htmlRoot" + filename;

        size_t fileSize = 0;
        File file;
        int res = file.open(fs, filename.c_str());

        if(res != 0) {
            _statusCode = 404;
        }
        
        if(res == 0) {
            fileSize = file.size();

            if (filename.length() > 4) 
                getStandardHeaders(filename.substr(filename.length()-4).c_str());
            else
                getStandardHeaders(nullptr);

            set_header("Content-Length", to_string(fileSize));
        }

        printf("send file: %s  size: %d Bytes\n", filename.c_str(), fileSize);

        nsapi_size_or_error_t sent = sendHeader();

        // send file chunks
        if((res == 0) && (sent > 0)) {
            const size_t maxChunkSize = 2*1024;
            char *chunkBuffer = new char[maxChunkSize];
            size_t bytesRead = 0;

            while (bytesRead < fileSize) {
                size_t chunkSize = min(fileSize - bytesRead, maxChunkSize);
                size_t n = file.read(chunkBuffer, chunkSize);
                nsapi_size_or_error_t sent = _clientConnection->send(chunkBuffer,  chunkSize);
                if (sent > 0)
                    bytesRead += n;
            }

            delete chunkBuffer;
        }

        file.close();

        return fileSize;
    }

    char* build(const void* body, size_t body_size, size_t* size) {
        char buffer[10];
        snprintf(buffer, sizeof(buffer), "%d", body_size);
        set_header("Content-Length", string(buffer));

        char _statusCode_buffer[6];
        snprintf(_statusCode_buffer, sizeof(_statusCode_buffer), "%d", _statusCode /* max 5 digits */);

        *size = 0;

        // first line is HTTP/1.1 200 OK\r\n
        *size += 8 + 1 + strlen(_statusCode_buffer) + 1 + strlen(status_message) + 2;

        // after that we'll do the headers
        typedef map<string, string>::iterator it_type;
        for(it_type it = headers.begin(); it != headers.end(); it++) {
            // line is KEY: VALUE\r\n
            *size += it->first.length() + 1 + 1 + it->second.length() + 2;
        }

        // then the body, first an extra newline
        *size += 2;

        // body
        *size += body_size;

        // Now let's print it
        char* res = (char*)calloc(*size + 1, 1);
        char* originalRes = res;

        res += sprintf(res, "HTTP/1.1 %s %s\r\n", _statusCode_buffer, status_message);

        typedef map<string, string>::iterator it_type;
        for(it_type it = headers.begin(); it != headers.end(); it++) {
            // line is KEY: VALUE\r\n
            res += sprintf(res, "%s: %s\r\n", it->first.c_str(), it->second.c_str());
        }

        res += sprintf(res, "\r\n");

        if (body_size > 0) {
            memcpy(res, body, body_size);
        }
        res += body_size;

        // Uncomment to debug...
        // printf("----- BEGIN RESPONSE -----\n");
        // printf("%s", originalRes);
        // printf("----- END RESPONSE -----\n");

        return originalRes;
    }

    nsapi_error_t send(const void* body, size_t body_size) {
        if (!_clientConnection) return NSAPI_ERROR_NO_SOCKET;

        size_t res_size;
        char* response = build(body, body_size, &res_size);

        nsapi_error_t r = _clientConnection->send(response, res_size);

        free(response);

        return r;
    }

    nsapi_error_t sendBodyString(string body) {
        if (!_clientConnection) return NSAPI_ERROR_NO_SOCKET;

        nsapi_error_t r = _clientConnection->send(body.c_str(), body.length());

        return r;
    }

private:
    void getStandardHeaders(const char* fext)
    {
        headers["DNT"] = "1";
        headers["Connection"] = "Keep-Alive";
        headers["Server"] = "JojoS_Mbed_Server";
        if (fext == nullptr)
            headers["Content-Type"] = "text/html; charset=utf-8";
        else {
            for (size_t i = 0; i < sizeof(fileTypeMapping)/sizeof(struct mapping_t); i++) {
                if (_stricmp(fileTypeMapping[i].key, fext) == 0) {
                    headers["Content-Type"] = fileTypeMapping[i].value;
                    break;
                }
            }
        }
    }

    int _stricmp(const char* a, const char* b)
    {
        int la = strlen(a);
        int lb = strlen(b);
        for (int i = 0 ; i < min(la, lb) ; i++) {
            if (tolower((int)a[i]) != tolower((int)b[i]))
                return i+1;
        }
        return 0;
    }

    uint16_t _statusCode;
    ClientConnection* _clientConnection;
    const char* status_message;
    map<string, string> headers;
    string  _buffer;
};

#endif // _MBED_HTTP_RESPONSE_BUILDER_
