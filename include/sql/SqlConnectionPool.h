#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include <mutex>
#include <condition_variable>
#include "../log/Log.h"

using namespace std;

class SqlConnectionPool
{
public:
	MYSQL *GetConnection();				 // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn); // 释放连接
	int GetFreeConn();					 // 获取连接
	void DestroyPool();					 // 销毁所有连接

	/**
	 * @brief 获取数据库连接池实例
	 */
	static SqlConnectionPool *GetInstance();
	/**
	 * @brief 初始化数据库连接池
	 * @param url 数据库主机地址
	 * @param User 数据库用户名
	 * @param PassWord 数据库密码
	 * @param DataBaseName 数据库名称
	 * @param Port 数据库端口号
	 * @param MaxConn 最大连接数
	 * @param close_log 日志开关
	 */
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	SqlConnectionPool();
	~SqlConnectionPool();

	int m_MaxConn;	// 最大连接数
	int m_CurConn;	// 当前已使用的连接数
	int m_FreeConn; // 当前空闲的连接数
	std::mutex lock;
	list<MYSQL *> connList;		// 连接池
	std::condition_variable cv; // 条件变量，用于实现信号量功能

public:
	string m_url;		   // 主机地址
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

// RAII，最大的好处是不用手动释放
class connectionRAII
{

public:
	connectionRAII(MYSQL **con, SqlConnectionPool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;
	SqlConnectionPool *poolRAII;
};

#endif
