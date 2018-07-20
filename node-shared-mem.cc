#include <nan.h>
#include <node.h>
#include "node-shared-mem.h"

using namespace v8;
using namespace Nan;

namespace node_shared_mem {
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
			if (args.Length() < 2) return Nan::ThrowError("needs mapName and mapSize");

			Nan::Utf8String path(args[0]);
			unsigned int len = args[1]->Uint32Value();

			HANDLE mapping = OpenFileMapping(PAGE_READWRITE, false, *path);
			if (mapping == 0) {
				return Nan::ThrowError(Nan::New("could not map file").ToLocalChecked());
			}

			void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);


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

		UnmapViewOfFile(obj->ptr);
		CloseHandle(obj->handle);
	}

	NODE_MODULE(node_shared_mem, node_shared_mem::NodeSharedMem::Init)
	//NODE_MODULE(NODE_GYP_MODULE_NAME, node_shared_mem::NodeSharedMem::Init)
}

//
//void fClose(const FunctionCallbackInfo<Value> &info)
//{
//	info.Data();
//	UnmapViewOfFile();
//	CloseHandle();
//}
//
//NAN_METHOD(Print) {
//	if (info.Length() < 2) return Nan::ThrowError("must pass a map-name and a map-size");
//	if (!info[0]->IsString()) return Nan::ThrowError("argument 0 must be a map-name");
//	if (!info[1]->IsNumber()) return Nan::ThrowError("argument 1 must be a map-size");
//
//	Nan::Utf8String path(info[0]);
//	unsigned int len = info[1]->Uint32Value();
//
//	HANDLE mapping = OpenFileMapping(PAGE_READWRITE, false, *path);
//	if(mapping == 0) { 
//		return Nan::ThrowError(Nan::New("could not map file").ToLocalChecked()); 
//	}
//
//	const void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
//
//	printf("data: %s\n", data);
//
//	auto isolate = info.GetIsolate();
//	auto buffer = ArrayBuffer::New(isolate, (void*)data, (size_t)len);
//
//	auto tpl = FunctionTemplate::New(isolate, fClose);
//	auto close = tpl->GetFunction();
//
//	auto object = Object::New(isolate);
//
//	auto kBuffer = String::NewFromUtf8(isolate, "buffer", NewStringType::kInternalized);
//	auto kClose = String::NewFromUtf8(isolate, "close", NewStringType::kInternalized);
//	object->Set(kBuffer.ToLocalChecked(), buffer);
//	object->Set(kClose.ToLocalChecked(), close);
//
//	info.GetReturnValue().Set(buffer);
//}
//
//NAN_MODULE_INIT(InitAll) {
//  Nan::Set(target, Nan::New<String>("print").ToLocalChecked(), Nan::GetFunction(Nan::New<FunctionTemplate>(Print)).ToLocalChecked());
//}
//
//NODE_MODULE(a_native_module, InitAll)
