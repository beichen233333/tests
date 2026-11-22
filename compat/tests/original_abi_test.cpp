#include "cJSON.h"
#include "original_store_abi.h"
#include <cassert>
#include <climits>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <string>
static int alloc_count=0,free_count=0;
static void *test_alloc(size_t n){++alloc_count;return std::malloc(n);}
static void test_free(void *p){if(p)++free_count;std::free(p);}
int main(){
    static_assert(sizeof(cJSON)==64,"cJSON size");
    static_assert(offsetof(cJSON,string)==56,"cJSON string offset");
    static_assert(sizeof(store_reward)==24&&sizeof(store_cost)==24,"store ABI sizes");
    assert(std::strcmp(cJSON_Version(),"1.7.19")==0);
    const char *source="{\"z\":1,\"Name\":\"Astrae\",\"flag\":true,\"arr\":[1,2,3],\"nested\":{\"ok\":null}}";
    cJSON *root=cJSON_Parse(source);assert(root&&cJSON_IsObject(root));
    assert(cJSON_GetArraySize(root)==5);
    assert(cJSON_GetObjectItem(root,"name")!=nullptr);
    assert(cJSON_GetObjectItemCaseSensitive(root,"name")==nullptr);
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(root,"arr");assert(cJSON_GetArraySize(arr)==3);
    assert(cJSON_GetNumberValue(cJSON_GetArrayItem(arr,2))==3.0);
    char *compact=cJSON_PrintUnformatted(root);assert(compact);
    assert(std::strstr(compact,"\"z\":1")!=nullptr);
    cJSON *copy=cJSON_Parse(compact);assert(copy&&cJSON_Compare(root,copy,1));
    cJSON *reordered=cJSON_Parse("{\"nested\":{\"ok\":null},\"arr\":[1,2,3],\"flag\":true,\"Name\":\"Astrae\",\"z\":1}");assert(reordered&&cJSON_Compare(root,reordered,1));
    char *pretty=cJSON_Print(root);assert(pretty&&std::strstr(pretty,"\n\t\"Name\":\t"));cJSON_free(pretty);
    cJSON *decimal=cJSON_CreateNumber(0.1);char *decimal_text=cJSON_PrintUnformatted(decimal);assert(decimal_text&&std::strcmp(decimal_text,"0.1")==0);cJSON_free(decimal_text);cJSON_Delete(decimal);
    cJSON_free(compact);cJSON_Delete(reordered);
    cJSON *extra=cJSON_CreateNumber(42);assert(extra&&cJSON_AddItemToObject(root,"answer",extra));
    assert(cJSON_GetNumberValue(cJSON_GetObjectItem(root,"answer"))==42.0);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(root,"answer",cJSON_CreateString("yes")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root,"answer")));
    cJSON *detached=cJSON_DetachItemFromObjectCaseSensitive(root,"answer");assert(detached);cJSON_Delete(detached);
    cJSON *dup=cJSON_Duplicate(root,1);assert(dup&&cJSON_Compare(root,dup,1));
    cJSON *shallow=cJSON_Duplicate(root,0);assert(shallow&&shallow->child==nullptr&&!(shallow->type&cJSON_IsReference));cJSON_Delete(shallow);cJSON_Delete(dup);
    assert(root->child&&root->child->prev);cJSON *tail=root->child->prev;assert(tail->next==nullptr);assert(cJSON_GetObjectItemCaseSensitive(root,"z")==root->child);
    cJSON *arr2=cJSON_CreateArray();assert(arr2);assert(cJSON_AddItemToArray(arr2,cJSON_CreateNumber(1)));assert(cJSON_AddItemToArray(arr2,cJSON_CreateNumber(3)));assert(arr2->child->prev==cJSON_GetArrayItem(arr2,1));assert(cJSON_InsertItemInArray(arr2,1,cJSON_CreateNumber(2)));assert(cJSON_GetNumberValue(cJSON_GetArrayItem(arr2,1))==2);assert(cJSON_InsertItemInArray(arr2,999,cJSON_CreateNumber(4)));assert(cJSON_GetArraySize(arr2)==4&&cJSON_GetNumberValue(cJSON_GetArrayItem(arr2,3))==4);assert(arr2->child->prev==cJSON_GetArrayItem(arr2,3));
    cJSON *last=cJSON_DetachItemFromArray(arr2,3);assert(last&&last->next==nullptr&&last->prev==nullptr);cJSON_Delete(last);assert(arr2->child->prev==cJSON_GetArrayItem(arr2,2));
    assert(cJSON_ReplaceItemInObject(root,"NAME",cJSON_CreateString("CaseInsensitive")));assert(cJSON_GetObjectItemCaseSensitive(root,"NAME")!=nullptr);assert(cJSON_GetObjectItemCaseSensitive(root,"Name")==nullptr);assert(cJSON_GetObjectItem(root,"name")&&std::strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(root,"name")),"CaseInsensitive")==0);cJSON_Delete(arr2);
    cJSON *shortstr=cJSON_CreateString("longer");assert(shortstr);char *old_string=shortstr->valuestring;assert(cJSON_SetValuestring(shortstr,"x")==old_string&&std::strcmp(shortstr->valuestring,"x")==0);assert(cJSON_SetValuestring(shortstr,"a much longer string")!=old_string);assert(std::strcmp(shortstr->valuestring,"a much longer string")==0);assert(cJSON_SetValuestring(shortstr,shortstr->valuestring)==nullptr);cJSON_Delete(shortstr);
    cJSON *near_a=cJSON_CreateNumber(1.0),*near_b=cJSON_CreateNumber(1.0+DBL_EPSILON*0.5),*far_b=cJSON_CreateNumber(1.0+DBL_EPSILON*4);assert(cJSON_Compare(near_a,near_b,1));assert(!cJSON_Compare(near_a,far_b,1));assert(!cJSON_Compare(nullptr,nullptr,1));cJSON *invalid_a=(cJSON*)std::calloc(1,sizeof(cJSON)),*invalid_b=(cJSON*)std::calloc(1,sizeof(cJSON));assert(invalid_a&&invalid_b&&!cJSON_Compare(invalid_a,invalid_b,1));std::free(invalid_a);std::free(invalid_b);cJSON_Delete(near_a);cJSON_Delete(near_b);cJSON_Delete(far_b);
    assert(cJSON_Print(nullptr)==nullptr);assert(cJSON_PrintBuffered(root,-1,0)==nullptr);
    assert(cJSON_CreateString(nullptr)==nullptr);
    int values[]={-1,0,2,INT_MAX};cJSON *numbers=cJSON_CreateIntArray(values,4);assert(numbers&&cJSON_GetArraySize(numbers)==4);cJSON_Delete(numbers);
    const char *names[]={"a","b"};cJSON *strings=cJSON_CreateStringArray(names,2);assert(strings&&cJSON_IsString(cJSON_GetArrayItem(strings,1)));cJSON_Delete(strings);
    char print_buffer[256];assert(cJSON_PrintPreallocated(root,print_buffer,sizeof(print_buffer),0));
    char short_buffer[2]={'x','x'};assert(!cJSON_PrintPreallocated(root,short_buffer,sizeof(short_buffer),0)&&short_buffer[0]=='x');
    char minify[]=" { \"a\" : 1, // cmt\n \"b\" : [ 2, 3 ] } ";cJSON_Minify(minify);assert(std::strcmp(minify,"{\"a\":1,\"b\":[2,3]}")==0);
    const char with_bom[]="\xef\xbb\xbf{\"b\":2}";cJSON *bom=cJSON_Parse(with_bom);assert(bom&&cJSON_IsObject(bom));cJSON_Delete(bom);
    const char prefix[]="{\"a\":1} trailing";const char *parse_end=nullptr;cJSON *prefix_doc=cJSON_ParseWithLengthOpts(prefix,sizeof(prefix)-1,&parse_end,0);assert(prefix_doc&&parse_end&&*parse_end==' ');cJSON_Delete(prefix_doc);
    cJSON *prefix_default=cJSON_Parse("true trailing");assert(prefix_default&&cJSON_IsTrue(prefix_default)&&prefix_default->valueint==1);cJSON_Delete(prefix_default);
    cJSON *leading_zero=cJSON_Parse("01");assert(leading_zero&&cJSON_GetNumberValue(leading_zero)==1.0);cJSON_Delete(leading_zero);
    cJSON *nul_doc=cJSON_ParseWithLengthOpts("{\"a\":1}",8,nullptr,1);assert(nul_doc);cJSON_Delete(nul_doc);
    cJSON *bad_doc=cJSON_Parse("{");assert(!bad_doc&&cJSON_GetErrorPtr()!=nullptr);
    cJSON_Delete(copy);cJSON_Delete(root);
    cJSON_Hooks hooks{test_alloc,test_free};cJSON_InitHooks(&hooks);cJSON *hooked=cJSON_CreateObject();assert(hooked);assert(cJSON_AddStringToObject(hooked,"k","v"));char *zero_prebuffer=cJSON_PrintBuffered(hooked,0,0);assert(zero_prebuffer&&std::strcmp(zero_prebuffer,"{\"k\":\"v\"}")==0);cJSON_free(zero_prebuffer);cJSON_Delete(hooked);assert(alloc_count>0&&free_count>0);cJSON_InitHooks(nullptr);
    assert(store_content_bits()==0);
    assert(store_document()==nullptr);
    return 0;
}
