#include <napi.h>
#include <node.h>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>
#include "node-shared-mem.h"

using namespace Napi;
using namespace std; //Don't if you're in a header-file


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
		fail("could not open mapping \"%s\"", path.c_str());
	}

	void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, len);
	if (data == nullptr) {
		CloseHandle(mapping);
		fail("could not map: \"%s\"", path.c_str());
	}

	this->handle = mapping;
#else
	
	#if USE_POSIX
	int key = shm_open(path.c_str(), O_RDONLY, 0644);
	if (key < 0) {
		fail("could open mapping: \"%s\"", path.c_str());
	}

	void* data = mmap(nullptr, len, 0x1, 0x1, key, 0);
	if(data == nullptr) {
		shm_unlink(path.c_str());
		fail("could not mmap");
	}
	this->handle = key;
	
	#else
	const char* folder = "/dev/shm";
	struct stat sb;
	char* filePath = new char[4096];
	


	if (stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		sprintf(filePath, "/dev/shm/%s.shm", path.c_str());
	}
	else {
		auto tmp = getenv("TMPDIR");
		if(tmp) sprintf(filePath, "%s%s.shm", getenv("TMPDIR"), path.c_str());
		else sprintf(filePath, "/tmp/%s.shm", path.c_str());
	}
	
	key_t key = ftok(filePath, 'R');
	if(key < 0) {
		fail("could open mapping: \"%s\" (%d)", filePath, errno);
	}

	key_t shmid = shmget(key, len, 0644);
	if(shmid == -1)
	{
		fail("could open mapping: \"%s\" (%d)", filePath, errno);
	}

	void* data = shmat(shmid,NULL,0);
	if(data == (void*)-1)
	{
		fail("could not map: \"%s\"", filePath);
	}

	delete filePath;
	this->handle = shmid;

	#endif

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

#ifdef _WIN32
	if(this->ptr != nullptr) {
		if(!UnmapViewOfFile(this->ptr)) {
			failv("could not unmap");
		}
		this->ptr = nullptr;
	}

	if(this->handle != nullptr) {
		if(!CloseHandle(this->handle)) {
			failv("could not close mapping");
		}
		this->handle = nullptr;
	}
#else

	#if USE_POSIX
	if(munmap(this->ptr, this->length) != 0) { 
		failv("could not unmap"); 
	}
	#else
	if(shmdt(this->ptr) != 0) { failv("could not unmap"); }
	#endif

#endif

	this->Value().Delete("buffer");
	this->Value().Delete("name");
	this->Value().Delete("length");


	// if (this->ptr == nullptr && this->handle == nullptr) {
	// 	Napi::Error::New(env, "already closed").ThrowAsJavaScriptException();
	// }
	// else {
	// 	UnmapViewOfFile(this->ptr);
	// 	CloseHandle(this->handle);
		
	// 	this->ptr = nullptr;
	// 	this->handle = nullptr;
	// 	this->Value().Delete("buffer");
	// 	this->Value().Delete("name");
	// 	this->Value().Delete("length");
	
	// }
	return Napi::Value();
}
