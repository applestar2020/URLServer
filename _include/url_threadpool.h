#ifndef __NGX_THREADPOOL_H
#define __NGX_THREADPOOL_H

#include <vector>
#include <atomic>
#include <future>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>
#include <queue>



using namespace std;

// From: https://www.cnblogs.com/lzpong/p/6397997.html

#define  THREADPOOL_MAX_NUM 16

class Threadpool
{
public:
    Threadpool();
    Threadpool(unsigned int num);
    ~Threadpool();

private:
    using Task = function<void()>; // 任务类型
    vector<thread> _pool;          // 线程池
    queue<Task> _tasks;            // 任务队列
    mutex _lock;                   // 同步锁
    condition_variable _task_cv;   // 条件阻塞
    atomic<bool> _run;             // 线程池是否执行
    atomic<int> _idlThrNum;

public:
    template<class F, class... Args>
	auto commit(F&& f, Args&&... args) ->future<decltype(f(args...))>
	{
		if (!_run)    // stoped ??
			throw runtime_error("commit on ThreadPool is stopped.");

		using RetType = decltype(f(args...)); // typename std::result_of<F(Args...)>::type, 函数 f 的返回值类型
		auto task = make_shared<packaged_task<RetType()>>(
			bind(forward<F>(f), forward<Args>(args)...)
		); // 把函数入口及参数,打包(绑定)
		future<RetType> future = task->get_future();
		{    // 添加任务到队列
			lock_guard<mutex> lock{ _lock };//对当前块的语句加锁  lock_guard 是 mutex 的 stack 封装类，构造的时候 lock()，析构的时候 unlock()
			_tasks.emplace([task](){ // push(Task{...}) 放到队列后面
				(*task)();
			});
		}
		_task_cv.notify_one(); // 唤醒一个线程执行

		return future;
	}

    void addThread(unsigned int size); // 添加指定数量的线程
    int idlCount();                    //空闲线程的数量
    int thrCount();                    //线程数量
};

#endif
