#ifndef __URL_MYSQL_
#define __URL_MYSQL_

#include <iostream>
#include <string>
#include <mysql/mysql.h>

using namespace std;

class MyDB
{
    class AutoRelase
    {
    public:
        ~AutoRelase()
        {
            if (m_instance)
            {
                // cout<<"delete m_instance"<<endl;
                delete m_instance;
            }
        }
    };

public:
    static MyDB *Getinstance()
    {
        if (m_instance == nullptr)
        {
            static AutoRelase ar;
            m_instance = new MyDB();
        }
        return m_instance;
    }

    bool initDB()
    {
        mysql = mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str(), db_name.c_str(), 0, nullptr, 0);
        if (mysql == nullptr)
        {
            cout << "Error: " << mysql_error(mysql);
            return false;
        }
        mysql->reconnect = 1;
        return true;
    }

    string find_surl_SQL(string lurl)
    {
        //mysql_query()执行成功返回0,执行失败返回非0值。
        string sql = "SELECT hash FROM url_map where lurl='" + lurl + "';";
        if (mysql_query(mysql, sql.c_str()))
        {
            cout << "Query Error: " << mysql_error(mysql);
            return "-1";
        }
        else // 查询成功
        {
            result = mysql_store_result(mysql); //获取结果集
            if (result)                         // 返回了结果集
            {
                int num_rows = mysql_num_rows(result);
                mysql_free_result(result);
                if (num_rows)
                {
                    //获取下一行数据
                    row = mysql_fetch_row(result);
                    if (row < 0)
                        return "-1";
                    return row[0];
                }
            }
        }
        return "-1";
    }

    bool add_surl_SQL(string surl, string lurl)
    {
        string sql = "INSERT INTO url_map (lurl, hash) VALUES ('" + lurl + "', '" + surl + "');";
        return exeSQL(sql);
    }

    string find_lurl_SQL(string surl)
    {
        //mysql_query()执行成功返回0,执行失败返回非0值。
        string sql = "SELECT lurl FROM url_map where hash='" + surl + "';";
        if (mysql_query(mysql, sql.c_str()))
        {
            cout << "Query Error: " << mysql_error(mysql);
            return "-1";
        }
        else // 查询成功
        {
            result = mysql_store_result(mysql); //获取结果集
            if (result)                         // 返回了结果集
            {
                int num_rows = mysql_num_rows(result);
                mysql_free_result(result);
                if (num_rows)
                {
                    //获取下一行数据
                    row = mysql_fetch_row(result);
                    if (row < 0)
                        return "-1";
                    return row[0];
                }
            }
        }

        return "-1";
    }

    bool exeSQL(string sql)
    {
        //mysql_query()执行成功返回0,执行失败返回非0值。
        if (mysql_query(mysql, sql.c_str()))
        {
            cout << "Query Error: " << mysql_error(mysql);
            return false;
        }
        else // 查询成功
        {
            result = mysql_store_result(mysql); //获取结果集
            if (result)                         // 返回了结果集
            {
                int num_fields = mysql_num_fields(result); //获取结果集中总共的字段数，即列数
                int num_rows = mysql_num_rows(result);     //获取结果集中总共的行数
                for (int i = 0; i < num_rows; i++)         //输出每一行
                {
                    //获取下一行数据
                    row = mysql_fetch_row(result);
                    if (row < 0)
                        break;

                    for (int j = 0; j < num_fields; j++) //输出每一字段
                    {
                        cout << row[j] << "\t\t";
                    }
                    cout << endl;
                }
                mysql_free_result(result);
            }
            else // result==NULL
            {
                if (mysql_field_count(mysql) == 0) //代表执行的是update,insert,delete类的非查询语句
                {
                    // (it was not a SELECT)
                    int num_rows = mysql_affected_rows(mysql); //返回update,insert,delete影响的行数
                }
                else // error
                {
                    cout << "Get result error: " << mysql_error(mysql);
                    return false;
                }
            }
        }
        return true;
    }

private:
    MyDB(string host, string user, string passwd, string db_name) : host(host), user(user), passwd(passwd), db_name(db_name)
    {
        mysql = mysql_init(nullptr);
        if (!mysql)
            exit(1);
        initDB();
    }

    MyDB() : host("127.0.0.1"), user("****"), passwd("****"), db_name("test")
    {
        mysql = mysql_init(nullptr);
        if (!mysql)
            exit(1);
        initDB();
    }

    ~MyDB()
    {
        if (mysql)
            mysql_close(mysql);
    }

    static MyDB *m_instance;
    MYSQL *mysql;      //连接mysql句柄指针
    MYSQL_RES *result; //指向查询结果的指针
    MYSQL_ROW row;     //按行返回的查询信息

    string host, user, passwd, db_name;
};

MyDB *MyDB::m_instance = nullptr;

#endif
