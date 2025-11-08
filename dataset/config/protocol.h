// MAIN 
#ifndef PROTOCOL_H
#define PROTOCOL_H

// TODO: here will be all 


#define MAX_PATH 1024
#define MAX_CONTENT (1024 * 100)  // ~100 KB per file
#define ESCAPE_BUF_SIZE (MAX_CONTENT * 2)
#define MAX_HASHES 1000


int is_duplicate(const char *content);






#endif