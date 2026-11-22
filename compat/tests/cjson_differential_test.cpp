#include "cJSON.h"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <vector>
struct CJSONApi {
    const char *(*version)();
    cJSON *(*parse)(const char *);
    cJSON *(*parse_len_opts)(const char *,size_t,const char **,cJSON_bool);
    char *(*print)(const cJSON *);
    char *(*print_unformatted)(const cJSON *);
    char *(*print_buffered)(const cJSON *,int,cJSON_bool);
    cJSON_bool (*print_preallocated)(cJSON *,char *,int,cJSON_bool);
    cJSON *(*create_number)(double);
    cJSON *(*create_object)();
    cJSON *(*create_string)(const char *);
    cJSON_bool (*add_number)(const cJSON *,const char *,double);
    cJSON_bool (*add_item_cs)(cJSON *,const char *,cJSON *);
    cJSON *(*create_raw)(const char *);
    cJSON *(*duplicate)(const cJSON *,cJSON_bool);
    cJSON_bool (*compare)(const cJSON *,const cJSON *,cJSON_bool);
    char *(*set_valuestring)(cJSON *,const char *);
    void (*minify)(char *);
    void (*del)(cJSON *);
    void (*free_fn)(void *);
    const char *(*error_ptr)();
};
static void *load_symbol(void *h,const char *n){void *p=dlsym(h,n);if(!p){std::cerr<<"missing cJSON symbol "<<n<<"\n";std::abort();}return p;}
static CJSONApi load_api(void *h){CJSONApi a{};
#define LOAD(field,name) a.field=reinterpret_cast<decltype(a.field)>(load_symbol(h,name))
    LOAD(version,"cJSON_Version");LOAD(parse,"cJSON_Parse");LOAD(parse_len_opts,"cJSON_ParseWithLengthOpts");LOAD(print,"cJSON_Print");LOAD(print_unformatted,"cJSON_PrintUnformatted");LOAD(print_buffered,"cJSON_PrintBuffered");LOAD(print_preallocated,"cJSON_PrintPreallocated");LOAD(create_number,"cJSON_CreateNumber");LOAD(create_object,"cJSON_CreateObject");LOAD(create_string,"cJSON_CreateString");LOAD(add_number,"cJSON_AddNumberToObject");LOAD(add_item_cs,"cJSON_AddItemToObjectCS");LOAD(create_raw,"cJSON_CreateRaw");LOAD(duplicate,"cJSON_Duplicate");LOAD(compare,"cJSON_Compare");LOAD(set_valuestring,"cJSON_SetValuestring");LOAD(minify,"cJSON_Minify");LOAD(del,"cJSON_Delete");LOAD(free_fn,"cJSON_free");LOAD(error_ptr,"cJSON_GetErrorPtr");
#undef LOAD
    return a;}
