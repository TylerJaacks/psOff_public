#include "../libSceSsl/types.h"
#include "common.h"
#include "httpsTypes.h"
#include "logging.h"
#include "types.h"
#include "utility/utility.h"

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <boost/asio.hpp>

using namespace boost::asio;

LOG_DEFINE_MODULE(libSceHttp);

namespace {
using SceHttpsCallback = SYSV_ABI int (*)(int libsslCtxId, unsigned int verifyErr, SceSslCert* const sslCert[], int certNum, void* userArg);

#define ARRAY_LENGTH 128

#define LOOKUP_AVAILABLE_IDX(ARRAY, ACTION)                                                                                                                    \
  int  i;                                                                                                                                                      \
  bool found = false;                                                                                                                                          \
  for (i = 1; i < ARRAY_LENGTH; i++) {                                                                                                                         \
    if (ARRAY[i] == nullptr) {                                                                                                                                 \
      ARRAY[i] = ACTION;                                                                                                                                       \
                                                                                                                                                               \
      break;                                                                                                                                                   \
    }                                                                                                                                                          \
  }                                                                                                                                                            \
  if (!found) return Err::HTTP_OUT_OF_MEMORY;

#define GET_LAST_RESPONSE                                                                                                                                      \
  HttpRequest*  request  = g_requests[reqId];                                                                                                                  \
  HttpResponse* response = request->lastResponse;                                                                                                              \
  if (response == nullptr) return Err::HTTP_SEND_REQUIRED;

static io_service        svc;
static ip::tcp::resolver resolver(svc);

struct HttpClient {
  int      libnetMemId;
  int      libsslCtxId;
  size_t   poolSize;
  uint32_t connectTimeout;

  HttpClient(int libnetMemId, int libsslCtxId, size_t poolSize): libnetMemId(libnetMemId), libsslCtxId(libsslCtxId), poolSize(poolSize) {}
};

struct HttpTemplate {
  HttpClient* parentClient;
  const char* userAgent;
  int         httpVer;
  int         isAutoProxyConf;

  HttpTemplate(HttpClient* parentClient, const char* userAgent, int httpVer, int isAutoProxyConf)
      : parentClient(parentClient), userAgent(userAgent), httpVer(httpVer), isAutoProxyConf(isAutoProxyConf) {}
};

struct HttpConnection {
  HttpTemplate* parentTemplate;
  const char*   serverName;
  const char*   scheme;
  uint16_t      port;
  int           isEnableKeepalive;

  bool connected = false;

  ip::tcp::resolver::query*   query = nullptr;
  ip::tcp::resolver::iterator endpoint_iterator;
  ip::tcp::socket*            socket = nullptr;

  HttpConnection(HttpTemplate* parentTemplate, const char* serverName, const char* scheme, uint16_t port, int isEnableKeepalive)
      : parentTemplate(parentTemplate), serverName(serverName), scheme(scheme), port(port), isEnableKeepalive(isEnableKeepalive) {}
};

struct HttpResponse {
  int         statusCode;
  uint32_t    contentLength;
  const char* body = nullptr;
};

struct HttpRequest {
  HttpConnection* parentConnection;
  int             method;
  const char*     path;
  uint64_t        contentLength;
  const void*     postData = nullptr;
  size_t          size;
  HttpResponse*   lastResponse = nullptr;

  HttpRequest(HttpConnection* parentConnection, int method, const char* path, uint64_t contentLength)
      : parentConnection(parentConnection), method(method), path(path), contentLength(contentLength) {}
};

struct HttpRequestParams {
  HttpConnection* connection;
  HttpRequest*    request;
  uint32_t        connectTimeout;
  const char*     userAgent;
  int             httpVer;
  int             isAutoProxyConf;
  const char*     serverName;
  const char*     scheme;
  uint16_t        port;
  int             isEnableKeepalive;
  int             method;
  const char*     path;
  uint64_t        contentLength;
  const void*     postData; // will be assigned to nullptr
  size_t          size;

