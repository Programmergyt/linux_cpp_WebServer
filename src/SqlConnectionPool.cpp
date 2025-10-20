#include "sql/SqlConnectionPool.h"

SqlConnectionPool *SqlConnectionPool::GetInstance()
{
    static SqlConnectionPool instance;
    return &instance;
}

SqlConnectionPool::SqlConnectionPool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

// 构造初始化
void SqlConnectionPool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
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

MYSQL *SqlConnectionPool::GetConnection()
{
    std::unique_lock<std::mutex> unique_lock(lock);

    cv.wait(unique_lock, [this] { return !connList.empty(); });

    MYSQL *con = connList.front();
    connList.pop_front();
    --m_FreeConn;
    ++m_CurConn;
    
    // 永远不会返回 nullptr（除非程序初始化失败）
    return con;
}

bool SqlConnectionPool::ReleaseConnection(MYSQL *conn)
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

void SqlConnectionPool::DestroyPool()
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

int SqlConnectionPool::GetFreeConn()
{
    std::lock_guard<std::mutex> lock_guard(lock);
    return m_FreeConn;
}

SqlConnectionPool::~SqlConnectionPool()
{
    DestroyPool();
}

// 传入双重指针为的是改变指针指向的值，返回一个连接
connectionRAII::connectionRAII(MYSQL **con, SqlConnectionPool *connPool)
{
    *con = connPool->GetConnection();
    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}