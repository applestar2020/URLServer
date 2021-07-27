

#include <fstream>
#include "url_cfg.h"

//静态成员赋值
CConfig *CConfig::m_instance = nullptr;

void trim(string &s);

// 构造函数
CConfig::CConfig():file_name("url.conf"),item_num(0)
{
    getCFG();
}

// 析构函数
CConfig::~CConfig()
{
    // cout << "CConfig析构函数" << endl;
}

//返回配置项的总数目
int CConfig::get_itemnum()
{
    return this->item_num;
}

// 获取其中一项的配置
string CConfig::get_item(string key,string def)
{
    if(cfg_map.count(key)==0)
        return def;
    return cfg_map[key];
}

// 从文件获取所有配置项
int CConfig::getCFG()
{
    string key, value;

    ifstream infile;

    infile.open(this->file_name);

    while (!infile.eof())
    {
        string st;
        getline(infile, st);
        if (st.size() == 0 || st[0] == '#' || st[0] == ' ')
            continue;
        trim(st);
        auto idx = st.find("=");
        if (idx == string::npos)
            continue;
        key = st.substr(0, idx);
        value = st.substr(idx + 1, st.size() - idx);
        cfg_map[key] = value;
        item_num++;
    }
    infile.close();

    return item_num;
}

// trim函数，去掉string中的空格
void trim(string &s)
{
    string st;
    for (auto &ch : s)
    {
        if (ch != ' ')
            st.push_back(ch);
    }
    s = st;
}

// 打印所有配置项
void CConfig::printCFG()
{
    for (const auto &iter : cfg_map)
    {
        cout << iter.first << "=" << iter.second << endl;
    }
}