  HttpRequestParams(HttpRequest* from) {
    connection        = from->parentConnection;
    request           = from;
    connectTimeout    = from->parentConnection->parentTemplate->parentClient->connectTimeout;
    userAgent         = from->parentConnection->parentTemplate->userAgent;
    httpVer           = from->parentConnection->parentTemplate->httpVer;
    isAutoProxyConf   = from->parentConnection->parentTemplate->isAutoProxyConf;
    serverName        = from->parentConnection->serverName;
    scheme            = from->parentConnection->scheme;
    port              = from->parentConnection->port;
    isEnableKeepalive = from->parentConnection->isEnableKeepalive;
    method            = from->method;
    path              = from->path;
    contentLength     = from->contentLength;
    postData          = from->postData;
    size              = from->size;
  }
};

static HttpClient*     g_clients[ARRAY_LENGTH]     = {nullptr};
static HttpTemplate*   g_templates[ARRAY_LENGTH]   = {nullptr};
static HttpConnection* g_connections[ARRAY_LENGTH] = {nullptr};
static HttpRequest*    g_requests[ARRAY_LENGTH]    = {nullptr};

template <typename T, size_t size>
static int testId(T (&array)[size], int id) {
  if (id < 1 || id >= size || array[id] == nullptr) return Err::HTTP_BAD_ID;

  return Ok;
}

static void deleteResponse(HttpResponse* response) {
  if (response->body != nullptr) {
    delete response->body;
  }
  delete response;
}

static int httpMethodStringToInt(const char* method) {
  if (std::strcmp(method, "get") == 0 || std::strcmp(method, "GET") == 0) {
    return SCE_HTTP_GET;
  } else if (std::strcmp(method, "post") == 0 || std::strcmp(method, "POST") == 0) {
    return SCE_HTTP_POST;
  }
  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"unsupported http method: %s", method);

  return -1;
}

