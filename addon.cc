#include <napi.h>
#include "node-shared-mem.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return SharedMemory::Init(env, exports);
}

NODE_API_MODULE(node_shared_mem, InitAll)