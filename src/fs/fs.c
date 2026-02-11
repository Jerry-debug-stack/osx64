#include "mm/mm.h"
#include "fs/fs.h"
#include "lib/string.h"

void init_block(void);

void init_fs_mem(void){
    init_block();
}
