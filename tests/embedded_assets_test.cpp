#include "embedded_assets.hpp"
#include "managed_wire.hpp"
#include "backend.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>
int main(){
    std::string schema;
    assert( astrae::assets::read_embedded_asset("assets/schema/wire_shapes.json",schema));
    assert(schema.size()>200000&&schema.find("AccountLogin.Request")!=std::string::npos);
    std::string error;assert(astrae::managed::ManagedWire::instance().load_schema("assets/schema/wire_shapes.json",&error));
    char save_template[]="/tmp/astrae-embedded-assets-XXXXXX";char *save_dir=mkdtemp(save_template);assert(save_dir);
    auto &backend=astrae::Backend::instance();assert(backend.init(save_dir,""));
    std::string diagnostics=backend.diagnostics_json();assert(diagnostics.find("requestCount")!=std::string::npos);
    backend.shutdown();std::filesystem::remove_all(save_dir);
    std::puts("embedded wire schema, protocol index, tables, and default backend initialization passed");return 0;
}
