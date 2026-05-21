#include <algorithm>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>
#include "node-shared-mem.h"

#ifdef _WIN32

// trim from end of string (right)
static std::string& rtrim(std::string& s)
{
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
    return s;
}

// trim from beginning of string (left)
static std::string& ltrim(std::string& s)
{
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
    return s;
}

// trim from both ends of string (right then left)
static std::string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

static std::string GetLastErrorAsString()
{
    //Get the error message, if any.
    const DWORD errorMessageID = ::GetLastError();
    if(errorMessageID == 0)
        return {}; //No error message has been recorded

    LPSTR messageBuffer = nullptr;
    const size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
									   nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);

    std::string message(messageBuffer, size);

    //Free the buffer.
    LocalFree(messageBuffer);

    return trim(message);
}

#endif


#define failFormat(...) { \
		size_t size = snprintf(nullptr, 0, __VA_ARGS__) + 1; \
		std::unique_ptr<char[]> buf(new char[size]); \
		snprintf(buf.get(), size, __VA_ARGS__); \
		auto str = std::string(buf.get(), buf.get() + size - 1); \
		Napi::Error::New(env, str).ThrowAsJavaScriptException(); \
	}

#define fail(...) { failFormat(__VA_ARGS__); return; }
#define failv(...) { failFormat(__VA_ARGS__); return Napi::Value(); }

SharedMemory::SharedMemory(const Napi::CallbackInfo& info) : Napi::ObjectWrap<SharedMemory>(info)
{
	Napi::Env env = info.Env();

	// Invoked as constructor: `new MyObject(...)`
	if (info.Length() < 2)		fail("[SharedMemory] needs mapName and mapSize")
	if (!info[0].IsString())	fail("[SharedMemory] argument 0 needs to be a valid mapName")
	if (!info[1].IsNumber())	fail("[SharedMemory] argument 1 needs to be a valid mapLength")

	auto path = info[0].As<Napi::String>().Utf8Value();
	auto length = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());

#ifdef _WIN32
	HANDLE mapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, path.c_str());
	if (mapping == nullptr) {
		auto err = GetLastErrorAsString();
		fail("[SharedMemory] could not open \"%s\" (ERROR: %s)", path.c_str(), err.c_str())
	}

	void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, length);
	if (data == nullptr) {
		auto err = GetLastErrorAsString();
		CloseHandle(mapping);
		fail("[SharedMemory] could not map: \"%s\" (ERROR: %s)", path.c_str(), err.c_str())
	}

	m_handle = mapping;
#else
    char filePath[4096];
	#if __APPLE__
    sprintf(filePath, "%s", path.c_str());
    #else
    sprintf(filePath, "/%s", path.c_str());
	#endif
	int key = shm_open(filePath, O_RDWR, 0777);
	if (key < 0) {
		fail("[SharedMemory] could open \"%s\" (ERROR: %s)", path.c_str(), strerror(errno))
	}

	void* data = mmap(nullptr, length, 0x3, MAP_SHARED, key, 0);
	if(data == nullptr) {
		shm_unlink(path.c_str());
		fail("[SharedMemory] could not map \"%s\" (f%d) (ERROR: %s)", path.c_str(), key, strerror(errno))
	}

	m_handle = key;
#endif
	napi_value result;
	const auto status = napi_create_external_arraybuffer(env, data, length, nullptr, nullptr, &result);
	if (status == napi_ok)
	{
		const auto buffer = Napi::ArrayBuffer(env, result);
		m_buffer = Napi::Persistent(buffer);
		this->Value().Set("buffer", buffer);
		this->Value().Set("requiresCopy", false);
	}
	else if (status == napi_no_external_buffers_allowed)
	{
		const auto buffer = Napi::ArrayBuffer::New(env, length);
		m_buffer = Napi::Persistent(buffer);
		this->Value().Set("buffer", buffer);
		this->Value().Set("requiresCopy", true);
	}
	else
	{
		fail("[SharedMemory] Could not allocate array buffer (error = %d)", status);
	}

	this->Value().Set("name", info[0]);
	this->Value().Set("length", info[1]);

	m_ptr = static_cast<uint8_t*>(data);
	m_name = path;
	m_length = length;
}


