#pragma once
#include "il2cpp_runtime.hpp"
#include "json.hpp"
#include <map>
#include <string>
#include <vector>

namespace astrae::managed {
struct FieldShape {std::string name,type,element;int offset=0;char kind='?';};
struct TypeShape {std::string name;bool is_struct=false;std::vector<FieldShape> fields;};
class SchemaCatalog {
public:
    bool load(const std::string &path,std::string *error=nullptr);
    const TypeShape *find(const std::string &name) const;
    size_t size() const {return shapes_.size();}
    size_t field_count() const {return field_count_;}
    std::string report_json() const;
private:
    std::map<std::string,TypeShape> shapes_;
    size_t field_count_=0;
};
class ManagedWire {
public:
    static ManagedWire &instance();
    bool load_schema(const std::string &path,std::string *error=nullptr);
    bool schema_loaded() const {return catalog_.size()!=0;}
    std::string schema_report() const {return catalog_.report_json();}
    std::string read_object_json(const std::string &type,const void *object,std::string *error=nullptr) const;
    uint32_t build_object_handle(const std::string &type,const std::string &json,std::string *error=nullptr) const;
    static bool scalar_bytes_to_json(char kind,const void *bytes,json::Value &out);
    static bool json_to_scalar_bytes(char kind,const json::Value &value,void *bytes,size_t size);
private:
    SchemaCatalog catalog_;
    struct BuildContext {std::vector<uint32_t> roots;void *root_object=nullptr;size_t nodes=0;};
    json::Value read_shape(const std::string &type,const void *base,void *owner_class,bool value_type,int depth,size_t &nodes,std::string *error) const;
    json::Value read_collection(const FieldShape &field,void *collection,void *field_class,int depth,size_t &nodes,std::string *error) const;
    json::Value read_element(const std::string &type,char kind,const void *bytes,void *boxed,void *element_class,int depth,size_t &nodes,std::string *error) const;
    json::Value read_pair(const std::string &type,const void *bytes,void *boxed,void *pair_class,int depth,size_t &nodes,std::string *error) const;
    void *build_shape(const std::string &type,const json::Value &value,void *inline_data,void *barrier_owner,int depth,BuildContext &ctx,std::string *error) const;
    bool build_field(const FieldShape &field,const json::Value &value,void *base,void *owner_object,void *owner_class,int depth,BuildContext &ctx,std::string *error) const;
    void *build_collection(const FieldShape &field,const json::Value &value,void *field_class,void *owner_object,void *destination,int depth,BuildContext &ctx,std::string *error) const;
    void *build_element(const std::string &type,char kind,const json::Value &value,void *element_class,int depth,BuildContext &ctx,void **argument_data,std::string *error) const;
    void *build_pair(const std::string &type,void *pair_class,const json::Value &value,int depth,BuildContext &ctx,void **argument_data,std::string *error) const;
};
}
