
#ifndef __URL_CFG_H__
#define __URL_CFG_H__

#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;
// 使用单例模式
class CConfig
{
    // 类中套类，用于自动释放
    class AutoRelease
    {
    public:
        ~AutoRelease()
        {
            if (m_instance)
            {
                // cout << "~AutoRelease:"<< (int*)m_instance << endl;
                delete m_instance;
                m_instance=nullptr;
            }
        }
    };

private:
    static CConfig *m_instance; // 单例模式实例指针
    string file_name;           // 配置文件名称
    int item_num;               // 配置项数目
    CConfig();                  // 构造函数

public:
    unordered_map<string, string> cfg_map; // 存储配置项的map容器
    ~CConfig();                            // 析构函数
    static CConfig *GetInstance()
    {
        if (m_instance == nullptr)
        {
            static AutoRelease ar;
            m_instance = new CConfig();
        }
        return m_instance;
    }
    int get_itemnum();           // 获取配置项数目
    string get_item(string key,string def); // 获取其中一项配置项
    int getCFG();                // 从文件获取所有配置项
    void printCFG();             // 打印所有配置项
};

#endif
