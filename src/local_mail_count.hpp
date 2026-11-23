#pragma once
#include "json.hpp"
#include <cstdint>
#include <string>
namespace astrae::lobby {
// Recreates local_mail_count@0x243928's Store-side predicate: mails[*].Receipt > 1.
bool count_local_mail_receipts(const json::Value &store_document,int32_t *count,std::string *error=nullptr);
// Applies the count to Account+0x60 through the verified SetMailCount(Int32, Boolean) ABI.
bool restore_local_mail_count_runtime(void *account,std::string *error=nullptr);
}
