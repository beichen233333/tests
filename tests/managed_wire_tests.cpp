#include "managed_wire.hpp"
#include "il2cpp_runtime.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
using astrae::json::Value;
using astrae::managed::ManagedWire;
int main(int argc,char **argv){if(argc<2)return 2;std::string error;auto &wire=ManagedWire::instance();if(!wire.load_schema(argv[1],&error)){std::cerr<<error<<"\n";return 3;}auto report=astrae::json::parse(wire.schema_report());if(report["typeCount"].as_u64()!=359){std::cerr<<"expected 359 wire shapes, got "<<report["typeCount"].as_u64()<<"\n";return 4;}if(report["fieldCount"].as_u64()<1000){std::cerr<<"wire field catalog is unexpectedly small\n";return 5;}
    const auto &api=astrae::il2cpp::Runtime::instance();(void)api;
    uint64_t u=0xfedcba9876543210ULL;Value out;if(!ManagedWire::scalar_bytes_to_json('L',&u,out)||out.as_u64()!=u)return 6;int64_t i=-1234567890123LL;if(!ManagedWire::scalar_bytes_to_json('l',&i,out)||out.as_i64()!=i)return 7;float f=3.25f;if(!ManagedWire::scalar_bytes_to_json('f',&f,out)||out.as_double()!=3.25)return 8;uint8_t b=1;if(!ManagedWire::scalar_bytes_to_json('b',&b,out)||!out.as_bool())return 9;
    uint64_t roundtrip=0;Value input(uint64_t(0x1020304050607080ULL));if(!ManagedWire::json_to_scalar_bytes('L',input,&roundtrip,sizeof(roundtrip))||roundtrip!=input.as_u64())return 10;int32_t signed_value=0;input=Value(-321);if(!ManagedWire::json_to_scalar_bytes('i',input,&signed_value,sizeof(signed_value))||signed_value!=-321)return 11;uint8_t bool_value=0;input=Value(true);if(!ManagedWire::json_to_scalar_bytes('b',input,&bool_value,sizeof(bool_value))||bool_value!=1)return 12;roundtrip=0;input=Value(std::string("18446744073709551615"));if(!ManagedWire::json_to_scalar_bytes('L',input,&roundtrip,sizeof(roundtrip))||roundtrip!=UINT64_MAX)return 19;input=Value(std::string("-1"));if(ManagedWire::json_to_scalar_bytes('L',input,&roundtrip,sizeof(roundtrip)))return 20;input=Value(int64_t(2147483648LL));if(ManagedWire::json_to_scalar_bytes('i',input,&signed_value,sizeof(signed_value)))return 21;input=Value(nullptr);if(ManagedWire::json_to_scalar_bytes('i',input,&signed_value,sizeof(signed_value)))return 22;
    std::ifstream schema_file(argv[1],std::ios::binary);std::string schema_text((std::istreambuf_iterator<char>(schema_file)),std::istreambuf_iterator<char>());Value schema_root=astrae::json::parse(schema_text);std::map<std::string,std::string> pair_fields;for(const auto &shape:schema_root.as_array()){for(const auto &field:shape["fields"].as_array()){std::string element=field["element"].as_string();if(element.find("KeyValuePair<")!=std::string::npos||element.find("ValueTuple<")!=std::string::npos)pair_fields[shape["name"].as_string()+"."+field["name"].as_string()]=element;}}
    if(pair_fields.size()!=5){std::cerr<<"expected 5 generic pair collection fields, got "<<pair_fields.size()<<"\n";return 13;}
    if(pair_fields["BarracksInstallationRunTask.Response.ResultGrade"]!="KeyValuePair<int,sbyte>"&&pair_fields["BarracksInstallationRunTask.Response.ResultGrade"]!="KeyValuePair<int, sbyte>")return 14;
    if(pair_fields["BuffInstallationInfo.OpenSlots"].find("KeyValuePair<ulong, int>")==std::string::npos)return 15;
    if(pair_fields["DevCheat.Response.ScenarioPackInfos"].find("ValueTuple<ScenarioPackMiniInfo, ScenarioPackDetailInfo>")==std::string::npos)return 16;
    bool have_mini=false,have_detail=false;for(const auto &t:report["types"].as_array()){have_mini|=t.as_string()=="ScenarioPackMiniInfo";have_detail|=t.as_string()=="ScenarioPackDetailInfo";}if(!have_mini||!have_detail)return 17;
    astrae::il2cpp::Runtime::instance().initialize(&error);
#if !defined(__ANDROID__)
    if( astrae::il2cpp::Runtime::instance().available()){std::cerr<<"host must not report IL2CPP runtime as available\n";return 18;}
#endif
    std::cout<<"managed wire schema OK: "<<report["typeCount"].as_u64()<<" types, "<<report["fieldCount"].as_u64()<<" fields, "<<pair_fields.size()<<" generic pair collections\n";return 0;
}
