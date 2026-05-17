#ifndef _INCLUDE_RTV_HTTP_CLIENT_H_
#define _INCLUDE_RTV_HTTP_CLIENT_H_

// Minimal async HTTP(S) GET/POST utilities used by the RTV plugin.
// On Windows: WinHTTP.
// On Linux: libcurl.

#include <functional>
#include <string>

// Callback type: (success, responseBody)
// WARNING: callbacks are invoked on a background thread. Do NOT touch game
// state directly from them. Use RTV_QueueMainThread() to schedule any work
// that needs to run on the game thread.
using HttpCallback = std::function<void(bool success, std::string body)>;

// Schedule a function to run on the game thread during the next GameFrame.
// Thread-safe: safe to call from an HttpCallback.
void RTV_QueueMainThread(std::function<void()> fn);

// Drain the main-thread queue. Called from Hook_GameFrame.
// Must only be called from the game thread.
void RTV_DrainMainThread();

// GET request. url must be https:// or http://.
// Callback is invoked on a background thread.
void RTV_HttpGet(const std::string &url, HttpCallback callback);

// POST request with JSON body.
void RTV_HttpPost(const std::string &url, const std::string &jsonBody, HttpCallback callback);

// POST request with application/x-www-form-urlencoded body.
void RTV_HttpPostForm(const std::string &url, const std::string &formBody, HttpCallback callback);

// Shutdown: cancel any in-flight request and join the worker thread.
// Called on plugin unload.
void RTV_HttpShutdown();

// Reset internal shutdown latch so that the worker can be restarted by the
// next request. Called from plugin Load() so that an unload/reload cycle
// inside the same process leaves HTTP functional.
void RTV_HttpResetShutdownLatch();

// Discard any queued main-thread callbacks without executing them.
// Called at the very end of plugin Unload() after hooks are gone.
void RTV_ClearMainQueue();

#endif // _INCLUDE_RTV_HTTP_CLIENT_H_
