#include "cJSON.h"
#include "original_store_abi.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
_Static_assert(sizeof(cJSON)==64,"original cJSON struct size");
_Static_assert(offsetof(cJSON, type)==24 && offsetof(cJSON, string)==56,"original cJSON field offsets");
_Static_assert(sizeof(cJSON_Hooks)==16,"original cJSON hooks size");
_Static_assert(sizeof(struct store_reward)==24 && offsetof(struct store_reward,id)==8,"store_reward ABI");
_Static_assert(sizeof(struct store_cost)==24 && offsetof(struct store_cost,amount)==16,"store_cost ABI");
int main(void){
    cJSON *root=cJSON_CreateObject();
    if(!root)return 1;
    if(!cJSON_AddNumberToObject(root,"id",UINT64_C(42)))return 2;
    char *text=cJSON_PrintUnformatted(root);
    if(!text||strcmp(text,"{\"id\":42}")!=0)return 3;
    cJSON_free(text);
    cJSON_Delete(root);
    if(CJSON_VERSION_MAJOR!=1||CJSON_VERSION_MINOR!=7||CJSON_VERSION_PATCH!=19)return 8;
    cJSON *number=cJSON_CreateNumber(1);
    if(!number)return 9;
    cJSON_SetIntValue(number,17);
    if(number->valueint!=17||number->valuedouble!=17.0)return 10;
    cJSON_SetNumberValue(number,2.5);
    if(number->valuedouble!=2.5)return 11;
    cJSON_Delete(number);
    cJSON *boolean=cJSON_CreateTrue();
    if(!boolean||cJSON_SetBoolValue(boolean,0)!=cJSON_False||!cJSON_IsFalse(boolean))return 12;
    cJSON_Delete(boolean);
    cJSON *array=cJSON_CreateArray();
    if(!array||!cJSON_AddItemToArray(array,cJSON_CreateNumber(3)))return 13;
    int foreach_count=0;
    cJSON *entry=NULL;
    cJSON_ArrayForEach(entry,array){++foreach_count;}
    if(foreach_count!=1)return 14;
    cJSON_Delete(array);
    if(strcmp(cJSON_Version(),"1.7.19")!=0)return 4;
    if(store_content_bits()!=0)return 5;
    if(store_document()!=NULL)return 6;
    if(Java_com_nc_astraeoffline_OfflineBridge_getActions(NULL,NULL)!=0)return 7;
    Java_com_nc_astraeoffline_OfflineBridge_requestAction(NULL,NULL,123);
    return 0;
}
