#include "json.hpp"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace astrae::json {
bool Value::as_bool(bool d) const { if (auto p=std::get_if<bool>(&v_)) return *p; if (auto p=std::get_if<int64_t>(&v_)) return *p!=0; if (auto p=std::get_if<uint64_t>(&v_)) return *p!=0; return d; }
int64_t Value::as_i64(int64_t d) const { if (auto p=std::get_if<int64_t>(&v_)) return *p; if (auto p=std::get_if<uint64_t>(&v_)) return *p>uint64_t(INT64_MAX)?d:static_cast<int64_t>(*p); if (auto p=std::get_if<double>(&v_)) return static_cast<int64_t>(*p); if (auto p=std::get_if<std::string>(&v_)) { try { size_t n=0; auto x=std::stoll(*p,&n); return n==p->size()?x:d; } catch (...) {} } return d; }
uint64_t Value::as_u64(uint64_t d) const { if (auto p=std::get_if<uint64_t>(&v_)) return *p; if (auto p=std::get_if<int64_t>(&v_)) return *p<0?d:static_cast<uint64_t>(*p); if (auto p=std::get_if<double>(&v_)) return *p<0?d:static_cast<uint64_t>(*p); if (auto p=std::get_if<std::string>(&v_)) { try { size_t n=0; auto x=std::stoull(*p,&n); return n==p->size()?x:d; } catch (...) {} } return d; }
double Value::as_double(double d) const { if (auto p=std::get_if<double>(&v_)) return *p; if (auto p=std::get_if<int64_t>(&v_)) return static_cast<double>(*p); if (auto p=std::get_if<uint64_t>(&v_)) return static_cast<double>(*p); if (auto p=std::get_if<std::string>(&v_)) { try { size_t n=0; double x=std::stod(*p,&n); return n==p->size()?x:d; } catch (...) {} } return d; }
std::string Value::as_string(const std::string &d) const { if (auto p=std::get_if<std::string>(&v_)) return *p; if (is_number() || is_bool()) return scalar_string(); return d; }
std::string Value::scalar_string() const { if (auto p=std::get_if<std::string>(&v_)) return *p; if (auto p=std::get_if<int64_t>(&v_)) return std::to_string(*p); if (auto p=std::get_if<uint64_t>(&v_)) return std::to_string(*p); if (auto p=std::get_if<bool>(&v_)) return *p?"true":"false"; if (auto p=std::get_if<double>(&v_)) { std::ostringstream s; s.precision(17); s<<*p; return s.str(); } return {}; }
Value::Array &Value::as_array() { if (!is_array()) v_=Array{}; return std::get<Array>(v_); }
const Value::Array &Value::as_array() const { static const Array empty; return is_array()?std::get<Array>(v_):empty; }
Value::Object &Value::as_object() { if (!is_object()) v_=Object{}; return std::get<Object>(v_); }
const Value::Object &Value::as_object() const { static const Object empty; return is_object()?std::get<Object>(v_):empty; }
Value &Value::operator[](const std::string &key) { return as_object()[key]; }
const Value &Value::operator[](const std::string &key) const { static const Value nil; auto &o=as_object(); auto it=o.find(key); return it==o.end()?nil:it->second; }
Value &Value::operator[](size_t i) { auto &a=as_array(); if (i>=a.size()) a.resize(i+1); return a[i]; }
const Value &Value::operator[](size_t i) const { static const Value nil; auto &a=as_array(); return i<a.size()?a[i]:nil; }
bool Value::contains(const std::string &key) const { return is_object() && as_object().find(key)!=as_object().end(); }
size_t Value::size() const { if (is_array()) return as_array().size(); if (is_object()) return as_object().size(); if (is_string()) return as_string().size(); return 0; }
void Value::push_back(Value x) { as_array().push_back(std::move(x)); }
void Value::erase(const std::string &key) { if (is_object()) as_object().erase(key); }

