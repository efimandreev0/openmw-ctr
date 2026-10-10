#include "operationholder.hpp"

#include "operation.hpp"

CSMDoc::OperationHolder::OperationHolder(QObject* parent, Operation* operation)
    : QObject(parent)
    , mOperation(operation)
{
    connect(mOperation, &Operation::progress, this, &OperationHolder::progress);

    connect(mOperation, &Operation::reportMessage, this, &OperationHolder::reportMessage);

    connect(mOperation, &Operation::done, this, &OperationHolder::doneSlot);

    connect(this, &OperationHolder::abortSignal, mOperation, &Operation::abort);

    connect(&mThread, &QThread::started, mOperation, &Operation::run);

    connect(&mThread, &QThread::finished, mOperation, &Operation::cleanup, Qt::DirectConnection);
}

CSMDoc::OperationHolder::~OperationHolder()
{
    quit();
}

bool CSMDoc::OperationHolder::isRunning() const
{
    return mThread.isRunning();
}

void CSMDoc::OperationHolder::start()
{
    if (!mOperation || mThread.isRunning())
        return;

    mOperation->moveToThread(&mThread);
    mThread.start();
}

void CSMDoc::OperationHolder::abort()
{
    emit abortSignal();
}

void CSMDoc::OperationHolder::quit()
{
    if (mThread.isRunning())
    {
        abort();
        mThread.quit();
        mThread.wait();
    }

    if (mOperation)
    {
        delete mOperation;
        mOperation = nullptr;
    }
}

void CSMDoc::OperationHolder::doneSlot(int type, bool failed)
{
    mThread.quit();
    mThread.wait();
    emit done(type, failed);
}