static int32_t performHttpRequest(HttpRequestParams* request, HttpResponse* response) {
  LOG_USE_MODULE(libSceHttp);

  HttpConnection* connection = request->connection;
  try {
    if (!connection->connected) {
      connection->query             = new ip::tcp::resolver::query(request->serverName, std::to_string(request->port));
      connection->endpoint_iterator = resolver.resolve(*connection->query);
      connection->socket            = new ip::tcp::socket(svc);
      boost::asio::connect(*connection->socket, connection->endpoint_iterator);
      connection->connected = true;

      if (std::strcmp(connection->scheme, "https") == 0) {
        LOG_TRACE(L"detected an attempt to connect via https, it's not supported yet");

        return Err::HTTP_SSL_ERROR;
      }
      if (std::strcmp(connection->scheme, "http") != 0) {
        LOG_TRACE(L"unknown scheme: %s://", connection->scheme);

        return Err::HTTP_BAD_SCHEME;
      }
    }
    if (request->request->lastResponse != nullptr) {
      deleteResponse(request->request->lastResponse);
    }
    boost::asio::streambuf buffer;
    std::ostream           bufferStream(&buffer);

    if (request->method == SCE_HTTP_GET) {
      bufferStream << "GET ";
    } else if (request->method == SCE_HTTP_POST) {
      bufferStream << "POST ";
    } else {
      LOG_TRACE(L"unsupported request method code (%d), pretending we've failed to connect", request->method);

      return Err::HTTP_FAILURE;
    }
    bufferStream << request->path;
    if (request->httpVer == 1)
      bufferStream << "HTTP/1.0";
    else
      bufferStream << "HTTP/1.1";
    bufferStream << "\r\n";
    if (request->httpVer != 1 /* means HTTP 1.1 */) {
      bufferStream << "Host: " << request->serverName << "\r\n";
      bufferStream << "User-Agent: " << request->userAgent << "\r\n";
      bufferStream << "Accept: */*\r\n";
      bufferStream << "Connection: " << (request->isEnableKeepalive ? "keep-alive" : "close") << "\r\n";
    }
    if (request->postData != nullptr) {
      bufferStream << "Content-Length: " << request->contentLength << "\r\n\r\n";
      for (size_t i = 0; i < request->size; i++) {
        bufferStream << ((char*)request->postData)[i];
      }
    }
    boost::asio::write(*connection->socket, buffer);

    boost::asio::streambuf responseBuffer;
    boost::asio::read_until(*connection->socket, responseBuffer, "\r\n");
    std::istream responseBufferStream(&responseBuffer);

    std::string httpVersion;
    int         statusCode;
    std::string statusDescription;
    responseBufferStream >> httpVersion;
    responseBufferStream >> statusCode;
    std::getline(responseBufferStream, statusDescription);

    boost::asio::read_until(*connection->socket, responseBuffer, "\r\n\r\n");

    bool        foundContentLengthHeader = false;
    uint32_t    contentLength;
    std::string header;
    while (std::getline(responseBufferStream, header) && header != "\r") {
      if (header.rfind("Content-Length: ", 0) == 0) { // todo: remove case-sensitive check
        std::string contentLengthUnparsed = header.substr(16);
        contentLength                     = std::stoi(contentLengthUnparsed);
        foundContentLengthHeader          = true;

        break;
      }
    }
    if (!foundContentLengthHeader) {
      LOG_TRACE(L"failed to find \"Content-Length\" header in the response");

      return Err::HTTP_FAILURE;
    }
    bool tooLow;
    if ((tooLow = contentLength < 1) || contentLength > 1610612736) {
      LOG_TRACE(L"bad content length: %s", (tooLow ? "less than 1 byte" : "more than 1.5 GiB"));

      return Err::HTTP_FAILURE;
    }
    size_t currentIdx = 0;
    char*  body;
    try {
      body = new char[contentLength];
    } catch (std::bad_alloc&) {
      LOG_TRACE(L"failed to allocate %d bytes", contentLength);

      return Err::HTTP_FAILURE;
    }
    response->statusCode    = statusCode;
    response->contentLength = contentLength;
    response->body          = body;

    boost::system::error_code error;
    while (boost::asio::read(*connection->socket, responseBuffer, boost::asio::transfer_at_least(1), error)) {
      std::streamsize available = responseBuffer.in_avail();
      char            c;
      for (std::streamsize i = 0; i < available; i++) {
        responseBufferStream >> c;
        body[currentIdx++] = c;
      }
    }
    if (error != boost::asio::error::eof) throw boost::system::system_error(error);

    return Ok;
  } catch (const boost::exception& e) {
    LOG_TRACE(L"caught a boost exception while performing an http request");

    return Err::HTTP_FAILURE;
  } catch (const std::exception& e) {
    LOG_TRACE(L"caught an std::exception while performing an http request");

    return Err::HTTP_FAILURE;
  }
}
} // namespace

extern "C" {

EXPORT const char* MODULE_NAME = "libSceHttp";

EXPORT SYSV_ABI int sceHttpInit(int libnetMemId, int libsslCtxId, size_t poolSize) {
  LOOKUP_AVAILABLE_IDX(g_clients, new HttpClient(libnetMemId, libsslCtxId, poolSize));

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"new http client (id: %d, memId: %d, sslCtxId: %d, poolSize: %d)", i, libnetMemId, libsslCtxId, poolSize);

  return i;
}

