#include "managed_invoke_args.hpp"
#include <cassert>
#include <cstdint>
int main(){
    uint64_t value=0x1122334455667788ULL;void *managed_object=reinterpret_cast<void *>(uintptr_t(0x12345000));
    void *value_args[1]={astrae::il2cpp::invoke_args::value(&value)};
    void *reference_args[1]={astrae::il2cpp::invoke_args::reference(managed_object)};
    assert(value_args[0]==&value);
    assert(reference_args[0]==managed_object);
    assert(astrae::il2cpp::invoke_args::reference(nullptr)==nullptr);
    assert(reference_args[0]!=static_cast<void *>(&managed_object));
    return 0;
}
