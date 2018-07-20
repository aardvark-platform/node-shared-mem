#include <nan.h>
#include "node-shared-mem.h"

void InitAll(v8::Local<v8::Object> exports) {
  node_shared_mem::NodeSharedMem::Init(exports);
}

NODE_MODULE(addon, InitAll)