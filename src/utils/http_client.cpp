#include "http_client.h"

void RTV_QueueMainThread(std::function<void()> fn)
{
	mmu::http::QueueMainThread(std::move(fn));
}

void RTV_DrainMainThread()
{
	mmu::http::DrainMainThread();
}

void RTV_HttpGet(const std::string &url, HttpCallback callback)
{
	mmu::http::Get(url, std::move(callback));
}

void RTV_HttpPost(const std::string &url, const std::string &jsonBody, HttpCallback callback)
{
	mmu::http::Post(url, jsonBody, std::move(callback));
}

void RTV_HttpPostForm(const std::string &url, const std::string &formBody, HttpCallback callback)
{
	mmu::http::PostForm(url, formBody, std::move(callback));
}

void RTV_HttpShutdown()
{
	mmu::http::Shutdown();
}

void RTV_HttpResetShutdownLatch()
{
	mmu::http::SetUserAgent("CS2RTV/1.0");
	mmu::http::ResetShutdownLatch();
}

void RTV_ClearMainQueue()
{
	mmu::http::ClearMainQueue();
}
