#include "astrae_backend.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
static std::string take(char *p){std::string s=p?p:"{}";AstraeOffline_Free(p);return s;}
static std::string call(const char *p,const char *q){return take(AstraeOffline_Handle(p,q));}
static std::string read_state(){return take(AstraeOffline_GetState());}
int main(int argc,char **argv){
    assert(argc>=3);
    const std::string assets=argv[1],save=argv[2];
    std::filesystem::remove_all(save);
    assert(AstraeOffline_Init(save.c_str(),assets.c_str())==1);
    assert(std::string(AstraeOffline_Version()).find("1.0.0-stage4")!=std::string::npos);
    auto login=call("AccountLogin.Request","{}");
    assert(login.find("AccountUserInfo")!=std::string::npos);
    auto cheat=call("DevCheat.Request","{\"Rewards\":[{\"Type\":1,\"TableId\":1001,\"Count\":500}]}");
    assert(cheat.find("UpdatedParcelInfo")!=std::string::npos);
    auto state=read_state();
    assert(state.find("1001")!=std::string::npos);
    auto rename=call("RegisterAccountName.Request","{\"NickName\":\"Offline Tester\"}");
    assert(rename.find("Offline Tester")!=std::string::npos);
    auto catalog=take(AstraeOffline_GetProtocolCatalog());
    assert(catalog.find("requestCount\": 93")!=std::string::npos);
    assert(catalog.find("unmapped")==std::string::npos);
    auto diagnostics=take(AstraeOffline_GetDiagnostics());
    assert(diagnostics.find("\"ready\": true")!=std::string::npos);
    assert(diagnostics.find("\"schemaCount\": 359")!=std::string::npos);
    auto validation=take(AstraeOffline_ValidateRequest("CharacterLevelUp.Request","{\"TargetLevel\":\"not-an-int\"}"));
    assert(validation.find("\"valid\": false")!=std::string::npos);
    auto unknown=call("Unrecognized.Request","{}");
    assert(unknown.find("UnknownProtocol")!=std::string::npos);
    assert(AstraeOffline_ReplaceState("{\"Account\":{},\"inventory\":[{\"Type\":1,\"TableId\":1001,\"Count\":-5}]}")==0);
    AstraeOffline_Shutdown();
    assert(AstraeOffline_Init(save.c_str(),assets.c_str())==1);
    state=read_state();
    assert(state.find("Offline Tester")!=std::string::npos&&state.find("1001")!=std::string::npos);
    AstraeOffline_Shutdown();

    const std::string recovery=save+"-recovery";
    std::filesystem::remove_all(recovery);
    assert(AstraeOffline_Init(recovery.c_str(),assets.c_str())==1);
    call("DevCheat.Request","{\"Rewards\":[{\"Type\":1,\"TableId\":9001,\"Count\":5}]}");
    AstraeOffline_Shutdown();
    {std::ofstream broken(std::filesystem::path(recovery)/"save.json",std::ios::binary|std::ios::trunc);broken<<"{broken-primary";}
    assert(AstraeOffline_Init(recovery.c_str(),assets.c_str())==1);
    state=read_state();
    assert(state.find("9001")!=std::string::npos&&state.find("\"Count\": 5")!=std::string::npos);
    diagnostics=take(AstraeOffline_GetDiagnostics());
    assert(diagnostics.find("\"storeRecovery\": \"backup\"")!=std::string::npos);
    assert(std::filesystem::exists(std::filesystem::path(recovery)/"save.json.invalid"));
    AstraeOffline_Shutdown();
    std::cout<<"stage1 backend regression tests passed\n";
    return 0;
}
