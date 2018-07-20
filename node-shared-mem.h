#ifndef NODE_SHARED_MEM_H
#define NODE_SHARED_MEM_H

//#include <node.h>
//#include <node_object_wrap.h>

#include <napi.h>
#include <windows.h>

class SharedMemory : public Napi::ObjectWrap<SharedMemory>{
	public:
		static Napi::Object Init(Napi::Env env, Napi::Object exports);
		SharedMemory(const Napi::CallbackInfo& info);

	private:
		static Napi::FunctionReference constructor;
		Napi::Value Close(const Napi::CallbackInfo& info);

		HANDLE handle;
		void* ptr;
		unsigned int length;
};

#endif