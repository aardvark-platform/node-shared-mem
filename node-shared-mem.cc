#include <napi.h>
#include <node.h>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>
#include "node-shared-mem.h"

using namespace Napi;
using namespace std; //Don't if you're in a header-file

#ifdef _WIN32

// trim from end of string (right)
inline std::string& rtrim(std::string& s)
{
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
    return s;
}

// trim from beginning of string (left)
inline std::string& ltrim(std::string& s)
{
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
    return s;
}

// trim from both ends of string (right then left)
inline std::string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

std::string GetLastErrorAsString()
{
    //Get the error message, if any.
    DWORD errorMessageID = ::GetLastError();
    if(errorMessageID == 0)
        return std::string(); //No error message has been recorded

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    //Free the buffer.
    LocalFree(messageBuffer);

    return trim(message);
}

#endif


#define failFormat(...) { \
		size_t size = snprintf(nullptr, 0, __VA_ARGS__) + 1; \
		unique_ptr<char[]> buf(new char[size]); \
		snprintf(buf.get(), size, __VA_ARGS__); \
		auto str = string(buf.get(), buf.get() + size - 1); \
		Napi::Error::New(env, str).ThrowAsJavaScriptException(); \
	}

#define fail(...) { failFormat(__VA_ARGS__); return; }
#define failt(...) { failFormat(__VA_ARGS__); return; }
#define failv(...) { failFormat(__VA_ARGS__); return Napi::Value(); }

Napi::FunctionReference SharedMemory::constructor;

SharedMemory::SharedMemory(const Napi::CallbackInfo& info) : Napi::ObjectWrap<SharedMemory>(info) 
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);

	// Invoked as constructor: `new MyObject(...)`
	if (info.Length() < 2)		fail("needs mapName and mapSize");
	if (!info[0].IsString())	failt("argument 0 needs to be a valid mapName");
	if (!info[1].IsNumber())	failt("argument 1 needs to be a valid mapLength");

	//this->name = info[0].As<Napi::String>();
	auto path = info[0].As<Napi::String>().Utf8Value();
	auto len = (int64_t)info[1].As<Napi::Number>();
#ifdef _WIN32
	HANDLE mapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, path.c_str());
	if (mapping == nullptr) {
		auto err = GetLastErrorAsString();
		fail("[SharedMemory] could not open \"%s\" (ERROR: %s)", path.c_str(), err.c_str());
	}

	void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, len);
	if (data == nullptr) {
		auto err = GetLastErrorAsString();
		CloseHandle(mapping);
		fail("[SharedMemory] could not map: \"%s\" (ERROR: %s)", path.c_str(), err.c_str());
	}

	this->handle = mapping;
#else
	
    char filePath[4096];
	#if __APPLE__
    sprintf(filePath, "%s", path.c_str());
    #else
    sprintf(filePath, "/%s", path.c_str());
	#endif
	int key = shm_open(filePath, O_RDWR, 0777);
	if (key < 0) {
		fail("[SharedMemory] could open \"%s\" (ERROR: %s)", path.c_str(), strerror(errno));
	}

	void* data = mmap(nullptr, len, 0x3, MAP_SHARED, key, 0);
	if(data == nullptr) {
		shm_unlink(path.c_str());
		fail("[SharedMemory] could not map \"%s\" (f%d) (ERROR: %s)", path.c_str(), key, strerror(errno));
	}
	this->handle = key;
	

#endif

	auto a = Napi::ArrayBuffer::New(env, data, (size_t)len);
	this->Value().Set("buffer", a);
	this->Value().Set("name", info[0]);
	this->Value().Set("length", info[1]);

	this->ptr = data;
	this->length = len;
}


Napi::Object SharedMemory::Init(Napi::Env env, Napi::Object exports) {
	Napi::HandleScope scope(env);

	Napi::Function func = DefineClass(env, "SharedMemory", {
		InstanceMethod("close", &SharedMemory::Close)
	});

	constructor = Napi::Persistent(func);
	constructor.SuppressDestruct();
	exports.Set("SharedMemory", func);
	return exports;
}

Napi::Value SharedMemory::Close(const Napi::CallbackInfo& info) {
	
	Napi::Env env = info.Env();

	auto hasName = this->Value().Get("name").IsString();
	auto name = "UNKNOWN";
	if(hasName) name = this->Value().Get("name").As<Napi::String>().Utf8Value().c_str();

#ifdef _WIN32
	if(this->ptr != nullptr) {
		if(!UnmapViewOfFile(this->ptr)) {
			auto err = GetLastErrorAsString();
			CloseHandle(this->handle);
			failv("[SharedMemory] could not unmap \"%s\" (ERROR: %s)", name, err.c_str());
		}
		this->ptr = nullptr;
	}

	if(this->handle != nullptr) {
		if(!CloseHandle(this->handle)) {
			auto err = GetLastErrorAsString();
			failv("[SharedMemory] could not close \"%s\" (ERROR: %s)", name, err.c_str());
		}
		this->handle = nullptr;
	}
#else

	if(this->ptr != nullptr) {
		if(munmap(this->ptr, this->length) != 0) { 
			close(this->handle);
			failv("[SharedMemory] could not unmap \"%s\" (ERROR: %s)", name, strerror(errno)); 
		}
		this->ptr = nullptr;
	}
	if(this->handle != 0) {
		if(close(this->handle) != 0) {
			failv("[SharedMemory] could not close \"%s\" (ERROR: %s)", name, strerror(errno)); 
		}
		this->handle = 0;
	}
#endif

	this->Value().Delete("buffer");
	this->Value().Delete("name");
	this->Value().Delete("length");


	return Napi::Value();
}
