#include "cJSON.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cstdint>
#include <string>
#include <vector>

namespace {
std::mutex g_hooks_mutex;
cJSON_Hooks g_hooks{std::malloc,std::free};
void *(*g_realloc_fn)(void *,size_t)=std::realloc;
const char *g_error_ptr=nullptr;
std::mutex g_error_mutex;
void *alloc_raw(size_t n){void *(*fn)(size_t)=nullptr;{std::lock_guard<std::mutex> l(g_hooks_mutex);fn=g_hooks.malloc_fn?g_hooks.malloc_fn:std::malloc;}return fn(n);}
void free_raw(void *p){if(!p)return;void (*fn)(void*)=nullptr;{std::lock_guard<std::mutex> l(g_hooks_mutex);fn=g_hooks.free_fn?g_hooks.free_fn:std::free;}fn(p);}
void clear_error(){std::lock_guard<std::mutex> l(g_error_mutex);g_error_ptr=nullptr;}
void set_error(const char *p){std::lock_guard<std::mutex> l(g_error_mutex);if(!g_error_ptr)g_error_ptr=p;}
const char *get_error(){std::lock_guard<std::mutex> l(g_error_mutex);return g_error_ptr;}
char *dup_s(const char *s){if(!s)return nullptr;size_t n=std::strlen(s)+1;char *p=(char*)alloc_raw(n);if(p)std::memcpy(p,s,n);return p;}
cJSON *new_node(int type){auto *n=(cJSON*)alloc_raw(sizeof(cJSON));if(!n)return nullptr;std::memset(n,0,sizeof(*n));n->type=type;return n;}
void append_child(cJSON *parent,cJSON *item){
    if(!parent||!item)return;
    item->next=nullptr;
    if(!parent->child){item->prev=item;parent->child=item;return;}
    cJSON *head=parent->child;
    cJSON *tail=head->prev;
    if(!tail){tail=head;while(tail->next)tail=tail->next;}
    tail->next=item;item->prev=tail;head->prev=item;
}
bool eq_name(const char *a,const char *b,bool cs){if(!a||!b)return false;return cs?std::strcmp(a,b)==0:strcasecmp(a,b)==0;}
cJSON *get_object(const cJSON *o,const char *key,bool cs){if(!o||!key)return nullptr;for(cJSON *p=o->child;p;p=p->next){if(cs&&p->string==nullptr)break;if(eq_name(p->string,key,cs))return p;}return nullptr;}
void delete_one(cJSON *n){if(!n)return;if(!(n->type&cJSON_IsReference)){cJSON *p=n->child;while(p){cJSON *next=p->next;delete_one(p);p=next;}if(!(n->type&cJSON_StringIsConst))free_raw(n->string);free_raw(n->valuestring);}free_raw(n);}
void utf8_append(std::string &o,uint32_t cp){if(cp<=0x7f)o.push_back((char)cp);else if(cp<=0x7ff){o.push_back((char)(0xc0|(cp>>6)));o.push_back((char)(0x80|(cp&63)));}else if(cp<=0xffff){o.push_back((char)(0xe0|(cp>>12)));o.push_back((char)(0x80|((cp>>6)&63)));o.push_back((char)(0x80|(cp&63)));}else{o.push_back((char)(0xf0|(cp>>18)));o.push_back((char)(0x80|((cp>>12)&63)));o.push_back((char)(0x80|((cp>>6)&63)));o.push_back((char)(0x80|(cp&63)));}}
class Parser {
public:
    Parser(const char *s,size_t n):start_(s),p_(s),end_(s+n){if((size_t)(end_-p_)>=3&&(unsigned char)p_[0]==0xef&&(unsigned char)p_[1]==0xbb&&(unsigned char)p_[2]==0xbf)p_+=3;}
    cJSON *run(bool require_null,const char **endptr){skip();cJSON *v=parse_value(0);if(!v){if(endptr)*endptr=(p_>=end_&&end_>start_)?end_-1:p_;return nullptr;}const char *parsed_end=p_;if(require_null){skip();if(p_>=end_||*p_!='\0'){delete_one(v);fail();if(endptr)*endptr=(p_>=end_&&end_>start_)?end_-1:p_;return nullptr;}if(endptr)*endptr=p_;}else if(endptr)*endptr=parsed_end;return v;}
    const char *pos()const{return p_;}
private:
    const char *start_,*p_,*end_;
    void skip(){while(p_<end_&&(unsigned char)*p_<=32){++p_;if(p_==end_&&end_>start_){--p_;return;}}}
    void fail(){set_error((p_>=end_&&end_>start_)?end_-1:p_);}
    bool hex4(uint32_t &v){if(end_-p_<4)return false;v=0;for(int i=0;i<4;i++){unsigned char c=(unsigned char)*p_++;v<<=4;if(c>='0'&&c<='9')v|=c-'0';else if(c>='a'&&c<='f')v|=c-'a'+10;else if(c>='A'&&c<='F')v|=c-'A'+10;else return false;}return true;}
    bool parse_string(std::string &out){
        const char *input_pointer=p_+1;
        const char *input_end=p_+1;
        if(p_>=end_||*p_!='"'){p_=input_pointer;return false;}
        size_t skipped_bytes=0;
        while(input_end<end_&&*input_end!='"'){
            if(*input_end=='\\'){
                if(input_end+1>=end_){p_=input_pointer;return false;}
                ++skipped_bytes;
                ++input_end;
            }
            ++input_end;
        }
        if(input_end>=end_||*input_end!='"'){p_=input_pointer;return false;}
        (void)skipped_bytes;
        while(input_pointer<input_end){
            if(*input_pointer!='\\'){out.push_back(*input_pointer++);continue;}
            const char *escape=input_pointer;
            if(input_end-input_pointer<2){p_=escape;return false;}
            const char e=input_pointer[1];
            switch(e){
                case '"':out.push_back('"');input_pointer+=2;break;
                case '\\':out.push_back('\\');input_pointer+=2;break;
                case '/':out.push_back('/');input_pointer+=2;break;
                case 'b':out.push_back('\b');input_pointer+=2;break;
                case 'f':out.push_back('\f');input_pointer+=2;break;
                case 'n':out.push_back('\n');input_pointer+=2;break;
                case 'r':out.push_back('\r');input_pointer+=2;break;
                case 't':out.push_back('\t');input_pointer+=2;break;
                case 'u':{
                    p_=input_pointer+2;
                    uint32_t cp=0;
                    if(!hex4(cp)){p_=escape;return false;}
                    if(cp>=0xd800&&cp<=0xdbff){
                        if(input_end-p_<6||p_[0]!='\\'||p_[1]!='u'){p_=escape;return false;}
                        p_+=2;
                        uint32_t lo=0;
                        if(!hex4(lo)||lo<0xdc00||lo>0xdfff){p_=escape;return false;}
                        cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
                    }
                    utf8_append(out,cp);
                    input_pointer=p_;
                    break;
                }
                default:p_=escape;return false;
            }
        }
        p_=input_end+1;
        return true;
    }
    cJSON *parse_number(){
        const char *begin=p_;size_t length=0;
        while(p_+length<end_){const char c=p_[length];if((c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'||c=='e'||c=='E')++length;else break;}
        if(!length)return nullptr;
        std::string token(begin,length);char *after=nullptr;const double d=std::strtod(token.c_str(),&after);
        if(!after||after==token.c_str())return nullptr;
        const size_t consumed=(size_t)(after-token.c_str());p_+=consumed;
        cJSON *n=new_node(cJSON_Number);if(!n)return nullptr;n->valuedouble=d;
        if(d>=INT_MAX)n->valueint=INT_MAX;else if(d<=(double)INT_MIN)n->valueint=INT_MIN;else if(std::isnan(d))n->valueint=0;else n->valueint=(int)d;
        return n;
    }

    cJSON *parse_value(unsigned depth){if(depth>CJSON_NESTING_LIMIT){fail();return nullptr;}skip();if(p_>=end_){fail();return nullptr;}if(*p_=='"'){std::string s;if(!parse_string(s)){fail();return nullptr;}cJSON *n=new_node(cJSON_String);if(!n)return nullptr;n->valuestring=dup_s(s.c_str());if(!n->valuestring){free_raw(n);return nullptr;}return n;}if(*p_=='{'||*p_=='['){char open=*p_++,close=open=='{'?'}':']';cJSON *n=new_node(open=='{'?cJSON_Object:cJSON_Array);if(!n)return nullptr;skip();if(p_<end_&&*p_==close){++p_;return n;}for(;;){std::string key;if(open=='{'){skip();if(!parse_string(key)){delete_one(n);fail();return nullptr;}skip();if(p_>=end_||*p_!=':'){delete_one(n);fail();return nullptr;}++p_;}cJSON *ch=parse_value(depth+1);if(!ch){delete_one(n);return nullptr;}if(open=='{'){ch->string=dup_s(key.c_str());if(!ch->string){delete_one(ch);delete_one(n);return nullptr;}}append_child(n,ch);skip();if(p_>=end_){delete_one(n);fail();return nullptr;}if(*p_==close){++p_;return n;}if(*p_!=','){delete_one(n);fail();return nullptr;}++p_;}}
        if(end_-p_>=4&&std::memcmp(p_,"null",4)==0){p_+=4;return new_node(cJSON_NULL);}if(end_-p_>=4&&std::memcmp(p_,"true",4)==0){p_+=4;cJSON *n=new_node(cJSON_True);if(n)n->valueint=1;return n;}if(end_-p_>=5&&std::memcmp(p_,"false",5)==0){p_+=5;return new_node(cJSON_False);}if(*p_=='-'||(*p_>='0'&&*p_<='9')){cJSON *n=parse_number();if(!n)fail();return n;}fail();return nullptr;}
};
void print_escaped(std::string &o,const char *s){o.push_back('"');if(s)for(const unsigned char *p=(const unsigned char*)s;*p;++p){switch(*p){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\b':o+="\\b";break;case '\f':o+="\\f";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:if(*p<0x20){char b[7];std::snprintf(b,sizeof(b),"\\u%04x",*p);o+=b;}else o.push_back((char)*p);}}o.push_back('"');}
bool valid_print_tree(const cJSON *n,int depth){
    if(!n||depth>CJSON_NESTING_LIMIT)return false;
    const int t=n->type&cJSON_TypeMask;
    if(t==cJSON_Invalid)return false;
    if(t==cJSON_Raw&&!n->valuestring)return false;
    if(t!=cJSON_NULL&&t!=cJSON_False&&t!=cJSON_True&&t!=cJSON_Number&&t!=cJSON_String&&t!=cJSON_Raw&&t!=cJSON_Array&&t!=cJSON_Object)return false;
    for(const cJSON *ch=n->child;ch;ch=ch->next)if(!valid_print_tree(ch,depth+1))return false;
    return true;
}
void print_value(std::string &o,const cJSON *n,bool pretty,int depth){
    if(!n||depth>CJSON_NESTING_LIMIT)return;
    const int t=n->type&cJSON_TypeMask;char buf[64];
    switch(t){
        case cJSON_NULL:o+="null";return;
        case cJSON_False:o+="false";return;
        case cJSON_True:o+="true";return;
        case cJSON_Number:{
            const double d=n->valuedouble;
            if(!std::isfinite(d)){o+="null";return;}
            int length=0;
            if(d==(double)n->valueint)length=std::snprintf(buf,sizeof(buf),"%d",n->valueint);
            else{
                length=std::snprintf(buf,sizeof(buf),"%.15g",d);
                char *end=nullptr;const double roundtrip=std::strtod(buf,&end);
                const double max_value=std::max(std::fabs(roundtrip),std::fabs(d));
                if(!end||*end||std::fabs(roundtrip-d)>max_value*DBL_EPSILON)length=std::snprintf(buf,sizeof(buf),"%.17g",d);
            }
            if(length>0)o.append(buf,(size_t)length);else o+="null";
            return;
        }
        case cJSON_String:print_escaped(o,n->valuestring?n->valuestring:"");return;
        case cJSON_Raw:o+=(n->valuestring?n->valuestring:"null");return;
        case cJSON_Array:case cJSON_Object:break;
        default:o+="null";return;
    }
    const bool obj=t==cJSON_Object;o.push_back(obj?'{':'[');const cJSON *ch=n->child;bool first=true;if(obj&&pretty)o.push_back('\n');
    while(ch){
        if(!first){if(obj)o+=pretty?",\n":",";else o+=pretty?", ":",";}
        if(obj&&pretty)o.append((size_t)(depth+1),'\t');
        if(obj){print_escaped(o,ch->string?ch->string:"");o+=pretty?":\t":":";}
        print_value(o,ch,pretty,depth+1);first=false;ch=ch->next;
    }
    if(obj&&pretty&&!first){o.push_back('\n');o.append((size_t)depth,'\t');}
    o.push_back(obj?'}':']');
}
void append_num(cJSON *n,double d){if(!n)return;n->valuedouble=d;if(d>=INT_MAX)n->valueint=INT_MAX;else if(d<=(double)INT_MIN)n->valueint=INT_MIN;else if(std::isnan(d))n->valueint=0;else n->valueint=(int)d;}
struct PrintBuffer {char *buffer;size_t length;size_t offset;size_t depth;bool format;bool noalloc;};
bool buffer_ensure(PrintBuffer &p,size_t needed){if(!p.buffer)return false;if(p.length>0&&p.offset>=p.length)return false;if(needed>(size_t)INT_MAX||needed>SIZE_MAX-p.offset-1)return false;needed+=p.offset+1;if(needed<=p.length)return true;if(p.noalloc)return false;const size_t newsize=needed>(size_t)INT_MAX/2?(size_t)INT_MAX:needed*2;void *(*realloc_fn)(void *,size_t)=nullptr;{std::lock_guard<std::mutex> l(g_hooks_mutex);realloc_fn=g_realloc_fn;}char *newbuffer=nullptr;if(realloc_fn){newbuffer=(char*)realloc_fn(p.buffer,newsize);if(!newbuffer){free_raw(p.buffer);p.buffer=nullptr;p.length=0;return false;}}else{newbuffer=(char*)alloc_raw(newsize);if(!newbuffer){free_raw(p.buffer);p.buffer=nullptr;p.length=0;return false;}if(p.offset+1<=p.length)std::memcpy(newbuffer,p.buffer,p.offset+1);else newbuffer[0]='\0';free_raw(p.buffer);}p.buffer=newbuffer;p.length=newsize;return true;}
void buffer_update_offset(PrintBuffer &p){if(p.buffer&&p.offset<p.length)p.offset+=std::strlen(p.buffer+p.offset);}
bool buffer_print_string(PrintBuffer &p,const char *value){std::string encoded;print_escaped(encoded,value?value:"");if(!buffer_ensure(p,encoded.size()+1))return false;std::memcpy(p.buffer+p.offset,encoded.c_str(),encoded.size()+1);return true;}
std::string number_text(const cJSON *n){char buf[64];const double d=n->valuedouble;if(!std::isfinite(d))return "null";int length=0;if(d==(double)n->valueint)length=std::snprintf(buf,sizeof(buf),"%d",n->valueint);else{length=std::snprintf(buf,sizeof(buf),"%.15g",d);char *end=nullptr;const double roundtrip=std::strtod(buf,&end);const double max_value=std::max(std::fabs(roundtrip),std::fabs(d));if(!end||*end||std::fabs(roundtrip-d)>max_value*DBL_EPSILON)length=std::snprintf(buf,sizeof(buf),"%.17g",d);}return length>0?std::string(buf,(size_t)length):std::string("null");}
bool buffer_print_value(PrintBuffer &p,const cJSON *n){if(!n||p.depth>CJSON_NESTING_LIMIT)return false;const int t=n->type&cJSON_TypeMask;char *out=nullptr;switch(t){case cJSON_NULL:if(!buffer_ensure(p,5))return false;std::memcpy(p.buffer+p.offset,"null",5);return true;case cJSON_False:if(!buffer_ensure(p,6))return false;std::memcpy(p.buffer+p.offset,"false",6);return true;case cJSON_True:if(!buffer_ensure(p,5))return false;std::memcpy(p.buffer+p.offset,"true",5);return true;case cJSON_Number:{const std::string value=number_text(n);if(!buffer_ensure(p,value.size()+1))return false;std::memcpy(p.buffer+p.offset,value.c_str(),value.size()+1);return true;}case cJSON_String:return buffer_print_string(p,n->valuestring);case cJSON_Raw:{if(!n->valuestring)return false;const size_t len=std::strlen(n->valuestring)+1;if(!buffer_ensure(p,len))return false;std::memcpy(p.buffer+p.offset,n->valuestring,len);return true;}case cJSON_Array:{if(!buffer_ensure(p,1))return false;p.buffer[p.offset]='[';++p.offset;++p.depth;for(const cJSON *ch=n->child;ch;ch=ch->next){if(!buffer_print_value(p,ch))return false;buffer_update_offset(p);if(ch->next){const size_t length=p.format?2:1;if(!buffer_ensure(p,length+1))return false;out=p.buffer+p.offset;*out++=',';if(p.format)*out++=' ';*out='\0';p.offset+=length;}}if(!buffer_ensure(p,2))return false;out=p.buffer+p.offset;*out++=']';*out='\0';--p.depth;return true;}case cJSON_Object:{const size_t opening=p.format?2:1;if(!buffer_ensure(p,opening+1))return false;out=p.buffer+p.offset;*out++='{';++p.depth;if(p.format)*out++='\n';p.offset+=opening;for(const cJSON *ch=n->child;ch;ch=ch->next){if(p.format){if(!buffer_ensure(p,p.depth))return false;out=p.buffer+p.offset;for(size_t i=0;i<p.depth;i++)*out++='\t';p.offset+=p.depth;}if(!buffer_print_string(p,ch->string))return false;buffer_update_offset(p);const size_t colon=p.format?2:1;if(!buffer_ensure(p,colon))return false;out=p.buffer+p.offset;*out++=':';if(p.format)*out++='\t';p.offset+=colon;if(!buffer_print_value(p,ch))return false;buffer_update_offset(p);const size_t separator=(p.format?1:0)+(ch->next?1:0);if(!buffer_ensure(p,separator+1))return false;out=p.buffer+p.offset;if(ch->next)*out++=',';if(p.format)*out++='\n';*out='\0';p.offset+=separator;}const size_t closing=p.format?(p.depth+1):2;if(!buffer_ensure(p,closing))return false;out=p.buffer+p.offset;if(p.format)for(size_t i=0;i<p.depth-1;i++)*out++='\t';*out++='}';*out='\0';--p.depth;return true;}default:return false;}}
char *print_allocated(const cJSON *item,bool fmt,int prebuffer,bool shrink){if(prebuffer<0)return nullptr;char *initial=(char*)alloc_raw((size_t)prebuffer);if(!initial)return nullptr;PrintBuffer p{initial,(size_t)prebuffer,0,0,fmt,false};try{if(!buffer_print_value(p,item)){free_raw(p.buffer);return nullptr;}buffer_update_offset(p);if(!shrink)return p.buffer;void *(*realloc_fn)(void *,size_t)=nullptr;{std::lock_guard<std::mutex> l(g_hooks_mutex);realloc_fn=g_realloc_fn;}if(realloc_fn){char *result=(char*)realloc_fn(p.buffer,p.offset+1);if(!result){free_raw(p.buffer);return nullptr;}return result;}char *result=(char*)alloc_raw(p.offset+1);if(!result){free_raw(p.buffer);return nullptr;}std::memcpy(result,p.buffer,std::min(p.length,p.offset+1));result[p.offset]='\0';free_raw(p.buffer);return result;}catch(...){free_raw(p.buffer);return nullptr;}}
cJSON *duplicate_impl(const cJSON *src,bool recurse,size_t depth){
    if(!src||depth>CJSON_CIRCULAR_LIMIT)return nullptr;
    cJSON *n=new_node(src->type&~(cJSON_IsReference|cJSON_StringIsConst));if(!n)return nullptr;
    n->valueint=src->valueint;n->valuedouble=src->valuedouble;
    if(src->valuestring){n->valuestring=dup_s(src->valuestring);if(!n->valuestring){delete_one(n);return nullptr;}}
    if(src->string){if(src->type&cJSON_StringIsConst){n->string=src->string;n->type|=cJSON_StringIsConst;}else{n->string=dup_s(src->string);if(!n->string){delete_one(n);return nullptr;}}}
    if(src->child&&recurse){for(const cJSON *ch=src->child;ch;ch=ch->next){cJSON *d=duplicate_impl(ch,true,depth+1);if(!d){delete_one(n);return nullptr;}append_child(n,d);}}
    return n;
}
bool compare_nodes(const cJSON *a,const cJSON *b,bool cs,int depth){
    if(!a||!b||depth>CJSON_CIRCULAR_LIMIT)return false;
    const int ta=a->type&cJSON_TypeMask,tb=b->type&cJSON_TypeMask;if(ta!=tb)return false;
    switch(ta){
        case cJSON_NULL:return true;
        case cJSON_False:case cJSON_True:return true;
        case cJSON_Number:{const double max_value=std::max(std::fabs(a->valuedouble),std::fabs(b->valuedouble));return std::fabs(a->valuedouble-b->valuedouble)<=max_value*DBL_EPSILON;}
        case cJSON_String:case cJSON_Raw:if(!a->valuestring||!b->valuestring)return false;return std::strcmp(a->valuestring,b->valuestring)==0;
        case cJSON_Array:{const cJSON *pa=a->child,*pb=b->child;for(;pa&&pb;pa=pa->next,pb=pb->next)if(!compare_nodes(pa,pb,cs,depth+1))return false;return !pa&&!pb;}
        case cJSON_Object:{
            int count_a=0,count_b=0;for(const cJSON *p=a->child;p;p=p->next)++count_a;for(const cJSON *p=b->child;p;p=p->next)++count_b;if(count_a!=count_b)return false;
            for(const cJSON *pa=a->child;pa;pa=pa->next){const cJSON *pb=get_object(b,pa->string,cs);if(!pb||!compare_nodes(pa,pb,cs,depth+1))return false;}return true;
        }
        default:return false;
    }
}
cJSON *add_to_obj(cJSON *o,const char *name,cJSON *item,bool constant){
    if(!o||!name||!item||item==o)return nullptr;
    char *new_name=constant?(char*)name:dup_s(name);if(!constant&&!new_name)return nullptr;
    if(!(item->type&cJSON_StringIsConst))free_raw(item->string);
    item->string=new_name;if(constant)item->type|=cJSON_StringIsConst;else item->type&=~cJSON_StringIsConst;
    append_child(o,item);return item;
}
cJSON *add_new(cJSON *o,const char *name,int type){cJSON *n=new_node(type);if(!n)return nullptr;if(!add_to_obj(o,name,n,false)){delete_one(n);return nullptr;}return n;}
cJSON *reference_node(const cJSON *src){if(!src)return nullptr;cJSON *n=new_node(src->type|cJSON_IsReference);if(!n)return nullptr;n->valuestring=src->valuestring;n->child=src->child;n->valueint=src->valueint;n->valuedouble=src->valuedouble;return n;}
}
extern "C" {
const char *cJSON_Version(void){return "1.7.19";}
const char *cJSON_GetErrorPtr(void){return get_error();}
char *cJSON_GetStringValue(const cJSON *item){return item&&(item->type&cJSON_TypeMask)==cJSON_String?item->valuestring:nullptr;}
double cJSON_GetNumberValue(const cJSON *item){return item&&(item->type&cJSON_TypeMask)==cJSON_Number?item->valuedouble:NAN;}
void cJSON_InitHooks(cJSON_Hooks *hooks){std::lock_guard<std::mutex> l(g_hooks_mutex);if(!hooks){g_hooks={std::malloc,std::free};g_realloc_fn=std::realloc;return;}g_hooks.malloc_fn=hooks->malloc_fn?hooks->malloc_fn:std::malloc;g_hooks.free_fn=hooks->free_fn?hooks->free_fn:std::free;g_realloc_fn=(g_hooks.malloc_fn==std::malloc&&g_hooks.free_fn==std::free)?std::realloc:nullptr;}
void cJSON_Delete(cJSON *item){while(item){cJSON *next=item->next;item->next=item->prev=nullptr;delete_one(item);item=next;}}
double cJSON_SetNumberHelper(cJSON *object,double number){if(!object)return number;append_num(object,number);return number;}
char *cJSON_SetValuestring(cJSON *object,const char *valuestring){
    if(!object||(object->type&cJSON_String)==0||(object->type&cJSON_IsReference)||!object->valuestring||!valuestring)return nullptr;
    const size_t new_len=std::strlen(valuestring),old_len=std::strlen(object->valuestring);
    if(new_len<=old_len){
        const uintptr_t src=(uintptr_t)valuestring,dst=(uintptr_t)object->valuestring;
        if(!(src+new_len<dst||dst+old_len<src))return nullptr;
        std::memcpy(object->valuestring,valuestring,new_len+1);return object->valuestring;
    }
    char *replacement=dup_s(valuestring);if(!replacement)return nullptr;free_raw(object->valuestring);object->valuestring=replacement;return replacement;
}

cJSON *cJSON_ParseWithLengthOpts(const char *value,size_t buffer_length,const char **return_parse_end,cJSON_bool require_null_terminated){clear_error();if(return_parse_end)*return_parse_end=value;if(!value||!buffer_length)return nullptr;Parser p(value,buffer_length);cJSON *n=p.run(require_null_terminated!=0,return_parse_end);return n;}
cJSON *cJSON_ParseWithOpts(const char *value,const char **return_parse_end,cJSON_bool require_null_terminated){if(!value)return nullptr;return cJSON_ParseWithLengthOpts(value,std::strlen(value)+1,return_parse_end,require_null_terminated);}
cJSON *cJSON_Parse(const char *value){return cJSON_ParseWithOpts(value,nullptr,0);}
cJSON *cJSON_ParseWithLength(const char *value,size_t buffer_length){return cJSON_ParseWithLengthOpts(value,buffer_length,nullptr,0);}
char *cJSON_PrintBuffered(const cJSON *item,int prebuffer,cJSON_bool fmt){return print_allocated(item,fmt!=0,prebuffer,false);}
char *cJSON_Print(const cJSON *item){return print_allocated(item,true,256,true);}
char *cJSON_PrintUnformatted(const cJSON *item){return print_allocated(item,false,256,true);}
cJSON_bool cJSON_PrintPreallocated(cJSON *item,char *buffer,const int length,const cJSON_bool format){if(length<0||!buffer)return 0;try{PrintBuffer p{buffer,(size_t)length,0,0,format!=0,true};return buffer_print_value(p,item)?1:0;}catch(...){return 0;}}
int cJSON_GetArraySize(const cJSON *array){if(!array)return 0;size_t n=0;for(const cJSON *p=array->child;p;p=p->next)++n;return (int)n;}
cJSON *cJSON_GetArrayItem(const cJSON *array,int index){if(!array||index<0)return nullptr;cJSON *p=array->child;while(p&&index--)p=p->next;return p;}
cJSON *cJSON_GetObjectItem(const cJSON *object,const char *string){return get_object(object,string,false);}
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object,const char *string){return get_object(object,string,true);}
cJSON_bool cJSON_HasObjectItem(const cJSON *object,const char *string){return get_object(object,string,false)!=nullptr;}
cJSON_bool cJSON_AddItemToArray(cJSON *array,cJSON *item){if(!array||!item||array==item||item->next||item->prev)return 0;append_child(array,item);return 1;}
cJSON_bool cJSON_AddItemToObject(cJSON *object,const char *string,cJSON *item){return add_to_obj(object,string,item,false)!=nullptr;}
cJSON_bool cJSON_AddItemToObjectCS(cJSON *object,const char *string,cJSON *item){return add_to_obj(object,string,item,true)!=nullptr;}
cJSON_bool cJSON_AddItemReferenceToArray(cJSON *array,cJSON *item){if(!array||!item)return 0;cJSON *r=reference_node(item);if(!r)return 0;if(!cJSON_AddItemToArray(array,r)){delete_one(r);return 0;}return 1;}
cJSON_bool cJSON_AddItemReferenceToObject(cJSON *object,const char *string,cJSON *item){if(!object||!string||!item)return 0;cJSON *r=reference_node(item);if(!r)return 0;if(!add_to_obj(object,string,r,false)){delete_one(r);return 0;}return 1;}
cJSON *cJSON_AddNullToObject(cJSON *o,const char *n){return add_new(o,n,cJSON_NULL);}
cJSON *cJSON_AddTrueToObject(cJSON *o,const char *n){return add_new(o,n,cJSON_True);}
cJSON *cJSON_AddFalseToObject(cJSON *o,const char *n){return add_new(o,n,cJSON_False);}
cJSON *cJSON_AddBoolToObject(cJSON *o,const char *n,const cJSON_bool b){return add_new(o,n,b?cJSON_True:cJSON_False);}
cJSON *cJSON_AddNumberToObject(cJSON *o,const char *n,const double d){cJSON *v=add_new(o,n,cJSON_Number);if(v)append_num(v,d);return v;}
cJSON *cJSON_AddStringToObject(cJSON *o,const char *n,const char *s){if(!o||!n||!s)return nullptr;cJSON *v=cJSON_CreateString(s);if(!v)return nullptr;if(!cJSON_AddItemToObject(o,n,v)){delete_one(v);return nullptr;}return v;}
cJSON *cJSON_AddRawToObject(cJSON *o,const char *n,const char *s){if(!o||!n||!s)return nullptr;cJSON *v=cJSON_CreateRaw(s);if(!v)return nullptr;if(!cJSON_AddItemToObject(o,n,v)){delete_one(v);return nullptr;}return v;}
cJSON *cJSON_AddObjectToObject(cJSON *o,const char *n){return add_new(o,n,cJSON_Object);}
cJSON *cJSON_AddArrayToObject(cJSON *o,const char *n){return add_new(o,n,cJSON_Array);}
cJSON *cJSON_CreateNull(void){return new_node(cJSON_NULL);}
cJSON *cJSON_CreateTrue(void){return new_node(cJSON_True);}
cJSON *cJSON_CreateFalse(void){return new_node(cJSON_False);}
cJSON *cJSON_CreateBool(cJSON_bool b){return new_node(b?cJSON_True:cJSON_False);}
cJSON *cJSON_CreateNumber(double d){cJSON *n=new_node(cJSON_Number);append_num(n,d);return n;}
cJSON *cJSON_CreateString(const char *s){if(!s)return nullptr;cJSON *n=new_node(cJSON_String);if(!n)return nullptr;n->valuestring=dup_s(s);if(!n->valuestring){free_raw(n);return nullptr;}return n;}
cJSON *cJSON_CreateRaw(const char *s){if(!s)return nullptr;cJSON *n=new_node(cJSON_Raw);if(!n)return nullptr;n->valuestring=dup_s(s);if(!n->valuestring){free_raw(n);return nullptr;}return n;}
cJSON *cJSON_CreateObject(void){return new_node(cJSON_Object);}
cJSON *cJSON_CreateArray(void){return new_node(cJSON_Array);}
cJSON *cJSON_DetachItemViaPointer(cJSON *parent,cJSON * const item){
    if(!parent||!item)return nullptr;
    cJSON *head=parent->child,*p=head;while(p&&p!=item)p=p->next;if(!p)return nullptr;
    if(p==head){cJSON *tail=head->prev;if(p->next){parent->child=p->next;parent->child->prev=(tail==p?parent->child:tail);}else parent->child=nullptr;}
    else{cJSON *prev=p->prev,*next=p->next;prev->next=next;if(next)next->prev=prev;else head->prev=prev;}
    p->next=p->prev=nullptr;return p;
}
cJSON *cJSON_DetachItemFromArray(cJSON *array,int which){return cJSON_DetachItemViaPointer(array,cJSON_GetArrayItem(array,which));}
void cJSON_DeleteItemFromArray(cJSON *array,int which){cJSON_Delete(cJSON_DetachItemFromArray(array,which));}
cJSON *cJSON_DetachItemFromObject(cJSON *object,const char *string){return cJSON_DetachItemViaPointer(object,get_object(object,string,false));}
cJSON *cJSON_DetachItemFromObjectCaseSensitive(cJSON *object,const char *string){return cJSON_DetachItemViaPointer(object,get_object(object,string,true));}
void cJSON_DeleteItemFromObject(cJSON *object,const char *string){cJSON_Delete(cJSON_DetachItemFromObject(object,string));}
void cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object,const char *string){cJSON_Delete(cJSON_DetachItemFromObjectCaseSensitive(object,string));}
cJSON_bool cJSON_InsertItemInArray(cJSON *array,int which,cJSON *newitem){
    if(!array||!newitem||array==newitem||which<0||newitem->next||newitem->prev)return 0;
    cJSON *head=array->child;if(!head){append_child(array,newitem);return 1;}
    if(which==0){cJSON *tail=head->prev;newitem->next=head;newitem->prev=tail;head->prev=newitem;array->child=newitem;return 1;}
    cJSON *cur=head;int index=0;while(cur&&index<which){cur=cur->next;++index;}
    if(!cur){append_child(array,newitem);return 1;}
    cJSON *prev=cur->prev;prev->next=newitem;newitem->prev=prev;newitem->next=cur;cur->prev=newitem;return 1;
}
cJSON_bool cJSON_ReplaceItemViaPointer(cJSON * const parent,cJSON * const item,cJSON *replacement){
    if(!parent||!item||!replacement)return 0;if(item==replacement)return 1;if(replacement==parent||replacement->next||replacement->prev)return 0;
    cJSON *head=parent->child,*p=head;while(p&&p!=item)p=p->next;if(!p)return 0;
    replacement->next=p->next;replacement->prev=p->prev;
    if(p==head){if(replacement->next)replacement->next->prev=replacement;else replacement->prev=replacement;parent->child=replacement;}
    else{p->prev->next=replacement;if(replacement->next)replacement->next->prev=replacement;else head->prev=replacement;}
    p->next=p->prev=nullptr;delete_one(p);return 1;
}
cJSON_bool cJSON_ReplaceItemInArray(cJSON *array,int which,cJSON *newitem){return cJSON_ReplaceItemViaPointer(array,cJSON_GetArrayItem(array,which),newitem);}
static cJSON_bool replace_item_object(cJSON *object,const char *string,cJSON *newitem,bool case_sensitive){
    if(!string||!newitem)return 0;
    if(!(newitem->type&cJSON_StringIsConst)&&newitem->string)free_raw(newitem->string);
    newitem->string=dup_s(string);if(!newitem->string)return 0;newitem->type&=~cJSON_StringIsConst;
    return cJSON_ReplaceItemViaPointer(object,get_object(object,string,case_sensitive),newitem);
}
cJSON_bool cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem){return replace_item_object(object,string,newitem,false);}
cJSON_bool cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem){return replace_item_object(object,string,newitem,true);}
cJSON *cJSON_CreateStringReference(const char *s){cJSON *n=new_node(cJSON_String|cJSON_IsReference);if(n)n->valuestring=(char*)s;return n;}
cJSON *cJSON_CreateObjectReference(const cJSON *child){cJSON *n=new_node(cJSON_Object|cJSON_IsReference);if(n)n->child=(cJSON*)child;return n;}
cJSON *cJSON_CreateArrayReference(const cJSON *child){cJSON *n=new_node(cJSON_Array|cJSON_IsReference);if(n)n->child=(cJSON*)child;return n;}
cJSON *cJSON_CreateIntArray(const int *numbers,int count){if(count<0||!numbers)return nullptr;cJSON *a=cJSON_CreateArray();if(!a)return nullptr;for(int i=0;i<count;i++){cJSON *n=cJSON_CreateNumber(numbers[i]);if(!n||!cJSON_AddItemToArray(a,n)){cJSON_Delete(n);cJSON_Delete(a);return nullptr;}}return a;}
cJSON *cJSON_CreateFloatArray(const float *numbers,int count){if(count<0||!numbers)return nullptr;cJSON *a=cJSON_CreateArray();if(!a)return nullptr;for(int i=0;i<count;i++){cJSON *n=cJSON_CreateNumber(numbers[i]);if(!n||!cJSON_AddItemToArray(a,n)){cJSON_Delete(n);cJSON_Delete(a);return nullptr;}}return a;}
cJSON *cJSON_CreateDoubleArray(const double *numbers,int count){if(count<0||!numbers)return nullptr;cJSON *a=cJSON_CreateArray();if(!a)return nullptr;for(int i=0;i<count;i++){cJSON *n=cJSON_CreateNumber(numbers[i]);if(!n||!cJSON_AddItemToArray(a,n)){cJSON_Delete(n);cJSON_Delete(a);return nullptr;}}return a;}
cJSON *cJSON_CreateStringArray(const char * const *strings,int count){if(count<0||!strings)return nullptr;cJSON *a=cJSON_CreateArray();if(!a)return nullptr;for(int i=0;i<count;i++){cJSON *n=cJSON_CreateString(strings[i]);if(!n||!cJSON_AddItemToArray(a,n)){cJSON_Delete(n);cJSON_Delete(a);return nullptr;}}return a;}
cJSON *cJSON_Duplicate_rec(const cJSON *item,size_t depth,cJSON_bool recurse){return duplicate_impl(item,recurse!=0,depth);}
cJSON *cJSON_Duplicate(const cJSON *item,cJSON_bool recurse){return duplicate_impl(item,recurse!=0,0);}
void cJSON_Minify(char *json){if(!json)return;char *into=json;while(*json){if(*json==' '||*json=='\t'||*json=='\r'||*json=='\n'){++json;continue;}if(*json=='/'&&json[1]=='/'){json+=2;while(*json&&*json!='\n'&&*json!='\r')++json;continue;}if(*json=='/'&&json[1]=='*'){json+=2;while(*json&&!(json[0]=='*'&&json[1]=='/'))++json;if(*json)json+=2;continue;}if(*json=='"'){*into++=*json++;while(*json){*into++=*json;if(*json=='\\'){++json;if(*json)*into++=*json;}else if(*json=='"'){++json;break;}++json;}continue;}*into++=*json++;}*into='\0';}
cJSON_bool cJSON_IsInvalid(const cJSON * const i){return !i||((i->type&cJSON_TypeMask)==cJSON_Invalid);}
cJSON_bool cJSON_IsFalse(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_False);}
cJSON_bool cJSON_IsTrue(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_True);}
cJSON_bool cJSON_IsBool(const cJSON * const i){return i&&(((i->type&cJSON_TypeMask)==cJSON_True)||((i->type&cJSON_TypeMask)==cJSON_False));}
cJSON_bool cJSON_IsNull(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_NULL);}
cJSON_bool cJSON_IsNumber(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_Number);}
cJSON_bool cJSON_IsString(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_String);}
cJSON_bool cJSON_IsArray(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_Array);}
cJSON_bool cJSON_IsObject(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_Object);}
cJSON_bool cJSON_IsRaw(const cJSON * const i){return i&&((i->type&cJSON_TypeMask)==cJSON_Raw);}
cJSON_bool cJSON_Compare(const cJSON * const a,const cJSON * const b,const cJSON_bool cs){return compare_nodes(a,b,cs!=0,0);}
void *cJSON_malloc(size_t size){return alloc_raw(size);}
void cJSON_free(void *object){free_raw(object);}
}
