// Minimal async HTTP(S) client for the CS2 RTV plugin.
// Windows: WinHTTP. Linux: libcurl.
// Requests are queued and dispatched on a single background worker thread.

#include "http_client.h"
#include "src/common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

enum class HttpMethod
{
	GET,
	POST,
	POST_FORM
};

struct HttpRequest
{
	HttpMethod method;
	std::string url;
	std::string body; // only for POST
	HttpCallback callback;
};

static std::thread s_worker;
static std::mutex s_mutex;
static std::condition_variable s_cv;
static std::queue<HttpRequest> s_queue;
static std::atomic<bool> s_running {false};

// Main-thread dispatch queue
static std::mutex s_mainMutex;
static std::vector<std::function<void()>> s_mainQueue;
static std::vector<std::function<void()>> s_mainDrain; // swap target

void RTV_QueueMainThread(std::function<void()> fn)
{
	std::lock_guard<std::mutex> lock(s_mainMutex);
	s_mainQueue.push_back(std::move(fn));
}

void RTV_DrainMainThread()
{
	{
		std::lock_guard<std::mutex> lock(s_mainMutex);
		s_mainDrain.swap(s_mainQueue);
	}
	for (auto &fn : s_mainDrain)
	{
		fn();
	}
	s_mainDrain.clear();
}

#ifdef _WIN32

static bool PerformRequest(const HttpRequest &req, std::string &outBody)
{
	const std::string &url = req.url;

	// Parse scheme
	bool isHttps = (url.rfind("https://", 0) == 0);
	size_t schemeEnd = url.find("://") + 3;
	size_t hostEnd = url.find('/', schemeEnd);
	if (hostEnd == std::string::npos)
	{
		hostEnd = url.size();
	}

	std::string host = url.substr(schemeEnd, hostEnd - schemeEnd);
	std::string path = (hostEnd < url.size()) ? url.substr(hostEnd) : "/";

	// Convert to wide
	auto ToWide = [](const std::string &s) -> std::wstring
	{
		int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
		return w;
	};

	HINTERNET hSession = WinHttpOpen(L"CS2RTV/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
	{
		return false;
	}

	INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
	HINTERNET hConnect = WinHttpConnect(hSession, ToWide(host).c_str(), port, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
	const wchar_t *verb = (req.method == HttpMethod::GET) ? L"GET" : L"POST";
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb, ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	// Set timeouts: resolve=5s, connect=5s, send=15s, receive=15s
	DWORD t5s = 5000;
	DWORD t15s = 15000;
	WinHttpSetTimeouts(hRequest, t5s, t5s, t15s, t15s);

	// Headers
	std::wstring headers;
	if (req.method == HttpMethod::POST)
	{
		headers = L"Content-Type: application/json\r\n";
	}
	else if (req.method == HttpMethod::POST_FORM)
	{
		headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
	}

	bool hasBody = (req.method == HttpMethod::POST || req.method == HttpMethod::POST_FORM);
	BOOL sent = WinHttpSendRequest(hRequest, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : (DWORD)-1,
								   hasBody ? (LPVOID)req.body.c_str() : WINHTTP_NO_REQUEST_DATA,
								   hasBody ? (DWORD)req.body.size() : 0,
								   hasBody ? (DWORD)req.body.size() : 0, 0);

	bool success = false;
	if (sent && WinHttpReceiveResponse(hRequest, nullptr))
	{
		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
							WINHTTP_NO_HEADER_INDEX);

		// Read body
		std::string body;
		DWORD avail = 0;
		while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
		{
			std::string chunk(avail, '\0');
			DWORD read = 0;
			WinHttpReadData(hRequest, &chunk[0], avail, &read);
			body.append(chunk.c_str(), read);
		}

		if (statusCode >= 200 && statusCode < 300)
		{
			outBody = std::move(body);
			success = true;
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return success;
}

#else // Linux - libcurl

struct CurlWriteCtx
{
	std::string data;
};

static size_t CurlWriteCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *ctx = static_cast<CurlWriteCtx *>(userdata);
	ctx->data.append(ptr, size * nmemb);
	return size * nmemb;
}

static bool PerformRequest(const HttpRequest &req, std::string &outBody)
{
	CURL *curl = curl_easy_init();
	if (!curl)
	{
		return false;
	}

	CurlWriteCtx wctx;
	struct curl_slist *headers = nullptr;
	if (req.method == HttpMethod::POST)
	{
		headers = curl_slist_append(headers, "Content-Type: application/json");
	}
	else if (req.method == HttpMethod::POST_FORM)
	{
		headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	}

	curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
	if (req.method == HttpMethod::POST || req.method == HttpMethod::POST_FORM)
	{
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
	}
	if (headers)
	{
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	CURLcode res = curl_easy_perform(curl);
	bool success = false;
	if (res == CURLE_OK)
	{
		long code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		if (code >= 200 && code < 300)
		{
			outBody = std::move(wctx.data);
			success = true;
		}
	}

	if (headers)
	{
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);
	return success;
}

#endif // Linux

static void WorkerThread()
{
	while (s_running.load())
	{
		HttpRequest req;
		{
			std::unique_lock<std::mutex> lock(s_mutex);
			s_cv.wait(lock, [] { return !s_queue.empty() || !s_running.load(); });

			if (!s_running.load() && s_queue.empty())
			{
				break;
			}
			if (s_queue.empty())
			{
				continue;
			}

			req = std::move(s_queue.front());
			s_queue.pop();
		}

		std::string body;
		bool ok = PerformRequest(req, body);
		if (req.callback)
		{
			req.callback(ok, std::move(body));
		}
	}
}

static void EnsureStarted()
{
	if (!s_running.load())
	{
		s_running.store(true);
		s_worker = std::thread(WorkerThread);
	}
}

void RTV_HttpGet(const std::string &url, HttpCallback callback)
{
	EnsureStarted();
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_queue.push({HttpMethod::GET, url, "", std::move(callback)});
	}
	s_cv.notify_one();
}

void RTV_HttpPost(const std::string &url, const std::string &jsonBody, HttpCallback callback)
{
	EnsureStarted();
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_queue.push({HttpMethod::POST, url, jsonBody, std::move(callback)});
	}
	s_cv.notify_one();
}

void RTV_HttpPostForm(const std::string &url, const std::string &formBody, HttpCallback callback)
{
	EnsureStarted();
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_queue.push({HttpMethod::POST_FORM, url, formBody, std::move(callback)});
	}
	s_cv.notify_one();
}

void RTV_HttpShutdown()
{
	if (!s_running.load())
	{
		return;
	}

	s_running.store(false);
	s_cv.notify_all();

	if (s_worker.joinable())
	{
		s_worker.join();
	}

	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_queue.empty())
	{
		s_queue.pop();
	}
}
