/*
 * Name: Lanyixuan Xu    login id: xul  
 * reference:
 * http://www.cs.wm.edu/~liqun/teaching/cs304/cs304_15f/labs/cachelab.html
 * https://github.com/irisyuan/cache/blob/master/csim.c
 *
 */

#include "cachelab.h"
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int s, S, E, b, t, i, j, k;
int hit_count = 0, miss_count = 0, eviction_count = 0;
char *trace_file;
FILE *fp = NULL;

struct cache_line{
  int valid;
  unsigned tag;
  struct cache_line *next;
} ;

int main(int argc, char * argv[]) {
  int opt;
/*
 * take command line input
 */
  while ((opt = getopt(argc, argv, "s:E:b:t:"))!= -1) {
	switch( opt )
        {
		case 's': s = atoi(optarg);
			break;

		case 'E': E = atoi(optarg);
			break;

		case 'b': b = atoi(optarg);
			break;

		case 't':
			trace_file = optarg;
			break;

		default:
                	printf("ERROR!!");
			break;
		}
	}
  FILE *fp = fopen (trace_file, "r"); //open trace file

  S = 1 << s;   // number of sets
/*
 * set 2D array
 */

  struct cache_line** cache = (struct cache_line**) malloc(sizeof(struct cache_line*) * S);

  for (i=0; i<S; i++){
    cache[i]=(struct cache_line*) malloc(sizeof(struct cache_line) * E);
      for (j=0; j<E; j++){
        cache[i][j].valid = 0;
        cache[i][j].tag = 0;
		}
	}
  while(!feof(fp)) {
     //read from the  trace file
	char op;
	long long address;
  	unsigned int size;
        fscanf(fp, " %c %llx,%d\n", &op, &address, &size);
//	printf("%c %x, %d\n", op, address, size);

	if (op == 'I')		//doesn't matter
	continue;

	unsigned int tag_bits, set_bits;
        tag_bits = address >> (b + s);
        set_bits = (address >> b) % (1 << s);

//	printf("tag_bits = %x, set_bits = %x\n", tag_bits, set_bits);          
        int hitting = 0;
/*
 * search in set, it has to be valid
 * as well as has the same bits 
 * to hit!!
 */

	for (k = 0; k < E; k++){
		if(cache[set_bits][k].valid != 0) {
                	if(tag_bits == cache[set_bits][k].tag) {
				cache[set_bits][k].valid = 0;
                    		hit_count++;
                    		hitting = 1;
                    		printf("%c %llx, hit\n", op, address);
				}
			cache[set_bits][k].valid++;
			}
		}

/*if it doesn't hit, that would be a miss.
 * Still need to run to see if there has evict.
 */


	if(!hitting) {
		miss_count++;
		printf("%c %llx, miss\n", op, address);
	
		int evicting = 1;

		for (k = 0; k < E; k++) {
			if(cache[set_bits][k].valid == 0) {
				cache[set_bits][k].tag = tag_bits;
                    		cache[set_bits][k].valid = 1;
                    		evicting = 0;
                    		break;
			}
		}
/* a eviction!!
 */
		int max = 0;
		if(evicting == 1) {
			for(k = 0; k < E; k++) {
				if(cache[set_bits][k].valid > cache[set_bits][max].valid)
					max = k;
			}
			cache[set_bits][max].tag = tag_bits;
                	cache[set_bits][max].valid = 1;
                	eviction_count++;
                	printf("evict\n");
		}
	}

	if(op == 'M') {
		hit_count++;
            	printf("hit\n");
		}	
}

  printSummary(hit_count, miss_count, eviction_count);
  return 0;
}