EXPORT SYSV_ABI int sceHttpTerm(int libhttpCtxId) {
  if (auto ret = testId(g_clients, libhttpCtxId)) return ret;
  delete g_clients[libhttpCtxId];
  g_clients[libhttpCtxId] = nullptr;

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"http client removed (id: %d)", libhttpCtxId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetMemoryPoolStats(int libhttpCtxId, SceHttpMemoryPoolStats* currentStat) {
  if (auto ret = testId(g_clients, libhttpCtxId)) return ret;
  currentStat->currentInuseSize = 16384; // todo (?)
  currentStat->maxInuseSize     = 131072;
  currentStat->poolSize         = g_clients[libhttpCtxId]->poolSize;

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"memory pool stats were requested (client id: %d)", libhttpCtxId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateTemplate(int libhttpCtxId, const char* userAgent, int httpVer, int isAutoProxyConf) {
  if (auto ret = testId(g_clients, libhttpCtxId)) return ret;
  HttpClient* client = g_clients[libhttpCtxId];
  LOOKUP_AVAILABLE_IDX(g_templates, new HttpTemplate(client, userAgent, httpVer, isAutoProxyConf));

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"new http template (id: %d, client id: %d)", i, libhttpCtxId);

  return i;
}

EXPORT SYSV_ABI int sceHttpDeleteTemplate(int tmplId) {
  if (auto ret = testId(g_templates, tmplId)) return ret;
  delete g_templates[tmplId];
  g_templates[tmplId] = nullptr;

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"http template removed (id: %d)", tmplId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateConnection(int tmplId, const char* serverName, const char* scheme, uint16_t port, int isEnableKeepalive) {
  if (auto ret = testId(g_templates, tmplId)) return ret;
  HttpTemplate* httpTemplate = g_templates[tmplId];
  LOOKUP_AVAILABLE_IDX(g_connections, new HttpConnection(httpTemplate, serverName, scheme, port, isEnableKeepalive));

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"new http connection (id: %d, template id: %d)", i, tmplId);

  return i;
}

EXPORT SYSV_ABI int sceHttpCreateConnectionWithURL(int tmplId, const char* url, int isEnableKeepalive) {
  // using namespace boost::network;
  // uri::uri instance(url);

  return Err::CONNECT_TIMEOUT;
}

EXPORT SYSV_ABI int sceHttpDeleteConnection(int connId) {
  if (auto ret = testId(g_connections, connId)) return ret;
  HttpConnection* connection = g_connections[connId];
  if (connection->query != nullptr) {
    delete connection->query;
  }
  if (connection->socket != nullptr) {
    connection->socket->close();

    delete connection->socket;
  }
  delete connection;
  g_connections[connId] = nullptr;

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"http connection removed (id: %d)", connId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateRequest(int connId, int method, const char* path, uint64_t contentLength) {
  if (auto ret = testId(g_connections, connId)) return ret;
  HttpConnection* httpConnection = g_connections[connId];
  LOOKUP_AVAILABLE_IDX(g_requests, new HttpRequest(httpConnection, method, path, contentLength));

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"new http request (id: %d, connection id: %d)", i, connId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateRequest2(int connId, const char* method, const char* path, uint64_t contentLength) {
  return sceHttpCreateRequest(connId, httpMethodStringToInt(method), path, contentLength);
}

EXPORT SYSV_ABI int sceHttpCreateRequestWithURL(int connId, int method, const char* url, uint64_t contentLength) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateRequestWithURL2(int connId, const char* method, const char* url, uint64_t contentLength) {
  return sceHttpCreateRequestWithURL(connId, httpMethodStringToInt(method), url, contentLength);
}

EXPORT SYSV_ABI int sceHttpDeleteRequest(int reqId) {
  if (auto ret = testId(g_requests, reqId)) return ret;
  HttpRequest* request = g_requests[reqId];
  if (request->lastResponse != nullptr) {
    deleteResponse(request->lastResponse);
  }
  delete request;
  g_requests[reqId] = nullptr;

  LOG_USE_MODULE(libSceHttp);
  LOG_TRACE(L"http request removed (id: %d)", reqId);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetRequestContentLength(int id, uint64_t contentLength) {
  int reqId = id; // (?)
  if (auto ret = testId(g_requests, reqId)) return ret;
  g_requests[reqId]->contentLength = contentLength;

  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetChunkedTransferEnabled(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetInflateGZIPEnabled(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSendRequest(int reqId, const void* postData, size_t size) {
  if (auto ret = testId(g_requests, reqId)) return ret;
  HttpRequest* request          = g_requests[reqId];
  request->postData             = postData;
  request->size                 = size;
  HttpRequestParams* fullParams = new HttpRequestParams(request);
  HttpResponse*      response   = new HttpResponse();
  int32_t            result     = performHttpRequest(fullParams, response);
  delete fullParams;
  if (result == Ok) {
    request->lastResponse = response;
  } else {
    deleteResponse(response);
  }

  return result;
}

EXPORT SYSV_ABI int sceHttpAbortRequest(int reqId) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetResponseContentLength(int reqId, int* result, uint64_t* contentLength) {
  if (auto ret = testId(g_requests, reqId)) return ret;
  GET_LAST_RESPONSE;
  *result        = 0; // Content-Length is guaranteed to exist, otherwise performHttpRequest would have failed
  *contentLength = response->contentLength;

  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetStatusCode(int reqId, int* statusCode) {
  if (auto ret = testId(g_requests, reqId)) return ret;
  GET_LAST_RESPONSE;
  *statusCode = response->statusCode;

  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetAllResponseHeaders(int reqId, char** header, size_t* headerSize) {
  *headerSize = 0;

  return Ok;
}

EXPORT SYSV_ABI int sceHttpReadData(int reqId, void* data, size_t size) {
  if (auto ret = testId(g_requests, reqId)) return ret;
  GET_LAST_RESPONSE;
  size_t finalSize = min(size, response->contentLength);
  memcpy(data, response->body, finalSize);

  return Ok;
}

EXPORT SYSV_ABI int sceHttpAddRequestHeader(int id, const char* name, const char* value, uint32_t mode) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpRemoveRequestHeader(int id, const char* name) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpParseResponseHeader(const char* header, size_t headerLen, const char* fieldStr, const char** fieldValue, size_t* valueLen) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpParseStatusLine(const char* statusLine, size_t lineLen, int* httpMajorVer, int* httpMinorVer, int* responseCode,
                                           const char** reasonPhrase, size_t* phraseLen) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetResponseHeaderMaxSize(int id, size_t headerSize) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetAuthInfoCallback(int id, SceHttpAuthInfoCallback cbfunc, void* userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetAuthEnabled(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetAuthEnabled(int id, int* isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpAuthCacheFlush(int libhttpCtxId) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetRedirectCallback(int id, SceHttpRedirectCallback cbfunc, void* userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetAutoRedirect(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetAutoRedirect(int id, int* isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpRedirectCacheFlush(int libhttpCtxId) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetResolveTimeOut(int id, uint32_t usec) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetResolveRetry(int id, int retry) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetConnectTimeOut(int id, uint32_t usec) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetSendTimeOut(int id, uint32_t usec) {
  return Err::SEND_TIMEOUT;
}

EXPORT SYSV_ABI int sceHttpSetRecvTimeOut(int id, uint32_t usec) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetRequestStatusCallback(int id, SceHttpRequestStatusCallback cbfunc, void* userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetLastErrno(int reqId, int* errNum) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetNonblock(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetNonblock(int id, int* isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpTrySetNonblock(int id, int isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpTryGetNonblock(int id, int* isEnable) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpCreateEpoll(int libhttpCtxId, SceHttpEpollHandle* eh) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpSetEpoll(int id, SceHttpEpollHandle eh, void* userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpUnsetEpoll(int id) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpGetEpoll(int id, SceHttpEpollHandle* eh, void** userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpDestroyEpoll(int libhttpCtxId, SceHttpEpollHandle eh) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpWaitRequest(SceHttpEpollHandle eh, SceHttpNBEvent* nbev, int maxevents, int timeout) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpAbortWaitRequest(SceHttpEpollHandle eh) {
  return Ok;
}

// HTTPS
EXPORT SYSV_ABI int sceHttpsLoadCert(int libhttpCtxId, int caCertNum, const SceSslData** caList, const SceSslData* cert, const SceSslData* privKey) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsUnloadCert(int libhttpCtxId) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsEnableOption(int id, uint32_t sslFlags) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsDisableOption(int id, uint32_t sslFlags) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsGetSslError(int id, int* errNum, uint32_t* detail) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsSetSslCallback(int id, SceHttpsCallback cbfunc, void* userArg) {
  return Ok;
}

EXPORT SYSV_ABI int sceHttpsSetSslVersion(int id, SceSslVersion version) {
  return Ok;
}
}