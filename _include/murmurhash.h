#ifndef __MURMURHASH_H_
#define __MURMURHASH_H_

#include <iostream>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <mutex>

#include <algorithm>

using namespace std;

static string cs="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

unsigned int MurmurHash2(const void *key, int len, unsigned int seed)
{
    // http://cn.voidcc.com/question/p-qvepvvth-bkh.html
    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well.

    const unsigned int m = 0x5bd1e995;
    const int r = 24;

    // Initialize the hash to a 'random' value
    unsigned int h = seed ^ len;

    // Mix 4 bytes at a time into the hash

    const unsigned char *data = (const unsigned char *)key;

    while (len >= 4)
    {
        unsigned int k = *(unsigned int *)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    // Handle the last few bytes of the input array

    switch (len)
    {
    case 3:
        h ^= data[2] << 16;
    case 2:
        h ^= data[1] << 8;
    case 1:
        h ^= data[0];
        h *= m;
    };

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated.

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}

string to_62(unsigned int x)
{
    string result;
    while (x != 0)
    {
        int r = x % 62;
        // int r=x-(x/62)*62;
        result.push_back(cs[r]);
        x /= 62;
    }
    reverse(result.begin(), result.end());
    return result;
}


#endif
