#ifndef NODE_SHARED_MEM_H
#define NODE_SHARED_MEM_H

#include <node.h>
#include <node_object_wrap.h>

#include <WinBase.h>

namespace node_shared_mem {
	class SharedMemory : public node::ObjectWrap {
		public:
			static void Init(v8::Local<v8::Object> exports);

		private:
			explicit SharedMemory(HANDLE handle, void* ptr, unsigned int length);
			~SharedMemory();

			static void New(const Nan::FunctionCallbackInfo<v8::Value>& args);
			static void Close(const Nan::FunctionCallbackInfo<v8::Value>& args);
			static Nan::Persistent<v8::Function> constructor;

			HANDLE handle;
			void* ptr;
			unsigned int length;
	};
}

#endif