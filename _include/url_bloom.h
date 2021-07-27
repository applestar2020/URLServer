#ifndef __URL_BLOOM_
#define __URL_BLOOM_

#include <iostream>
#include <vector>
#include <string.h>
#include <fstream>

#include "url_log.h"
#include "murmurhash.h"

using namespace std;

/* 
1. k个哈希函数
2. bit数组
3. 插入函数
4. 查找函数
 */
class BloomFilter
{
public:
    BloomFilter(size_t _m, int _k) : k(_k), m(_m), m_filter(nullptr), filterFilePath("filter.bf")
    {
        initSize();
    }

    BloomFilter(int _k) : k(_k), m(5000000), m_filter(nullptr), filterFilePath("filter.bf")
    {
        initSize();
    }

    BloomFilter(string _filterFilePath) : k(4), m(5000000), m_filter(nullptr), filterFilePath(_filterFilePath)
    {
        initSize();
        struct stat sbuf;
        if (stat(filterFilePath.c_str(), &sbuf) == 0) // 文件存在
        {
            loadFilter(filterFilePath);
            // ngx_log_error_core(6, 0, "%s 过滤器加载成功", filterFilePath.c_str());
            cout<<"布隆过滤器："<<filterFilePath<<" 加载成功"<<endl;
        }
    }

    ~BloomFilter()
    {
        if (m_filter != nullptr)
        {
            storeFilter();
            delete[] m_filter;
            m_filter = nullptr;
        }
    }

    void initSize()
    {
		// murmurhash的seed
        seeds = {11, 23, 37, 97, 83, 103, 137, 149, 151};
        len = m / 8 + 1;
        m_filter = new uint8_t[len];
    }

    void insert(string &s)
    {
        unsigned int hashval = 0;
        for (int i = 0; i < k; i++)
        {
            hashval = MurmurHash2(s.c_str(), s.size(), seeds[i]) % m;
            // cout << hashval << " ";
            m_filter[hashval / 8] |= (0x1 << (hashval % 8));
        }
        // cout << endl;
    }

    bool contains(string &s)
    {
        unsigned int hashval = 0;
        for (int i = 0; i < k; i++)
        {
            hashval = MurmurHash2(s.c_str(), s.size(), seeds[i]) % m;
            // cout << hashval << " ";
            if (m_filter[hashval / 8] & (0x1 << (hashval % 8)))
                continue;
            else
                return false;
        }
        // cout << endl;
        return true;
    }

    void storeFilter(const string &_filterFilePath) const
    {
        std::ofstream ofs(_filterFilePath.c_str(), std::ios::out | std::ios::binary);
        ofs.write(reinterpret_cast<char *>(m_filter), len);
        ofs.flush();
        ofs.close();
    }

    void storeFilter()
    {
        std::ofstream ofs(filterFilePath.c_str(), std::ios::out | std::ios::binary);
        ofs.write(reinterpret_cast<char *>(m_filter), len);
        ofs.flush();
        ofs.close();
    }

    void loadFilter(const string &filterFilePath)
    {
        std::ifstream file(filterFilePath);
        file.read(reinterpret_cast<char *>(m_filter), len);
        file.close();
    }

private:
    size_t m;
    int k;
    size_t len;
    string filterFilePath;
    vector<int> seeds;
    uint8_t *m_filter;
};

#endif