#include "sql/sql_connection_pool.h"

connection_pool *connection_pool::GetInstance()
{
    static connection_pool instance;
    return &instance;
}

connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if (con == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if (con == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }

    m_MaxConn = m_FreeConn;
}

MYSQL *connection_pool::GetConnection()
{
    std::unique_lock<std::mutex> unique_lock(lock);

    // 等待 10 微秒，或者直到有连接
    if (cv.wait_for(unique_lock, std::chrono::microseconds(10), [this] { return !connList.empty(); }))
    {
        // 成功等到连接
        MYSQL *con = connList.front();
        connList.pop_front();
        --m_FreeConn;
        ++m_CurConn;
        return con;
    }
    
    // 超时了，仍然没有连接
    return nullptr;
}

bool connection_pool::ReleaseConnection(MYSQL *conn)
{
    if (conn == NULL)
        return false;

    {
        std::lock_guard<std::mutex> lock_guard(lock);
        connList.push_back(conn);
        ++m_FreeConn;
        --m_CurConn;
    }

    cv.notify_one(); // 通知等待的线程有连接可用
    return true;
}

void connection_pool::DestroyPool()
{
    std::lock_guard<std::mutex> lock_guard(lock);
    if (!connList.empty())
    {
        for (auto it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
}

int connection_pool::GetFreeConn()
{
    std::lock_guard<std::mutex> lock_guard(lock);
    return m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

// 传入双重指针为的是改变指针指向的值，返回一个连接
connectionRAII::connectionRAII(MYSQL **con, connection_pool *connPool)
{
    *con = connPool->GetConnection();
    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}