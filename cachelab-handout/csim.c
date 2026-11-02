#include "cachelab.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

struct cache_line {
    int valid;
    unsigned long tag;
    int lru;
};

int main(int argc, char** argv)
{
    int opt,s = 0,E = 0,b = 0;
    int flagv = 0;
    FILE* pfile;
    char* filename = NULL;
    while (-1 != (opt = getopt(argc, argv, "vs:E:b:t:"))) {
        switch(opt) {
            case 'v':
                flagv = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                filename = optarg;
                break;
            default:
                printf("wrong argument\n");
                break;
        }
    }
    int S = 1UL << s;
    struct cache_line* cache;
    cache = calloc(S * E, sizeof(struct cache_line));
    pfile = fopen(filename, "r");
    char identifier;
    unsigned long address;
    int size;
    unsigned long tag1;
    int setnum;
    int hit_count = 0, miss_count = 0, eviction_count = 0;
    int time = 0;
    while (fscanf(pfile, " %c %lx,%d", &identifier, &address, &size) > 0) { 
        if (identifier == 'I') {
            continue;
        }
        if (flagv) printf("%c %lx,%d", identifier, address, size);
        tag1 = address >> (s + b);
        setnum = (address >> b) & ((1UL << s) - 1);
        struct cache_line* pline = cache;
        pline += setnum * E;
        bool flag = false;
        for (int i = 0; i < E; i++, pline++) {
            if (pline->valid == 0) {
                continue;
            }
            if (pline->tag != tag1) {
                continue;
            }
            hit_count++;
            pline->lru = time;
            if (identifier == 'M') {
                time++;
                pline->lru = time;
                hit_count++;
                if (flagv) printf(" hit");
            }
            if (flagv) printf(" hit");
            flag = true;
            break;
        }
        if (flag == false) {
            miss_count++;
            if (flagv) printf(" miss");
            pline = cache + setnum * E;
            for (int i = 0; i < E; i++, pline++) {
                if (pline->valid == 0) {
                    pline->valid = 1;
                    pline->tag = tag1;
                    pline->lru = time;
                    flag = true;
                    break;
                }
            }
            if (flag == false) {
                eviction_count++;
                if (flagv) printf(" eviction");
                pline = cache + setnum * E;
                int lru1 = pline->lru;
                int num = 0;
                for (int i = 0; i < E; i++, pline++) {
                    if (pline->lru < lru1) {
                        lru1 = pline->lru;
                        num = i;
                    }
                }
                pline = cache + setnum * E;
                pline += num;
                pline->tag = tag1;
                pline->lru = time;
            }
            if (identifier == 'M') {
                time++;
                pline->lru = time;
                hit_count++;
                if (flagv) printf(" hit");
            }
        }
        if (flagv) printf("\n");
        time++;
    }
    fclose(pfile);
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}