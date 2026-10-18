#include "pet_transaction.h"
bool pet_network_transaction(const pet_config_t *candidate,pet_transaction_ops_t ops){
 if(!pet_config_valid(candidate)||!candidate->network_count||!ops.connect||!ops.commit||!ops.restore)return false;
 bool ok=ops.connect(&candidate->networks[0],ops.context);
 if(ok)ok=ops.commit(candidate,ops.context);
 if(!ok)ops.restore(ops.context);
 return ok;
}
