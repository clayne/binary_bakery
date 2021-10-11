#include "data_store.h"

#include "C:\Dropbox\code\binary_inliner\input_tests\binary_bakery_payload.h"

auto example::decode_to_vector(
   const char* name,
   bb::decompression_fun_type decomp_fun
) -> std::vector<example::user_type>
{
   return bb::decode_to_vector<user_type>(name, decomp_fun);
}
