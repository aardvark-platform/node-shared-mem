#include <nan.h>
#include <node.h>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>
#include "node-shared-mem.h"

using namespace v8;
using namespace Nan;

namespace node_shared_mem {

	using namespace std; //Don't if you're in a header-file

	template<typename ... Args>
	static void seterror(const std::string& format, Args ... args) {
		size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
		unique_ptr<char[]> buf(new char[size]);
		snprintf(buf.get(), size, format.c_str(), args ...);
		auto str = string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
		Nan::ThrowError(str.c_str());
	}

	#define fail(...) { seterror(__VA_ARGS__); return; }

	Nan::Persistent<Function> NodeSharedMem::constructor;

	NodeSharedMem::NodeSharedMem(HANDLE handle, void* ptr, unsigned int length)
	{
		this->handle = handle;
		this->ptr = ptr;
		this->length = length;
	}

	NodeSharedMem::~NodeSharedMem() {

	}

	void NodeSharedMem::Init(Local<Object> exports) {

		// Prepare constructor template
		Local<FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
		tpl->SetClassName(Nan::New("NodeSharedMem").ToLocalChecked());
		tpl->InstanceTemplate()->SetInternalFieldCount(3);

		// Prototype
		Nan::SetPrototypeMethod(tpl, "close", Close);

		constructor.Reset(tpl->GetFunction());
		exports->Set(Nan::New("NodeSharedMem").ToLocalChecked(), tpl->GetFunction());

	}

	void NodeSharedMem::New(const Nan::FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();

		if (args.IsConstructCall()) {
			// Invoked as constructor: `new MyObject(...)`
			if (args.Length() < 2) fail("needs mapName and mapSize");
			if (!args[0]->IsString()) fail("argument 0 needs to be a valid mapName");
			if (!args[1]->IsNumber()) fail("argument 1 needs to be a valid mapLength");

			Nan::Utf8String path(args[0]);
			unsigned int len = args[1]->Uint32Value();

			HANDLE mapping = OpenFileMapping(PAGE_READWRITE, false, *path);
			if (mapping == nullptr) {
				fail("could not open mapping \"%s\"", *path);
			}

			void* data = MapViewOfFile(mapping, FILE_READ_ACCESS, 0, 0, len);
			if (data == nullptr) {
				CloseHandle(mapping);
				fail("could not map: \"%s\"", *path);
			}

			auto buffer = ArrayBuffer::New(isolate, data, (size_t)len);
			NodeSharedMem* obj = new NodeSharedMem(mapping, data, len);

			obj->Wrap(args.This());
			args.This()->Set(String::NewFromUtf8(isolate, "buffer"), buffer);
			args.GetReturnValue().Set(args.This());
		}
		else {
			// Invoked as plain function `MyObject(...)`, turn into construct call.
			const int argc = 1;
			Local<Value> argv[argc] = { args[0] };
			Local<Context> context = isolate->GetCurrentContext();
			Local<Function> cons = Local<Function>::New(isolate, constructor);
			Local<Object> result =
				cons->NewInstance(context, argc, argv).ToLocalChecked();
			args.GetReturnValue().Set(result);
		}
	}

	void NodeSharedMem::Close(const Nan::FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();
		NodeSharedMem* obj = ObjectWrap::Unwrap<NodeSharedMem>(args.Holder());

		if (!obj->handle) {
			fail("already closed");
		}
		else {

			UnmapViewOfFile(obj->ptr);
			CloseHandle(obj->handle);
			obj->ptr = nullptr;
			obj->handle = nullptr;
		}
	}

	NODE_MODULE(node_shared_mem, node_shared_mem::NodeSharedMem::Init)
}