class Parser {
public:
    explicit Parser(const std::string &s):s_(s){}
    Value run(){ ws(); Value v=value(0); ws(); if(p_!=s_.size()) fail("trailing characters"); return v; }
private:
    const std::string &s_; size_t p_=0;
    [[noreturn]] void fail(const char *m) const { throw std::runtime_error(std::string("JSON parse error at ")+std::to_string(p_)+": "+m); }
    void ws(){ while(p_<s_.size() && (s_[p_]==' '||s_[p_]=='\n'||s_[p_]=='\r'||s_[p_]=='\t')) ++p_; }
    bool take(char c){ if(p_<s_.size()&&s_[p_]==c){++p_;return true;} return false; }
    void expect(char c){ if(!take(c)) fail("unexpected token"); }
    static void utf8(std::string &o,uint32_t c){ if(c<=0x7f)o.push_back(char(c)); else if(c<=0x7ff){o.push_back(char(0xc0|(c>>6)));o.push_back(char(0x80|(c&63)));} else if(c<=0xffff){o.push_back(char(0xe0|(c>>12)));o.push_back(char(0x80|((c>>6)&63)));o.push_back(char(0x80|(c&63)));} else {o.push_back(char(0xf0|(c>>18)));o.push_back(char(0x80|((c>>12)&63)));o.push_back(char(0x80|((c>>6)&63)));o.push_back(char(0x80|(c&63)));} }
    uint32_t hex4(){ if(p_+4>s_.size())fail("short unicode escape"); uint32_t v=0; for(int i=0;i<4;i++){char c=s_[p_++];v<<=4;if(c>='0'&&c<='9')v|=c-'0';else if(c>='a'&&c<='f')v|=c-'a'+10;else if(c>='A'&&c<='F')v|=c-'A'+10;else fail("invalid unicode escape");} return v; }
    std::string string(){ expect('"'); std::string o; while(p_<s_.size()){ unsigned char c=s_[p_++]; if(c=='"')return o; if(c<0x20)fail("control in string"); if(c!='\\'){o.push_back(char(c));continue;} if(p_>=s_.size())fail("short escape"); char e=s_[p_++]; switch(e){case '"':o.push_back('"');break;case '\\':o.push_back('\\');break;case '/':o.push_back('/');break;case 'b':o.push_back('\b');break;case 'f':o.push_back('\f');break;case 'n':o.push_back('\n');break;case 'r':o.push_back('\r');break;case 't':o.push_back('\t');break;case 'u':{uint32_t cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(p_+2>s_.size()||s_[p_]!='\\'||s_[p_+1]!='u')fail("missing low surrogate");p_+=2;uint32_t lo=hex4();if(lo<0xdc00||lo>0xdfff)fail("invalid low surrogate");cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)fail("orphan low surrogate");utf8(o,cp);break;}default:fail("invalid escape");}} fail("unterminated string"); }
    Value number(){ size_t b=p_; bool neg=take('-'); if(p_>=s_.size())fail("bad number"); if(s_[p_]=='0')++p_; else {if(s_[p_]<'1'||s_[p_]>'9')fail("bad number");while(p_<s_.size()&&s_[p_]>='0'&&s_[p_]<='9')++p_;} bool real=false; if(take('.')){real=true;if(p_>=s_.size()||s_[p_]<'0'||s_[p_]>'9')fail("bad fraction");while(p_<s_.size()&&s_[p_]>='0'&&s_[p_]<='9')++p_;} if(p_<s_.size()&&(s_[p_]=='e'||s_[p_]=='E')){real=true;++p_;if(p_<s_.size()&&(s_[p_]=='+'||s_[p_]=='-'))++p_;if(p_>=s_.size()||s_[p_]<'0'||s_[p_]>'9')fail("bad exponent");while(p_<s_.size()&&s_[p_]>='0'&&s_[p_]<='9')++p_;} std::string t=s_.substr(b,p_-b); if(real){char *e=nullptr;double d=std::strtod(t.c_str(),&e);if(!e||*e||!std::isfinite(d))fail("invalid real");return d;} try{if(neg)return static_cast<int64_t>(std::stoll(t));return static_cast<uint64_t>(std::stoull(t));}catch(...){fail("integer out of range");} }
    Value value(int depth){ if(depth>128)fail("too deep");ws();if(p_>=s_.size())fail("unexpected end");char c=s_[p_];if(c=='"')return string();if(c=='{'){++p_;Value o=Value::object();ws();if(take('}'))return o;for(;;){ws();if(p_>=s_.size()||s_[p_]!='"')fail("object key expected");auto k=string();ws();expect(':');o[k]=value(depth+1);ws();if(take('}'))return o;expect(',');}}if(c=='['){++p_;Value a=Value::array();ws();if(take(']'))return a;for(;;){a.push_back(value(depth+1));ws();if(take(']'))return a;expect(',');}}if(s_.compare(p_,4,"true")==0){p_+=4;return true;}if(s_.compare(p_,5,"false")==0){p_+=5;return false;}if(s_.compare(p_,4,"null")==0){p_+=4;return nullptr;}if(c=='-'||(c>='0'&&c<='9'))return number();fail("unexpected token"); }
};
Value parse(const std::string &text){ return Parser(text).run(); }
static void esc(std::string &o,const std::string &s){ o.push_back('"');for(unsigned char c:s){switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\b':o+="\\b";break;case '\f':o+="\\f";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:if(c<0x20){char b[7];std::snprintf(b,sizeof(b),"\\u%04x",c);o+=b;}else o.push_back(char(c));}}o.push_back('"'); }
static void emit(std::string &o,const Value &v,bool pretty,int n,int step){auto pad=[&](int d){if(pretty)o.append(size_t(d), ' ');};if(v.is_null()){o+="null";return;}if(auto p=std::get_if<bool>(&v.data())){o+=*p?"true":"false";return;}if(auto p=std::get_if<int64_t>(&v.data())){o+=std::to_string(*p);return;}if(auto p=std::get_if<uint64_t>(&v.data())){o+=std::to_string(*p);return;}if(auto p=std::get_if<double>(&v.data())){char b[64];std::snprintf(b,sizeof(b),"%.17g",*p);o+=b;return;}if(auto p=std::get_if<std::string>(&v.data())){esc(o,*p);return;}if(v.is_array()){o.push_back('[');auto &a=v.as_array();for(size_t i=0;i<a.size();++i){if(i)o.push_back(',');if(pretty){o.push_back('\n');pad(n+step);}emit(o,a[i],pretty,n+step,step);}if(pretty&&!a.empty()){o.push_back('\n');pad(n);}o.push_back(']');return;}o.push_back('{');auto &m=v.as_object();size_t i=0;for(auto &kv:m){if(i++)o.push_back(',');if(pretty){o.push_back('\n');pad(n+step);}esc(o,kv.first);o+=pretty?": ":":";emit(o,kv.second,pretty,n+step,step);}if(pretty&&!m.empty()){o.push_back('\n');pad(n);}o.push_back('}');}
std::string dump(const Value &value,bool pretty,int indent){std::string out;emit(out,value,pretty,0,indent);return out;}
}
