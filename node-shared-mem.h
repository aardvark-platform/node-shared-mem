#ifndef NODE_SHARED_MEM_H
#define NODE_SHARED_MEM_H

#include <napi.h>
#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
typedef HANDLE HANDLE;
#else
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
typedef key_t HANDLE;
#endif

#define MEMORY_ACCESS_READ 0
#define MEMORY_ACCESS_WRITE 1
#define MEMORY_ACCESS_EXECUTE 2
#define HAS_MEMORY_ACCESS(mask, access) (((mask) & (access)) == (access))

class SharedMemory : public Napi::ObjectWrap<SharedMemory>{
	public:
		static Napi::Object Init(Napi::Env env, Napi::Object exports);
		SharedMemory(const Napi::CallbackInfo& info);

	private:
		Napi::Value CopyFrom(const Napi::CallbackInfo& info);
		Napi::Value CopyTo(const Napi::CallbackInfo& info);
		Napi::Value Close(const Napi::CallbackInfo& info);

		HANDLE m_handle;
		uint8_t* m_ptr;
		size_t m_length;
		std::string m_name;
		Napi::Reference<Napi::ArrayBuffer> m_buffer;
};

#endif