Napi::Object SharedMemory::Init(Napi::Env env, Napi::Object exports) {
	const Napi::Function func = DefineClass(env, "SharedMemory", {
		InstanceMethod("copyFrom", &SharedMemory::CopyFrom),
		InstanceMethod("copyTo", &SharedMemory::CopyTo),
		InstanceMethod("close", &SharedMemory::Close)
	});

	exports.Set("SharedMemory", func);
	return exports;
}

Napi::Value SharedMemory::CopyFrom(const Napi::CallbackInfo& info)
{
	if (m_buffer.IsEmpty() || m_ptr == nullptr || m_length == 0) return {};

	auto buffer = m_buffer.Value();
	if (buffer.IsExternal()) return {};

	size_t srcOffset = info.Length() > 0 && info[0].IsNumber() ? info[0].As<Napi::Number>().Uint32Value() : 0;
	srcOffset = std::min(srcOffset, m_length);

	size_t dstOffset = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 0;
	dstOffset = std::min(dstOffset, m_length);

	size_t length = info.Length() > 2 && info[2].IsNumber() ? info[2].As<Napi::Number>().Uint32Value() : m_length;
	length = std::min({length, m_length - srcOffset, m_length - dstOffset});

	std::memcpy(static_cast<uint8_t*>(buffer.Data()) + dstOffset, m_ptr + srcOffset, length);

	return {};
}

Napi::Value SharedMemory::CopyTo(const Napi::CallbackInfo& info)
{
	if (m_buffer.IsEmpty() || m_ptr == nullptr || m_length == 0) return {};

	auto buffer = m_buffer.Value();
	if (buffer.IsExternal()) return {};

	size_t srcOffset = info.Length() > 0 && info[0].IsNumber() ? info[0].As<Napi::Number>().Uint32Value() : 0;
	srcOffset = std::min(srcOffset, m_length);

	size_t dstOffset = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 0;
	dstOffset = std::min(dstOffset, m_length);

	size_t length = info.Length() > 2 && info[2].IsNumber() ? info[2].As<Napi::Number>().Uint32Value() : m_length;
	length = std::min({length, m_length - srcOffset, m_length - dstOffset});

	std::memcpy(m_ptr + dstOffset, static_cast<uint8_t*>(buffer.Data()) + srcOffset, length);

	return {};
}

Napi::Value SharedMemory::Close(const Napi::CallbackInfo& info) {
	const Napi::Env env = info.Env();

#ifdef _WIN32
	if(m_ptr != nullptr) {
		if(!UnmapViewOfFile(m_ptr)) {
			const auto err = GetLastErrorAsString();
			CloseHandle(m_handle);
			failv("[SharedMemory] could not unmap \"%s\" (ERROR: %s)", m_name.c_str(), err.c_str())
		}
		m_ptr = nullptr;
	}

	if(m_handle != nullptr) {
		if(!CloseHandle(m_handle)) {
			const auto err = GetLastErrorAsString();
			failv("[SharedMemory] could not close \"%s\" (ERROR: %s)", m_name.c_str(), err.c_str())
		}
		m_handle = nullptr;
	}
#else
	if(m_ptr != nullptr) {
		if(munmap(m_ptr, m_length) != 0) {
			close(m_handle);
			failv("[SharedMemory] could not unmap \"%s\" (ERROR: %s)", m_name.c_str(), strerror(errno))
		}
		m_ptr = nullptr;
	}
	if(m_handle != 0) {
		if(close(m_handle) != 0) {
			failv("[SharedMemory] could not close \"%s\" (ERROR: %s)", m_name.c_str(), strerror(errno))
		}
		m_handle = 0;
	}
#endif

	m_buffer.Reset();
	this->Value().Delete("buffer");
	this->Value().Delete("name");
	this->Value().Delete("length");
	this->Value().Delete("requiresCopy");

	return {};
}
