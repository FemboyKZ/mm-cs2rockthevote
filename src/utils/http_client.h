#ifndef _INCLUDE_RTV_HTTP_CLIENT_H_
#define _INCLUDE_RTV_HTTP_CLIENT_H_

// Thin wrappers over mmu::http keeping the historical RTV_* names.
// Callbacks run on a background thread, schedule game work via RTV_QueueMainThread.

#include "mmu/http_client.h"

#include <functional>
#include <string>

using HttpCallback = mmu::http::Callback;

// Schedule a function to run on the game thread during the next GameFrame.
void RTV_QueueMainThread(std::function<void()> fn);

// Drain the main-thread queue. Called from Hook_GameFrame.
void RTV_DrainMainThread();

// GET request. url must be https:// or http://.
void RTV_HttpGet(const std::string &url, HttpCallback callback);

// POST request with JSON body.
void RTV_HttpPost(const std::string &url, const std::string &jsonBody, HttpCallback callback);

// POST request with application/x-www-form-urlencoded body.
void RTV_HttpPostForm(const std::string &url, const std::string &formBody, HttpCallback callback);

// Shutdown: cancel any in-flight request and join the worker thread.
void RTV_HttpShutdown();

// Reset the shutdown latch and set the user agent. Called from plugin Load().
void RTV_HttpResetShutdownLatch();

// Discard any queued main-thread callbacks without executing them.
void RTV_ClearMainQueue();

#endif // _INCLUDE_RTV_HTTP_CLIENT_H_
