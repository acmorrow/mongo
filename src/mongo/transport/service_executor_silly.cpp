/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kExecutor;

#include "mongo/platform/basic.h"

#include "mongo/transport/service_executor_silly.h"

#include "mongo/db/server_parameters.h"
#include "mongo/stdx/chrono.h"
#include "mongo/util/concurrency/threadlocal.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"


#include <asio.hpp>

namespace mongo {
namespace transport {

namespace {
MONGO_EXPORT_SERVER_PARAMETER(sillyServiceExecutorReserveThreads, int, 4);
MONGO_EXPORT_SERVER_PARAMETER(sillyServiceExecutorThreadIdleTimeoutMillis, int, 5000);
MONGO_EXPORT_SERVER_PARAMETER(sillyServiceExecutorThreadAgeLimit, int, 512);
MONGO_TRIVIALLY_CONSTRUCTIBLE_THREAD_LOCAL int tasksExecuted = 0;
}  // namespace

ServiceExecutorSilly::ServiceExecutorSilly(ServiceContext* ctx,
                                           std::shared_ptr<asio::io_context> ioCtx)
    : ServiceExecutorBase(ctx), _ioContext(std::move(ioCtx)) {}

ServiceExecutorSilly::~ServiceExecutorSilly() {
    invariant(!_isRunning.load());
}

Status ServiceExecutorSilly::start() {
    invariant(!_isRunning.load());
    _isRunning.store(true);
    for (int i = 0; i != sillyServiceExecutorReserveThreads.load(); ++i)
        _addThread();
    return Status::OK();
}

Status ServiceExecutorSilly::shutdown() {
    if (!_isRunning.load())
        return Status::OK();

    _isRunning.store(false);
    _ioContext->stop();

    {
        stdx::unique_lock<stdx::mutex> lock(_mutex);
        _threadDeathCondition.wait(lock, [this]{ return _threads.empty(); });
    }

    return Status::OK();
}

Status ServiceExecutorSilly::_schedule(Task task) {

    if (!_isRunning.load())
        return {ErrorCodes::BadValue, "Executor not accepting new tasks due to shutdown"};

    const auto tasksExecuting = _tasksExecuting.addAndFetch(1);
    const auto threadsRunning = _threadsRunning.load();

    auto needed = std::max(sillyServiceExecutorReserveThreads.load(), 1) + tasksExecuting - threadsRunning;

    if (needed > 0)
        for (int i = 0; i != needed; ++i)
            _addThread();

    auto wrappedTask = [this, task=std::move(task)] {
        const auto guard = MakeGuard([this] {
            _tasksExecuting.subtractAndFetch(1);
        });
        task();
        ++tasksExecuted;
    };

    _ioContext->post(std::move(wrappedTask));
    return Status::OK();
}

void ServiceExecutorSilly::_addThread() {
    dassert(_isRunning.load());

    size_t threadNum;
    decltype(_threads)::iterator where;
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        _threads.emplace_front();
        threadNum = _threads.size();
        where = _threads.begin();
    }

    *where = stdx::thread(&ServiceExecutorSilly::_threadRoutine, this, where, threadNum);
}

void ServiceExecutorSilly::_threadRoutine(ThreadList::iterator where, int threadNum) {
    _threadsRunning.fetchAndAdd(1);

    log() << "Starting worker thread, now have " << threadNum << " threads running";

    {
        asio::io_context::work work(*_ioContext);

        const auto guard = MakeGuard([this] {
            _threadsRunning.subtractAndFetch(1);
        });

        while (_isRunning.load()) {
            const auto timeout = stdx::chrono::milliseconds(sillyServiceExecutorThreadIdleTimeoutMillis.load());

            const auto handlersRun = _ioContext->run_for(timeout);

            if (!_isRunning.load()) {
                log() << "Thread " << threadNum << " will terminate, due to shutdown";
                break;
            }
            else if (handlersRun) {
                if (tasksExecuted >= sillyServiceExecutorThreadAgeLimit.load()) {
                    log() << "Thread " << threadNum << " will retire in favor of a new thread, due to exhaustion";
                    _addThread();
                    break;
                }
            } else {
                if (_threadsRunning.load() > std::max(sillyServiceExecutorReserveThreads.load(), 1)) {
                    log() << "Thread " << threadNum << " will terminate, due to idleness";
                    break;
                }
            }
        }
    }

    stdx::thread doomed;
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        doomed = std::move(*where);
        _threads.erase(where);
        threadNum = _threads.size();
    }
    doomed.detach();
    _threadDeathCondition.notify_one();

    log() << "Exiting worker thread, now have " << threadNum << " threads running";
}

}  // namespace transport
}  // namespace mongo
