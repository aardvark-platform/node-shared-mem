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
	if (info.Length() < 2)		fail("Arguments 'name' and 'length' missing for SharedMemory()")
	if (!info[0].IsString())	fail("Argument 'name' of SharedMemory() must be a string")
	if (!info[1].IsNumber())	fail("Argument 'length' of SharedMemory() must be a number")

	auto name = info[0].As<Napi::String>().Utf8Value();
	auto length = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
	auto access = (info.Length() > 2 && info[2].IsNumber()) ? info[2].As<Napi::Number>().Int32Value() : MEMORY_ACCESS_WRITE;

#ifdef _WIN32
	int desiredAccess = 0;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_READ))    desiredAccess |= FILE_MAP_READ;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_WRITE))   desiredAccess |= FILE_MAP_WRITE;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_EXECUTE)) desiredAccess |= FILE_MAP_EXECUTE;

	HANDLE mapping = OpenFileMapping(desiredAccess, false, name.c_str());
	if (mapping == nullptr) {
		auto err = GetLastErrorAsString();
		fail("Could not open shared memory object \"%s\" (ERROR: %s)", name.c_str(), err.c_str())
	}

	void* data = MapViewOfFile(mapping, desiredAccess, 0, 0, length);
	if (data == nullptr) {
		auto err = GetLastErrorAsString();
		CloseHandle(mapping);
		fail("Could not map shared memory object \"%s\" (ERROR: %s)", name.c_str(), err.c_str())
	}

	m_handle = mapping;
#else
	#if !__APPLE__
	if (name.empty() || !name.starts_with('/')) {
		name.insert(0, 1, '/');
	}
	#endif

	int oflag = HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_WRITE) ? O_RDWR : O_RDONLY;
	int fd = shm_open(name.c_str(), oflag, 0);
	if (fd < 0) {
		fail("Could not open shared memory object \"%s\" (ERROR: %s)", name.c_str(), strerror(errno))
	}

	int prot = 0;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_READ))    prot |= PROT_READ;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_WRITE))   prot |= PROT_WRITE;
	if (HAS_MEMORY_ACCESS(access, MEMORY_ACCESS_EXECUTE)) prot |= PROT_EXEC;

	void* data = mmap(nullptr, length, prot, MAP_SHARED, fd, 0);
	if(data == nullptr) {
		fail("Could not map shared memory object \"%s\" (ERROR: %s)", name.c_str(), strerror(errno))
	}

	m_handle = fd;
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
		fail("Could not allocate array buffer for shared memory object \"%s\" (error = %d)", name.c_str(), status);
	}

	this->Value().Set("name", info[0]);
	this->Value().Set("length", info[1]);
	this->Value().Set("access", access);

	m_ptr = static_cast<uint8_t*>(data);
	m_name = name;
	m_length = length;
}


Napi::Object SharedMemory::Init(Napi::Env env, Napi::Object exports) {

	const Napi::Function sharedMemory = DefineClass(env, "SharedMemory", {
		InstanceMethod("copyFrom", &SharedMemory::CopyFrom),
		InstanceMethod("copyTo", &SharedMemory::CopyTo),
		InstanceMethod("close", &SharedMemory::Close)
	});

	const Napi::Object memoryAccess = Napi::Object::New(env);
	memoryAccess.Set("READ",    Napi::Number::New(env, MEMORY_ACCESS_READ));
	memoryAccess.Set("WRITE",   Napi::Number::New(env, MEMORY_ACCESS_WRITE));
	memoryAccess.Set("EXECUTE", Napi::Number::New(env, MEMORY_ACCESS_EXECUTE));
	memoryAccess.Freeze();

	exports.Set("SharedMemory", sharedMemory);
	exports.Set("MemoryAccess", memoryAccess);
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
			failv("Could not unmap shared memory object \"%s\" (ERROR: %s)", m_name.c_str(), err.c_str())
		}
		m_ptr = nullptr;
	}

	if(m_handle != nullptr) {
		if(!CloseHandle(m_handle)) {
			const auto err = GetLastErrorAsString();
			failv("Could not close shared memory object \"%s\" (ERROR: %s)", m_name.c_str(), err.c_str())
		}
		m_handle = nullptr;
	}
#else
	if(m_ptr != nullptr) {
		if(munmap(m_ptr, m_length) != 0) {
			close(m_handle);
			failv("Could not unmap shared memory object \"%s\" (ERROR: %s)", m_name.c_str(), strerror(errno))
		}
		m_ptr = nullptr;
	}
	if(m_handle != 0) {
		if(close(m_handle) != 0) {
			failv("Could not close shared memory object \"%s\" (ERROR: %s)", m_name.c_str(), strerror(errno))
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