static std::string print_text(const CJSONApi &a,const cJSON *n,bool pretty){char *p=pretty?a.print(n):a.print_unformatted(n);assert(p);std::string s=p;a.free_fn(p);return s;}
int main(int argc,char **argv){
    assert(argc==3);void *ours=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#ifdef RTLD_DEEPBIND
    void *oracle=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
#else
    void *oracle=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(ours&&oracle);const CJSONApi a=load_api(ours),b=load_api(oracle);
    const std::vector<std::string> samples={"null","true","false","0","-0.25","0.1","1e-9","1e+30","1e999","1e+","01","{}","[]","[1,2,3]","{\"z\":1,\"a\":\"雪\\uD83D\\uDE03\",\"b\":[true,false,null]}"," { \"nested\" : { \"x\" : 1 } } ","[\"quote\\\"\",\"slash/\\\\\",\"line\\nfeed\"]","1.2345678901234567","1e-9999","1e9999","2.2250738585072014e-308","9007199254740991","-2147483649","2147483648","-0","-0.0","1.2300e+02"};
    for(const auto &src:samples){cJSON *x=a.parse(src.c_str()),*y=b.parse(src.c_str());assert((x==nullptr)==(y==nullptr));if(x){const std::string au=print_text(a,x,false),bu=print_text(b,y,false),af=print_text(a,x,true),bf=print_text(b,y,true);if(au!=bu){std::cerr<<"compact mismatch for: "<<src<<"\nours="<<au<<"\nref="<<bu<<"\n";return 11;}if(af!=bf){std::cerr<<"pretty mismatch for: "<<src<<"\nours="<<af<<"\nref="<<bf<<"\n";return 12;}cJSON *dx=a.duplicate(x,1),*dy=b.duplicate(y,1);assert(dx&&dy);if(print_text(a,dx,false)!=print_text(b,dy,false)){std::cerr<<"duplicate mismatch\n";return 13;}a.del(dx);b.del(dy);a.del(x);b.del(y);}}
    cJSON *na=a.create_number(0.1),*nb=b.create_number(0.1);assert(print_text(a,na,false)==print_text(b,nb,false));a.del(na);b.del(nb);
    cJSON *buffered_a=a.parse("{\"prebuffer\":[1,2,3],\"nested\":{\"x\":true}}"),*buffered_b=b.parse("{\"prebuffer\":[1,2,3],\"nested\":{\"x\":true}}");assert(buffered_a&&buffered_b);for(int pre:{0,1,8,64,256,1024})for(int fmt=0;fmt<=1;++fmt){char *ba=a.print_buffered(buffered_a,pre,fmt),*bb=b.print_buffered(buffered_b,pre,fmt);assert(ba&&bb);if(std::strcmp(ba,bb)!=0){std::cerr<<"PrintBuffered mismatch pre="<<pre<<" fmt="<<fmt<<"\n";return 20;}a.free_fn(ba);b.free_fn(bb);}a.del(buffered_a);b.del(buffered_b);
    cJSON *near_a=a.create_number(1.0),*near_b=b.create_number(1.0+2.2204460492503131e-16);assert(a.compare(near_a,near_b,1)==b.compare(near_a,near_b,1));a.del(near_a);a.del(near_b);
    cJSON *sa=a.create_string("original"),*sb=b.create_string("original");assert(sa&&sb);assert(a.set_valuestring(sa,"x")&&b.set_valuestring(sb,"x"));assert(std::strcmp(sa->valuestring,sb->valuestring)==0);assert(a.set_valuestring(sa,"a longer replacement")&&b.set_valuestring(sb,"a longer replacement"));assert(std::strcmp(sa->valuestring,sb->valuestring)==0);/* The target binary reports cJSON 1.7.19. Its upstream implementation rejects
       overlapping source/destination buffers in cJSON_SetValuestring. The host
       oracle is 1.7.18 and accepts exact self-aliasing, so this one versioned
       behavior is asserted against the 1.7.19 source contract, not compared. */
    const bool alias_a=a.set_valuestring(sa,sa->valuestring)==nullptr;assert(alias_a);(void)b.set_valuestring(sb,sb->valuestring);a.del(sa);b.del(sb);
    cJSON *oa=a.parse("{\"a\":1,\"b\":2}"),*ob=b.parse("{\"b\":2,\"a\":1}");assert(oa&&ob);assert(a.compare(oa,ob,1)==b.compare(oa,ob,1));a.del(oa);b.del(ob);
    const char prefix[]="{\"a\":1} trailing";const char *ea=nullptr,*eb=nullptr;cJSON *pa=a.parse_len_opts(prefix,sizeof(prefix)-1,&ea,0),*pb=b.parse_len_opts(prefix,sizeof(prefix)-1,&eb,0);assert(pa&&pb);assert((ea-prefix)==(eb-prefix));a.del(pa);b.del(pb);
    const char *default_prefix="true trailing";cJSON *dpa=a.parse(default_prefix),*dpb=b.parse(default_prefix);assert(dpa&&dpb);assert(print_text(a,dpa,false)==print_text(b,dpb,false));a.del(dpa);b.del(dpb);
    const std::vector<std::string> malformed={"{","[1,]","{\"a\":}","\"unterminated","{\"x\" 2}","[1 2]","\"bad\\q\"","[true false]","{\"a\":1,}","[\"\\uD800\"]"};
    for(const auto &src:malformed){cJSON *ma=a.parse(src.c_str()),*mb=b.parse(src.c_str());if((ma==nullptr)!=(mb==nullptr)){std::cerr<<"malformed parse mismatch: "<<src<<"\n";return 16;}if(ma){if(print_text(a,ma,false)!=print_text(b,mb,false)){std::cerr<<"malformed accepted value mismatch: "<<src<<"\n";return 17;}a.del(ma);b.del(mb);}const char *ea2=a.error_ptr(),*eb2=b.error_ptr();if(!ma&&!mb&&ea2&&eb2&&(ea2-src.c_str())!=(eb2-src.c_str())){std::cerr<<"error offset mismatch: "<<src<<" ours="<<(ea2-src.c_str())<<" ref="<<(eb2-src.c_str())<<"\n";return 18;}}
    const char *bad="{";cJSON *bad_a=a.parse(bad),*bad_b=b.parse(bad);assert(!bad_a&&!bad_b);assert(a.error_ptr()&&b.error_ptr());assert((a.error_ptr()-bad)==(b.error_ptr()-bad));
    char min_a[]=" { \"a\" : 1, //x\n \"b\" : [2,3] } ",min_b[]=" { \"a\" : 1, //x\n \"b\" : [2,3] } ";a.minify(min_a);b.minify(min_b);assert(std::strcmp(min_a,min_b)==0);
    const char nul_input[]="{\"a\":1}";cJSON *nul_a=a.parse_len_opts(nul_input,sizeof(nul_input),nullptr,1),*nul_b=b.parse_len_opts(nul_input,sizeof(nul_input),nullptr,1);assert((nul_a!=nullptr)==(nul_b!=nullptr)&&nul_a);a.del(nul_a);b.del(nul_b);
    cJSON *obj_a=a.create_object(),*obj_b=b.create_object();assert(obj_a&&obj_b);assert(a.add_number(obj_a,"n",42)&&b.add_number(obj_b,"n",42));
    cJSON *arr_a=a.parse("[1,2,3]"),*arr_b=b.parse("[1,2,3]");assert(arr_a&&arr_b);assert(arr_a->child->prev->valuedouble==3&&arr_b->child->prev->valuedouble==3);a.del(arr_a);b.del(arr_b);
    cJSON *sh_a=a.parse("[1,2]"),*sh_b=b.parse("[1,2]");cJSON *shallow_a=a.duplicate(sh_a,0),*shallow_b=b.duplicate(sh_b,0);assert(shallow_a&&shallow_b&&!shallow_a->child&&!shallow_b->child);a.del(shallow_a);b.del(shallow_b);a.del(sh_a);b.del(sh_b);
    cJSON *cs_a=a.create_object(),*cs_b=b.create_object();cJSON *cn_a=a.create_number(7),*cn_b=b.create_number(7);assert(cs_a&&cs_b&&cn_a&&cn_b);assert(a.add_item_cs(cs_a,"literal-key",cn_a)&&b.add_item_cs(cs_b,"literal-key",cn_b));assert((cn_a->type&512)&&(cn_b->type&512));cJSON *cd_a=a.duplicate(cs_a,1),*cd_b=b.duplicate(cs_b,1);assert(cd_a&&cd_b&&cd_a->child&&cd_b->child);assert((cd_a->child->type&512)&&(cd_b->child->type&512));assert(cd_a->child->string==cn_a->string&&cd_b->child->string==cn_b->string);a.del(cd_a);b.del(cd_b);a.del(cs_a);b.del(cs_b);
    const std::string expected_print=print_text(a,obj_a,false);
    for(int cap=1;cap<=static_cast<int>(expected_print.size())+8;++cap){char ba[128],bb[128];std::memset(ba,'X',sizeof(ba));std::memset(bb,'X',sizeof(bb));const int ra=a.print_preallocated(obj_a,ba,cap,0),rb=b.print_preallocated(obj_b,bb,cap,0);if(ra!=rb){std::cerr<<"preallocated result mismatch cap="<<cap<<" ours="<<ra<<" ref="<<rb<<"\n";return 14;}if(std::memcmp(ba,bb,(size_t)cap)!=0){std::cerr<<"preallocated buffer contents mismatch cap="<<cap<<"\n";return 15;}}
    a.del(obj_a);b.del(obj_b);
    std::cout<<"cJSON differential cases passed against "<<b.version()<<" (rebuild="<<a.version()<<")\n";dlclose(oracle);dlclose(ours);return 0;
}
