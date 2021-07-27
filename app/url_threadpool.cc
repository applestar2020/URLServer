


#include <iostream>


#include "url_threadpool.h"

Threadpool::Threadpool(unsigned int size = 4) : _run(true), _idlThrNum(0)
{
    addThread(size);
}

Threadpool::Threadpool() : _run(true), _idlThrNum(0)
{
    
}

Threadpool::~Threadpool()
{
    _run = false;
    _task_cv.notify_all(); // 唤醒所有线程
    for (thread &thread : _pool)
    {
        if (thread.joinable())
            thread.join(); //等待任务结束，前提是线程一定能执行完
    }
}

// template <class F, class... Args>
// auto Threadpool::commit(F&& f, Args&&... args) -> future<decltype(f(args...))>
// template<class F, class... Args>
// auto Threadpool::commit(F&& f, Args&&... args) ->future<decltype(f(args...))>
// {
// 		if (!_run)    // stoped ??
// 			throw runtime_error("commit on ThreadPool is stopped.");

// 		using RetType = decltype(f(args...)); // typename std::result_of<F(Args...)>::type, 函数 f 的返回值类型
// 		auto task = make_shared<packaged_task<RetType()>>(
// 			bind(forward<F>(f), forward<Args>(args)...)
// 		); // 把函数入口及参数,打包(绑定)
// 		future<RetType> future = task->get_future();
// 		{    // 添加任务到队列
// 			lock_guard<mutex> lock{ _lock };//对当前块的语句加锁  lock_guard 是 mutex 的 stack 封装类，构造的时候 lock()，析构的时候 unlock()
// 			_tasks.emplace([task](){ // push(Task{...}) 放到队列后面
// 				(*task)();
// 			});
// 		}
        
// 		_task_cv.notify_one(); // 唤醒一个线程执行

// 		return future;
// }

//空闲线程数量
int Threadpool::idlCount() { return _idlThrNum; }
//线程数量
int Threadpool::thrCount() { return _pool.size(); }

void Threadpool::addThread(unsigned int size)
{
    for (; _pool.size() < THREADPOOL_MAX_NUM && size > 0; size--)
    {
        _pool.emplace_back([this] {
            while (_run)
            {
                Task task;
                // 这里为什么要加{}？？
                {
                    // std::cout << " ================ " << std::this_thread::get_id() << std::endl;
                    unique_lock<mutex> lock{_lock};
                    // 不理解参考这里：https://blog.csdn.net/lv0918_qian/article/details/81745723
                    _task_cv.wait(lock, [this] {
                        return !_run || !_tasks.empty();
                    });
                    if (!_run && _tasks.empty())
                        return;
                    task = move(_tasks.front()); // 按先进先出从队列取一个 task
                    _tasks.pop();
                }
                _idlThrNum--;
                task(); //执行任务
                _idlThrNum++;
            }
        });
        _idlThrNum++;
    }
}
