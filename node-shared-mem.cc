#include <napi.h>
#include <node.h>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>
#include "node-shared-mem.h"

using namespace Napi;
using namespace std; //Don't if you're in a header-file

template<typename ... Args>
static std::string format(const std::string& format, Args ... args) {
	size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	auto str = string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
	return str;
}

#define fail(...) { Napi::Error::New(env, format(__VA_ARGS__)).ThrowAsJavaScriptException(); return; }
#define failt(...) { Napi::TypeError::New(env, format(__VA_ARGS__)).ThrowAsJavaScriptException(); return; }
#define failv(...) { Napi::Error::New(env, format(__VA_ARGS__)).ThrowAsJavaScriptException(); return Napi::Value(); }






Napi::FunctionReference SharedMemory::constructor;

SharedMemory::SharedMemory(const Napi::CallbackInfo& info) : Napi::ObjectWrap<SharedMemory>(info) 
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);

	// Invoked as constructor: `new MyObject(...)`
	if (info.Length() < 2)		fail("needs mapName and mapSize");
	if (!info[0].IsString())	failt("argument 0 needs to be a valid mapName");
	if (!info[1].IsNumber())	failt("argument 1 needs to be a valid mapLength");

	auto path = info[0].As<Napi::String>().Utf8Value();
	auto len = (int64_t)info[1].As<Napi::Number>();

	HANDLE mapping = OpenFileMapping(PAGE_READWRITE, false, path.c_str());
	if (mapping == nullptr) {
		fail("could not open mapping \"%s\"", path.c_str());
	}

	void* data = MapViewOfFile(mapping, FILE_READ_ACCESS, 0, 0, len);
	if (data == nullptr) {
		CloseHandle(mapping);
		fail("could not map: \"%s\"", path.c_str());
	}

	auto a = Napi::ArrayBuffer::New(env, data, (size_t)len);
	this->Value().Set("buffer", a);
	this->Value().Set("name", info[0]);
	this->Value().Set("length", info[1]);

	this->handle = mapping;
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
